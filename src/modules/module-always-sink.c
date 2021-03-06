/***
    This file is part of PulseAudio.

    Copyright 2008 Colin Guthrie

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

#include <pulse/xmalloc.h>
#include <pulse/i18n.h>

#include <pulsecore/core.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/namereg.h>
#include <pulsecore/core-util.h>

#include "module-always-sink-symdef.h"

PA_MODULE_AUTHOR("Colin Guthrie");
PA_MODULE_DESCRIPTION(_("Always keeps at least one sink loaded even if it's a null one"));
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);
PA_MODULE_USAGE(
        "sink_name=<name of sink>");

#define DEFAULT_SINK_NAME "auto_null"

static const char* const valid_modargs[] = {
    "sink_name",
    NULL,
};

struct userdata {
    pa_hook_slot *put_slot, *unlink_slot;
    uint32_t null_module;
    pa_bool_t ignore;
    char *sink_name;
};

static void load_null_sink_if_needed(pa_core *c, pa_sink *sink, struct userdata* u) {
    pa_sink *target;
    uint32_t idx;
    char *t;
    pa_module *m;

    pa_assert(c);
    pa_assert(u);
    pa_assert(u->null_module == PA_INVALID_INDEX);

    /* Loop through all sinks and check to see if we have *any*
     * sinks. Ignore the sink passed in (if it's not null) */
    for (target = pa_idxset_first(c->sinks, &idx); target; target = pa_idxset_next(c->sinks, &idx))
        if (!sink || target != sink)
            break;

    if (target)
        return;

    pa_log_debug("Autoloading null-sink as no other sinks detected.");

    u->ignore = TRUE;

    t = pa_sprintf_malloc("sink_name=%s sink_properties='device.description=\"%s\"'", u->sink_name,
                          _("Dummy Output"));
    m = pa_module_load(c, "module-null-sink", t);
    u->null_module = m ? m->index : PA_INVALID_INDEX;
    pa_xfree(t);

    u->ignore = FALSE;

    if (!m)
        pa_log_warn("Unable to load module-null-sink");
}

static pa_hook_result_t put_hook_callback(pa_core *c, pa_sink *sink, void* userdata) {
    struct userdata *u = userdata;

    pa_assert(c);
    pa_assert(sink);
    pa_assert(u);

    /* This is us detecting ourselves on load... just ignore this. */
    if (u->ignore)
        return PA_HOOK_OK;

    /* There's no point in doing anything if the core is shut down anyway */
    if (c->state == PA_CORE_SHUTDOWN)
        return PA_HOOK_OK;

    /* Auto-loaded null-sink not active, so ignoring newly detected sink. */
    if (u->null_module == PA_INVALID_INDEX)
        return PA_HOOK_OK;

    /* This is us detecting ourselves on load in a different way... just ignore this too. */
    if (sink->module && sink->module->index == u->null_module)
        return PA_HOOK_OK;

    pa_log_info("A new sink has been discovered. Unloading null-sink.");

    pa_module_unload_request_by_index(c, u->null_module, TRUE);
    u->null_module = PA_INVALID_INDEX;

    return PA_HOOK_OK;
}

static pa_hook_result_t unlink_hook_callback(pa_core *c, pa_sink *sink, void* userdata) {
    struct userdata *u = userdata;

    pa_assert(c);
    pa_assert(sink);
    pa_assert(u);

    /* First check to see if it's our own null-sink that's been removed... */
    if (u->null_module != PA_INVALID_INDEX && sink->module && sink->module->index == u->null_module) {
        pa_log_debug("Autoloaded null-sink removed");
        u->null_module = PA_INVALID_INDEX;
        return PA_HOOK_OK;
    }

    /* There's no point in doing anything if the core is shut down anyway */
    if (c->state == PA_CORE_SHUTDOWN)
        return PA_HOOK_OK;

    load_null_sink_if_needed(c, sink, u);

    return PA_HOOK_OK;
}

int pa__init(pa_module*m) {
    pa_modargs *ma = NULL;
    struct userdata *u;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        return -1;
    }

    m->userdata = u = pa_xnew(struct userdata, 1);
    u->sink_name = pa_xstrdup(pa_modargs_get_value(ma, "sink_name", DEFAULT_SINK_NAME));
    u->put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_PUT], PA_HOOK_LATE, (pa_hook_cb_t) put_hook_callback, u);
    u->unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) unlink_hook_callback, u);
    u->null_module = PA_INVALID_INDEX;
    u->ignore = FALSE;

    pa_modargs_free(ma);

    load_null_sink_if_needed(m->core, NULL, u);

    return 0;
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->put_slot)
        pa_hook_slot_free(u->put_slot);
    if (u->unlink_slot)
        pa_hook_slot_free(u->unlink_slot);
    if (u->null_module != PA_INVALID_INDEX && m->core->state != PA_CORE_SHUTDOWN)
        pa_module_unload_request_by_index(m->core, u->null_module, TRUE);

    pa_xfree(u->sink_name);
    pa_xfree(u);
}
