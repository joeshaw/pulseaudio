/* $Id$ */

/***
  This file is part of PulseAudio.
 
  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2 of the License,
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

#include <pulsecore/core.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/namereg.h>

#include "module-rescue-streams-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering")
PA_MODULE_DESCRIPTION("When a sink/source is removed, try to move their streams to the default sink/source")
PA_MODULE_VERSION(PACKAGE_VERSION)

static const char* const valid_modargs[] = {
    NULL,
};

struct userdata {
    pa_hook_slot *sink_slot, *source_slot;
};

static pa_hook_result_t sink_hook_callback(pa_core *c, pa_sink *sink, void* userdata) {
    pa_sink_input *i;
    pa_sink *target;
    
    assert(c);
    assert(sink);

    if (!pa_idxset_size(sink->inputs)) {
        pa_log_debug(__FILE__": No sink inputs to move away.");
        return PA_HOOK_OK;
    }
    
    if (!(target = pa_namereg_get(c, NULL, PA_NAMEREG_SINK, 0))) {
        pa_log_info(__FILE__": No evacuation sink found.");
        return PA_HOOK_OK;
    }

    assert(target != sink);

    while ((i = pa_idxset_first(sink->inputs, NULL))) {
        if (pa_sink_input_move_to(i, target, 1) < 0) {
            pa_log_warn(__FILE__": Failed to move sink input %u \"%s\" to %s.", i->index, i->name, target->name);
            return PA_HOOK_OK;
        }

        pa_log_info(__FILE__": Sucessfully moved sink input %u \"%s\" to %s.", i->index, i->name, target->name);
    }

    
    return PA_HOOK_OK;
}

static pa_hook_result_t source_hook_callback(pa_core *c, pa_source *source, void* userdata) {
    pa_source_output *o;
    pa_source *target;
    
    assert(c);
    assert(source);

    if (!pa_idxset_size(source->outputs)) {
        pa_log_debug(__FILE__": No source outputs to move away.");
        return PA_HOOK_OK;
    }
    
    if (!(target = pa_namereg_get(c, NULL, PA_NAMEREG_SOURCE, 0))) {
        pa_log_info(__FILE__": No evacuation source found.");
        return PA_HOOK_OK;
    }

    assert(target != source);

    while ((o = pa_idxset_first(source->outputs, NULL))) {
        if (pa_source_output_move_to(o, target) < 0) {
            pa_log_warn(__FILE__": Failed to move source output %u \"%s\" to %s.", o->index, o->name, target->name);
            return PA_HOOK_OK;
        }

        pa_log_info(__FILE__": Sucessfully moved source output %u \"%s\" to %s.", o->index, o->name, target->name);
    }

    
    return PA_HOOK_OK;
}

int pa__init(pa_core *c, pa_module*m) {
    pa_modargs *ma = NULL;
    struct userdata *u;
    
    assert(c);
    assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log(__FILE__": Failed to parse module arguments");
        return -1;
    }

    m->userdata = u = pa_xnew(struct userdata, 1);
    u->sink_slot = pa_hook_connect(&c->hook_sink_disconnect, (pa_hook_cb_t) sink_hook_callback, NULL);
    u->source_slot = pa_hook_connect(&c->hook_source_disconnect, (pa_hook_cb_t) source_hook_callback, NULL);

    pa_modargs_free(ma);
    return 0;
}

void pa__done(pa_core *c, pa_module*m) {
    struct userdata *u;
    
    assert(c);
    assert(m);

    if (!m->userdata)
        return;

    u = m->userdata;
    if (u->sink_slot)
        pa_hook_slot_free(u->sink_slot);
    if (u->source_slot)
        pa_hook_slot_free(u->source_slot);

    pa_xfree(u);
}