/* $Id$ */

/***
  This file is part of PulseAudio.
 
  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.
 
  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#include <pulse/utf8.h>
#include <pulse/xmalloc.h>

#include <pulsecore/core-util.h>

#include "log.h"

#define ENV_LOGLEVEL "POLYP_LOG"

static char *log_ident = NULL, *log_ident_local = NULL;
static pa_log_target_t log_target = PA_LOG_STDERR;
static void (*user_log_func)(pa_log_level_t l, const char *s) = NULL;
static pa_log_level_t maximal_level = PA_LOG_NOTICE;

#ifdef HAVE_SYSLOG_H
static const int level_to_syslog[] = {
    [PA_LOG_ERROR] = LOG_ERR,
    [PA_LOG_WARN] = LOG_WARNING,
    [PA_LOG_NOTICE] = LOG_NOTICE,
    [PA_LOG_INFO] = LOG_INFO,
    [PA_LOG_DEBUG] = LOG_DEBUG
};
#endif

void pa_log_set_ident(const char *p) {
    if (log_ident)
        pa_xfree(log_ident);
    if (log_ident_local)
        pa_xfree(log_ident_local);

    log_ident = pa_xstrdup(p);
    log_ident_local = pa_utf8_to_locale(log_ident);
    if (!log_ident_local)
        log_ident_local = pa_xstrdup(log_ident);
}

void pa_log_set_maximal_level(pa_log_level_t l) {
    assert(l < PA_LOG_LEVEL_MAX);
    maximal_level = l;
}

void pa_log_set_target(pa_log_target_t t, void (*func)(pa_log_level_t l, const char*s)) {
    assert(t == PA_LOG_USER || !func);
    log_target = t;
    user_log_func = func;
}

void pa_log_levelv(pa_log_level_t level, const char *format, va_list ap) {
    const char *e;
    char *text, *t, *n;
    
    assert(level < PA_LOG_LEVEL_MAX);

    if ((e = getenv(ENV_LOGLEVEL)))
        maximal_level = atoi(e);
    
    if (level > maximal_level)
        return;

    text = pa_vsprintf_malloc(format, ap);

    if (!pa_utf8_valid(text))
        pa_log_level(level, __FILE__": invalid UTF-8 string following below:");

    for (t = text; t; t = n) {
        if ((n = strchr(t, '\n'))) {
            *n = 0;
            n++;
        }

        if (!*t)
            continue;
    
        switch (log_target) {
            case PA_LOG_STDERR: {
                const char *prefix = "", *suffix = "";
                char *local_t;

#ifndef OS_IS_WIN32                
                /* Yes indeed. Useless, but fun! */
                if (isatty(STDERR_FILENO)) {
                    if (level <= PA_LOG_ERROR) {
                        prefix = "\x1B[1;31m";
                        suffix = "\x1B[0m";
                    } else if (level <= PA_LOG_WARN) {
                        prefix = "\x1B[1m";
                        suffix = "\x1B[0m";
                    }
                }
#endif

                local_t = pa_utf8_to_locale(t);
                if (!local_t)
                    fprintf(stderr, "%s%s%s\n", prefix, t, suffix);
                else {
                    fprintf(stderr, "%s%s%s\n", prefix, local_t, suffix);
                    pa_xfree(local_t);
                }

                break;
            }
                
#ifdef HAVE_SYSLOG_H            
            case PA_LOG_SYSLOG: {
                char *local_t;

                openlog(log_ident_local ? log_ident_local : "???", LOG_PID, LOG_USER);

                local_t = pa_utf8_to_locale(t);
                if (!local_t)
                    syslog(level_to_syslog[level], "%s", t);
                else {
                    syslog(level_to_syslog[level], "%s", local_t);
                    pa_xfree(local_t);
                }

                closelog();
                break;            
            }
#endif
                
            case PA_LOG_USER: 
                user_log_func(level, t);
                break;
                
            case PA_LOG_NULL:
            default:
                break;
        }
    }

    pa_xfree(text);

}

void pa_log_level(pa_log_level_t level, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    pa_log_levelv(level, format, ap);
    va_end(ap);
}

void pa_log_debug(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    pa_log_levelv(PA_LOG_DEBUG, format, ap);
    va_end(ap);
}

void pa_log_info(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    pa_log_levelv(PA_LOG_INFO, format, ap);
    va_end(ap);
}

void pa_log_notice(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    pa_log_levelv(PA_LOG_INFO, format, ap);
    va_end(ap);
}

void pa_log_warn(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    pa_log_levelv(PA_LOG_WARN, format, ap);
    va_end(ap);
}

void pa_log_error(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    pa_log_levelv(PA_LOG_ERROR, format, ap);
    va_end(ap);
}