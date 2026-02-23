#include "datastructures/dynamic_array.h"
#include <string.h>

void dynarray_init(DynArray *da, size_t elem_size, Allocator alloc) {
    if (!da) return;
    da->data = NULL;
    da->elem_size = elem_size;
    da->count = 0;
    da->capacity = 0;
    da->alloc = alloc;
}

void dynarray_free(DynArray *da) {
    if (!da) return;
    if (da->data && da->alloc.realloc) {
        /* Calling realloc with new_size 0 triggers the free logic */
        da->alloc.realloc(da->alloc.context, da->data, da->capacity * da->elem_size, 0);
    }
    da->data = NULL;
    da->count = 0;
    da->capacity = 0;
    da->elem_size = 0;
    da->alloc.realloc = NULL;
    da->alloc.context = NULL;
}

int dynarray_reserve(DynArray *da, size_t min_capacity) {
    if (!da || !da->alloc.realloc) return -1;
    if (da->capacity >= min_capacity) return 0;

    size_t newcap = da->capacity ? da->capacity * 2 : 8;
    while (newcap < min_capacity) newcap *= 2;

    size_t old_size = da->capacity * da->elem_size;
    size_t new_size = newcap * da->elem_size;

    void *newbuf = da->alloc.realloc(da->alloc.context, da->data, old_size, new_size);
    if (!newbuf) return -1;
    
    da->data = newbuf;
    da->capacity = newcap;
    return 0;
}

int dynarray_push_value(DynArray *da, const void *value) {
    if (!da) return -1;
    if (dynarray_reserve(da, da->count + 1) != 0) return -1;
    void *dst = (char*)da->data + da->count * da->elem_size;
    da->count += 1;
    if (value) {
        memcpy(dst, value, da->elem_size);
    } else {
        memset(dst, 0, da->elem_size);
    }
    return 0;
}

void *dynarray_push_uninit(DynArray *da) {
    if (!da) return NULL;
    if (dynarray_reserve(da, da->count + 1) != 0) return NULL;
    void *slot = (char*)da->data + da->count * da->elem_size;
    da->count += 1;
    return slot;
}

void dynarray_pop(DynArray *da) {
    if (!da) return;
    if (da->count == 0) return;
    da->count -= 1;
}

void dynarray_remove(DynArray *da, size_t index) {
    if (!da) return;
    if (index >= da->count) return;
    if (index < da->count - 1) {
        void *dst = (char*)da->data + index * da->elem_size;
        void *src = (char*)da->data + (index + 1) * da->elem_size;
        size_t n = da->count - index - 1;
        memmove(dst, src, n * da->elem_size);
    }
    da->count -= 1;
}

void *dynarray_get(DynArray *da, size_t index) {
    if (!da) return NULL;
    if (index >= da->count) return NULL;
    return (char*)da->data + index * da->elem_size;
}

int dynarray_set(DynArray *da, size_t index, const void *value) {
    if (!da || !value) return -1;
    if (index >= da->count) return -1;
    void *dst = (char*)da->data + index * da->elem_size;
    memcpy(dst, value, da->elem_size);
    return 0;
}