/* $Id$ */

/***
  This file is part of PulseAudio.
 
  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.
 
  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include <pulse/xmalloc.h>

#include "dynarray.h"

/* If the array becomes to small, increase its size by 100 entries */
#define INCREASE_BY 100

struct pa_dynarray {
    void **data;
    unsigned n_allocated, n_entries;
};

pa_dynarray* pa_dynarray_new(void) {
    pa_dynarray *a;
    a = pa_xnew(pa_dynarray, 1);
    a->data = NULL;
    a->n_entries = 0;
    a->n_allocated = 0;
    return a;
}

void pa_dynarray_free(pa_dynarray* a, void (*func)(void *p, void *userdata), void *userdata) {
    unsigned i;
    assert(a);

    if (func)
        for (i = 0; i < a->n_entries; i++)
            if (a->data[i])
                func(a->data[i], userdata);

    pa_xfree(a->data);
    pa_xfree(a);
}

void pa_dynarray_put(pa_dynarray*a, unsigned i, void *p) {
    assert(a);

    if (i >= a->n_allocated) {
        unsigned n;

        if (!p)
            return;

        n = i+INCREASE_BY;
        a->data = pa_xrealloc(a->data, sizeof(void*)*n);
        memset(a->data+a->n_allocated, 0, sizeof(void*)*(n-a->n_allocated));
        a->n_allocated = n;
    }

    a->data[i] = p;

    if (i >= a->n_entries)
        a->n_entries = i+1;
}

unsigned pa_dynarray_append(pa_dynarray*a, void *p) {
    unsigned i = a->n_entries;
    pa_dynarray_put(a, i, p);
    return i;
}

void *pa_dynarray_get(pa_dynarray*a, unsigned i) {
    assert(a);
    if (i >= a->n_allocated)
        return NULL;

    assert(a->data);
    return a->data[i];
}

unsigned pa_dynarray_size(pa_dynarray*a) {
    assert(a);
    return a->n_entries;
}