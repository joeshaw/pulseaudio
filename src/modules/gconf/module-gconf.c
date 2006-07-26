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

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <pulsecore/module.h>
#include <pulsecore/core.h>
#include <pulsecore/llist.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulse/mainloop-api.h>
#include <pulse/xmalloc.h>
#include <pulsecore/core-error.h>

#include "module-gconf-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering")
PA_MODULE_DESCRIPTION("GConf Adapter")
PA_MODULE_VERSION(PACKAGE_VERSION)
PA_MODULE_USAGE("")

#define MAX_MODULES 10
#define BUF_MAX 2048

#undef PA_GCONF_HELPER
#define PA_GCONF_HELPER "/home/lennart/projects/pulseaudio/src/gconf-helper"

struct module_info {
    char *name;
    
    unsigned n_indexes;
    uint32_t indexes[MAX_MODULES];
};

struct userdata {
    pa_core *core;
    pa_module *module;
    
    pa_hashmap *module_infos;

    pid_t pid;

    int fd;
    int fd_type;
    pa_io_event *io_event;

    char buf[BUF_MAX];
    size_t buf_fill;
};

static int fill_buf(struct userdata *u) {
    ssize_t r;
    assert(u);

    if (u->buf_fill >= BUF_MAX) {
        pa_log(__FILE__": read buffer overflow");
        return -1;
    }

    if ((r = pa_read(u->fd, u->buf + u->buf_fill, BUF_MAX - u->buf_fill, &u->fd_type)) <= 0)
        return -1;

    u->buf_fill += r;
    return 0;
}

static int read_byte(struct userdata *u) {
    int ret;
    assert(u);

    if (u->buf_fill < 1)
        if (fill_buf(u) < 0)
            return -1;

    ret = u->buf[0];
    assert(u->buf_fill > 0);
    u->buf_fill--;
    memmove(u->buf, u->buf+1, u->buf_fill);
    return ret;
}

static char *read_string(struct userdata *u) {
    assert(u);

    for (;;) {
        char *e;
        
        if ((e = memchr(u->buf, 0, u->buf_fill))) {
            char *ret = pa_xstrdup(u->buf);
            u->buf_fill -= e - u->buf +1;
            memmove(u->buf, e+1, u->buf_fill);
            return ret;
        }

        if (fill_buf(u) < 0)
            return NULL;
    }
}

static void unload_modules(struct userdata *u, struct module_info*m) {
    unsigned i;
    
    assert(u);
    assert(m);

    for (i = 0; i < m->n_indexes; i++) {
        pa_log_debug(__FILE__": Unloading module #%i", m->indexes[i]);
        pa_module_unload_by_index(u->core, m->indexes[i]);
    }

    m->n_indexes = 0;
}

static void load_module(
        struct userdata *u,
        struct module_info *m,
        const char *module,
        const char *args) {

    pa_module *mod;
    
    assert(u);
    assert(m);
    assert(module);

    assert(m->n_indexes < MAX_MODULES);

    pa_log_debug(__FILE__": Loading module '%s' with args '%s' due to GConf configuration.", module, args);
    
    if (!(mod = pa_module_load(u->core, module, args))) {
        pa_log(__FILE__": pa_module_load() failed");
        return;
    }
    
    m->indexes[m->n_indexes++] = mod->index;
}

static void module_info_free(void *p, void *userdata) {
    struct module_info *m = p;
    struct userdata *u = userdata;

    assert(m);
    assert(u);

    unload_modules(u, m);
    pa_xfree(m->name);
    pa_xfree(m);
}

static int handle_event(struct userdata *u) {
    int opcode;
    int ret = 0;

    do {
        if ((opcode = read_byte(u)) < 0)
            goto fail;
        
        switch (opcode) {
            case '!':
                /* The helper tool is now initialized */
                ret = 1;
                break;
                
            case '+': {
                char *name;
                struct module_info *m;
                
                if (!(name = read_string(u)))
                    goto fail;

                if ((m = pa_hashmap_get(u->module_infos, name))) {
                    unload_modules(u, m);
                } else {
                    m = pa_xnew(struct module_info, 1);
                    m->name = pa_xstrdup(name);
                    m->n_indexes = 0;
                    pa_hashmap_put(u->module_infos, m->name, m);
                }
                
                while (m->n_indexes < MAX_MODULES) {
                    char *module, *args;

                    if (!(module = read_string(u))) {
                        pa_xfree(name);
                        goto fail;
                    }

                    if (!*module) {
                        pa_xfree(module);
                        break;
                    }

                    if (!(args = read_string(u))) {
                        pa_xfree(name);
                        pa_xfree(module);
                        goto fail;
                    }

                    load_module(u, m, module, args);

                    pa_xfree(module);
                    pa_xfree(args);
                }

                pa_xfree(name);
                
                break;
            }
                
            case '-': {
                char *name;
                struct module_info *m;
                
                if (!(name = read_string(u)))
                    goto fail;

                if ((m = pa_hashmap_get(u->module_infos, name))) {
                    pa_hashmap_remove(u->module_infos, name);
                    module_info_free(m, u);
                }

                pa_xfree(name);
                
                break;
            }
        }
    } while (u->buf_fill > 0 && ret == 0);

    return ret;

fail:
    pa_log(__FILE__": Unable to read or parse data from client.");
    return -1;
}

static void io_event_cb(
        pa_mainloop_api*a,
        pa_io_event* e,
        int fd,
        pa_io_event_flags_t events,
        void *userdata) {

    struct userdata *u = userdata;

    handle_event(u);
}

static int start_client(const char *n, pid_t *pid) {
    pid_t child;
    int pipe_fds[2] = { -1, -1 };

    if (pipe(pipe_fds) < 0) {
        pa_log(__FILE__": pipe() failed: %s", pa_cstrerror(errno));
        goto fail;
    }
    
    if ((child = fork()) == (pid_t) -1) {
        pa_log(__FILE__": fork() failed: %s", pa_cstrerror(errno));
        goto fail;
    } else if (child != 0) {

        /* Parent */
        close(pipe_fds[1]);

        if (pid)
            *pid = child;

        return pipe_fds[0];
    } else {

        /* child */

        close(pipe_fds[0]);
        dup2(pipe_fds[1], 1);

        if (pipe_fds[1] != 1)
            close(pipe_fds[1]);

        execl(n, n, NULL);
        _exit(1);
    }
    
fail:
    if (pipe_fds[0] >= 0)
        close(pipe_fds[0]);

    if (pipe_fds[1] >= 0)
        close(pipe_fds[1]);
    
    return -1;
}

int pa__init(pa_core *c, pa_module*m) {
    struct userdata *u;
    int r;

    u = pa_xnew(struct userdata, 1);
    u->core = c;
    u->module = m;
    u->module_infos = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    u->pid = (pid_t) -1;
    u->fd = -1;
    u->fd_type = 0;
    u->io_event = NULL;
    u->buf_fill = 0;
    
    if ((u->fd = start_client(PA_GCONF_HELPER, &u->pid)) < 0)
        goto fail;
    
    u->io_event = c->mainloop->io_new(
            c->mainloop,
            u->fd,
            PA_IO_EVENT_INPUT,
            io_event_cb,
            u);
    
    do {
        if ((r = handle_event(u)) < 0)
            goto fail;

        /* Read until the client signalled us that it is ready with
         * initialization */
    } while (r != 1);
        
    return 0;

fail:
    pa__done(c, m);
    return -1;
}

void pa__done(pa_core *c, pa_module*m) {
    struct userdata *u;

    assert(c);
    assert(m);

    if (!(u = m->userdata))
        return;

    if (u->io_event)
        c->mainloop->io_free(u->io_event);

    if (u->fd >= 0)
        close(u->fd);

    if (u->pid != (pid_t) -1) {
        kill(u->pid, SIGTERM);
        waitpid(u->pid, NULL, 0);
    }

    if (u->module_infos)
        pa_hashmap_free(u->module_infos, module_info_free, u);

    pa_xfree(u);
}
