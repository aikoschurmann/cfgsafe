#ifndef INTEGRATION_HOOKS_H
#define INTEGRATION_HOOKS_H

#include <stdbool.h>
#include <string.h>
#include <stdint.h>

static bool test_hook(const void *val, void *err_ptr) {
    const char *s = (const char *)val;
    if (s && strcmp(s, "hook-fail") == 0) {
        return false;
    }
    return true;
}

static bool test_array_hook(const void *val, void *err_ptr) {
    struct { void *data; size_t count; } *arr = (void*)val;
    if (arr->count != 3) {
        return false;
    }
    return true;
}

static bool test_nested_hook(const void *val, void *err_ptr) {
    int64_t id = *(int64_t*)val;
    if (id == 0) {
        return false;
    }
    return true;
}

#endif
