/***
  This file is part of PulseAudio.

  Copyright 2005-2006 Lennart Poettering
  Copyright 2010 litl, LLC

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/input.h>

#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/modargs.h>
#include <pulsecore/namereg.h>
#include <pulsecore/sink.h>
#include <pulsecore/source.h>

#include "module-sense-jack-evdev-symdef.h"

PA_MODULE_AUTHOR("Joe Shaw");
PA_MODULE_DESCRIPTION("Associate a jack sense evdev input device with a PA sink");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(FALSE);

static const char *valid_modargs[] = {
    "name",
    "sink_name",
    "source_name",
    "device_file",
    NULL
};

struct userdata {
    char *name;
    char *device_file;
    int fd;
    pa_sink *sink;
    pa_source *source;
    pa_io_event *io_event;
};

#define test_bit(bytearray, bit) ((bytearray)[(bit) / 8] & (1 << ((bit) % 8)))

static void
set_device_file_property(struct userdata *u)
{
    char propname[256];
    pa_proplist *pl;

    snprintf(propname, sizeof(propname),
             "sense_jack_evdev.%s.device_file",
             u->name);

    pa_log("setting prop %s", propname);

    pl = pa_proplist_new();
    pa_proplist_sets(pl, propname, u->device_file);

    if (u->sink != NULL)
        pa_sink_update_proplist(u->sink, PA_UPDATE_REPLACE, pl);

    if (u->source != NULL)
        pa_source_update_proplist(u->source, PA_UPDATE_REPLACE, pl);

    pa_proplist_free(pl);
}

static void
set_name_property(struct userdata *u, const char *name)
{
    char propname[256];
    pa_proplist *pl;

    snprintf(propname, sizeof(propname),
             "sense_jack_evdev.%s.jack_name",
             u->name);

    pa_log("setting prop %s", propname);

    pl = pa_proplist_new();
    pa_proplist_sets(pl, propname, name);

    if (u->sink != NULL)
        pa_sink_update_proplist(u->sink, PA_UPDATE_REPLACE, pl);

    if (u->source != NULL)
        pa_source_update_proplist(u->source, PA_UPDATE_REPLACE, pl);

    pa_proplist_free(pl);
}

static void
set_jack_sense_property(struct userdata *u, uint8_t is_set)
{
    char propname[256];
    pa_proplist *pl;

    snprintf(propname, sizeof(propname),
             "sense_jack_evdev.%s.sensed",
             u->name);

    pa_log("setting prop %s", propname);

    pl = pa_proplist_new();
    pa_proplist_setf(pl, propname, "%d", is_set);

    if (u->sink != NULL)
        pa_sink_update_proplist(u->sink, PA_UPDATE_REPLACE, pl);

    if (u->source != NULL)
        pa_source_update_proplist(u->source, PA_UPDATE_REPLACE, pl);

    pa_proplist_free(pl);
}

static void
io_callback(pa_mainloop_api *io,
            pa_io_event *event,
            int fd,
            pa_io_event_flags_t flags,
            void *user_data)
{
    pa_module *module = user_data;
    struct userdata *u = module->userdata;

    if (flags & (PA_IO_EVENT_HANGUP | PA_IO_EVENT_ERROR)) {
        pa_log("Device %s has broken connection",
               u->device_file);
        goto fail;
    }

    if (flags & PA_IO_EVENT_INPUT) {
        struct input_event ev;
        int fd_type;

        if (pa_loop_read(u->fd, &ev, sizeof(ev), &fd_type) <= 0) {
            pa_log("Failed to read from event device %s: %s",
                   u->device_file, pa_cstrerror(errno));
            goto fail;
        }

        if (ev.type != EV_SW)
            return;

        if (ev.code == SW_HEADPHONE_INSERT ||
            ev.code == SW_MICROPHONE_INSERT ||
            ev.code == SW_LINEOUT_INSERT)
            set_jack_sense_property(u, ev.value);
    }

    return;

fail:
    module->core->mainloop->io_free(u->io_event);
    u->io_event = NULL;

    pa_module_unload_request(module, TRUE);
}

int
pa__init(pa_module *module)
{
    pa_modargs *modargs;
    struct userdata *u;
    const char *sink_name;
    const char *source_name;
    char name[256];
    uint8_t evbits[EV_MAX / 8 + 1];
    uint8_t swbits[SW_MAX / 8 + 1];
    uint8_t is_set;

    //PA_DEBUG_TRAP;

    modargs = pa_modargs_new(module->argument, valid_modargs);
    if (modargs == NULL) {
        pa_log("Failed to parse module arguments.");
        goto fail;
    }

    u = pa_xnew0(struct userdata, 1);
    module->userdata = u;

    u->name = pa_xstrdup(pa_modargs_get_value(modargs, "name", "jack"));

    u->device_file = pa_xstrdup(pa_modargs_get_value(modargs, "device_file", NULL));
    if (u->device_file == NULL) {
        pa_log("Module requires device_file argument");
        goto fail;
    }

    sink_name = pa_modargs_get_value(modargs, "sink_name", NULL);
    source_name = pa_modargs_get_value(modargs, "source_name", NULL);

    if (sink_name == NULL && source_name == NULL) {
        pa_log("Module requires either sink_name or source_name argument");
        goto fail;
    }

    if (sink_name != NULL) {
        u->sink = pa_namereg_get(module->core, sink_name, PA_NAMEREG_SINK);
        if (u->sink == NULL) {
            pa_log("Could not find a sink named %s", sink_name);
            goto fail;
        }
    }

    if (source_name != NULL) {
        u->source = pa_namereg_get(module->core, source_name, PA_NAMEREG_SOURCE);
        if (u->source == NULL) {
            pa_log("Could not find a source named %s", source_name);
            goto fail;
        }
    }

    u->fd = open(u->device_file, O_RDONLY | O_CLOEXEC, 0);
    if (u->fd < 0) {
        pa_log("Unable to open device file %s: %s",
               u->device_file, pa_cstrerror(errno));
        goto fail;
    }

    if (ioctl(u->fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        pa_log("Unable to get name from device file %s: %s",
               u->device_file, pa_cstrerror(errno));
        goto fail;
    }

    // FIXME pa_log_info
    pa_log("Found %s on %s", name, u->device_file);

    memset(evbits, 0, sizeof(evbits));
    if (ioctl(u->fd, EVIOCGBIT(0, EV_MAX), evbits) < 0) {
        pa_log("Unable to get event data from %s: %s",
               u->device_file, pa_cstrerror(errno));
        goto fail;
    }

    if (!test_bit(evbits, EV_SW)) {
        pa_log("Device %s does not support switches",
               u->device_file);
        goto fail;
    }

    memset(swbits, 0, sizeof(swbits));
    if (ioctl(u->fd, EVIOCGSW(sizeof(swbits)), swbits) < 0) {
        pa_log("Unable to get switch data from %s: %s",
               u->device_file, pa_cstrerror(errno));
        goto fail;
    }

    set_device_file_property(u);
    set_name_property(u, name);

    is_set = !!(test_bit(swbits, SW_HEADPHONE_INSERT) |
                test_bit(swbits, SW_MICROPHONE_INSERT) |
                test_bit(swbits, SW_LINEOUT_INSERT));
    set_jack_sense_property(u, is_set);

    u->io_event = module->core->mainloop->io_new(
        module->core->mainloop,
        u->fd,
        PA_IO_EVENT_INPUT | PA_IO_EVENT_HANGUP,
        io_callback,
        module);

    pa_modargs_free(modargs);

    return 0;

 fail:
    if (modargs != NULL)
        pa_modargs_free(modargs);

    pa__done(module);

    return -1;
}

void
pa__done(pa_module *module)
{
    struct userdata *u = module->userdata;

    if (u) {
        pa_xfree(u->name);
        pa_xfree(u->device_file);

        if (u->io_event)
            module->core->mainloop->io_free(u->io_event);

        if (u->fd > 0)
            pa_close(u->fd);

        pa_xfree(u);
    }
}
