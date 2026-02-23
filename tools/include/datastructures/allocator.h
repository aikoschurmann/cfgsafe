#pragma once
#include <stddef.h>
#include <string.h>

/* The minimal interface that allocators must implement */
typedef void* (*ReallocFunc)(void *allocator_context, void *ptr, size_t old_size, size_t new_size);

typedef struct {
    ReallocFunc realloc;
    void *context;
} Allocator;

/* Standard heap allocator initializer */
Allocator heap_allocator_create(void);


static inline void* allocator_alloc(Allocator alloc, size_t size) {
    if (!alloc.realloc) return NULL;
    return alloc.realloc(alloc.context, NULL, 0, size);
}

static inline void* allocator_calloc(Allocator alloc, size_t size) {
    void *ptr = allocator_alloc(alloc, size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

static inline void allocator_free(Allocator alloc, void *ptr, size_t old_size) {
    if (!alloc.realloc || !ptr) return;
    alloc.realloc(alloc.context, ptr, old_size, 0);
}

static inline void* allocator_realloc(Allocator alloc, void *ptr, size_t old_size, size_t new_size) {
    if (!alloc.realloc) return NULL;
    return alloc.realloc(alloc.context, ptr, old_size, new_size);
}