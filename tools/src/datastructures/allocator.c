#include "datastructures/allocator.h"
#include <stdlib.h>

static void* heap_realloc(void *context, void *ptr, size_t old_size, size_t new_size) {
    (void)context;
    (void)old_size;
    
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

Allocator heap_allocator_create(void) {
    Allocator alloc;
    alloc.realloc = heap_realloc;
    alloc.context = NULL;
    return alloc;
}