#pragma once
#include <stddef.h>

#include "allocator.h"

/* dynarray: generic vector of elements of fixed size. Memory management
 * is abstracted away behind the Allocator interface. */
typedef struct {
    void *data;         /* pointer to element buffer */
    size_t elem_size;   /* bytes per element */
    size_t count;       /* number of elements stored */
    size_t capacity;    /* capacity in elements */
    Allocator alloc;    /* allocator logic */
} DynArray;

/* Unified API */
void dynarray_init(DynArray *da, size_t elem_size, Allocator alloc);
void dynarray_free(DynArray *da);
int dynarray_reserve(DynArray *da, size_t min_capacity);
int dynarray_push_value(DynArray *da, const void *value);
void *dynarray_push_uninit(DynArray *da);
void dynarray_pop(DynArray *da);
void dynarray_remove(DynArray *da, size_t index);
void *dynarray_get(DynArray *da, size_t index);
int dynarray_set(DynArray *da, size_t index, const void *value);

/* Convenience: push pointer value as element (if elem_size == sizeof(void*)) */
static inline void dynarray_push_ptr(DynArray *da, void *p) {
    dynarray_push_value(da, &p);
}