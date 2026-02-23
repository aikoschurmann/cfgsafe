#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datastructures/arena.h"
#include "datastructures/allocator.h"
#include "datastructures/dynamic_array.h"
#include "datastructures/hash_map.h"
#include "datastructures/dense_interner.h"
#include "datastructures/utils.h"

int main(int argc, char const *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file.schema> [extra compiler flags...]\n", argv[0]);
        return 1;
    }
    
    Arena *lex_arena = arena_create(4048);
    Allocator alloc = arena_allocator_create(lex_arena);
    printf("[1] Arena & Allocator initialized successfully!\n");

   
    DynArray arr;
    dynarray_init(&arr, sizeof(int), alloc);
    
    for (int i = 0; i < 5; i++) {
        int val = i * 10; // 0, 10, 20, 30, 40
        dynarray_push_value(&arr, &val);
    }
    
    int *arr_res = dynarray_get(&arr, 3);
    printf("[2] DynArray Test: Item at index 3 is %d (Expected: 30)\n", arr_res ? *arr_res : -1);

   
    HashMap *map = hashmap_create(16, alloc);
    
    int key1 = 100, val1 = 999;
    int key2 = 200, val2 = 888;
    
    hashmap_put(map, &key1, &val1, ptr_identity_hash, ptr_identity_cmp);
    hashmap_put(map, &key2, &val2, ptr_identity_hash, ptr_identity_cmp);
    
    int *map_res = hashmap_get(map, &key1, ptr_identity_hash, ptr_identity_cmp);
    printf("[3] HashMap Test: Value for key1 is %d (Expected: 999)\n", map_res ? *map_res : -1);

    
    HashMap *intern_map = hashmap_create(32, alloc);
    
    DenseInterner *interner = intern_table_create(intern_map, alloc, string_copy_func, slice_hash, slice_cmp);
    
    Slice s1 = {"hello", 5};
    Slice s2 = {"world", 5};
    Slice s3 = {"hello", 5}; // Testing same index

    int idx1 = intern_idx(interner, &s1, NULL);
    int idx2 = intern_idx(interner, &s2, NULL);
    int idx3 = intern_idx(interner, &s3, NULL);

    printf("[4] Interner Test:\n");
    printf("    'hello' interned at index %d\n", idx1);
    printf("    'world' interned at index %d\n", idx2);
    printf("    'hello' (duplicate) interned at index %d (Expected: %d)\n", idx3, idx1);
    
    printf("    Retrieve index 0: '%s'\n", interner_get_cstr(interner, 0));
    printf("    Retrieve index 1: '%s'\n", interner_get_cstr(interner, 1));

    
    printf("\n[5] Arena Stats before destruction:\n");
    printf("    Total bytes used: %zu\n", arena_bytes_used(lex_arena));
    printf("    Number of blocks: %zu\n", arena_block_count(lex_arena));

    arena_destroy(lex_arena);
    printf("\nSuccess! Everything worked and memory is cleaned up.\n");

    return 0;    
}