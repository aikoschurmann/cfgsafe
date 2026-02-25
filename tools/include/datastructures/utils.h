#pragma once
#include <stdint.h>  // for uint32_t
#include <string.h>  // for memcmp


typedef struct {
    size_t start_line;
    size_t start_col;
    size_t end_line;
    size_t end_col;
} Span;

// ------------------------------
// A slice: view into the source buffer
// ------------------------------
typedef struct {
    const char* ptr;  // pointer into source buffer
    uint32_t len;     // length of the slice
} Slice;

static size_t slice_hash(void *p) {
    Slice *s = (Slice*)p;
    size_t h = (size_t)1469598103934665603ULL; /* FNV-1a 64-bit */
    for (size_t i = 0; i < s->len; ++i) {
        h ^= (unsigned char)s->ptr[i];
        h *= (size_t)1099511628211ULL;
    }
    return h;
}

static int slice_cmp(void *a, void *b) {
    Slice *sa = (Slice*)a;
    Slice *sb = (Slice*)b;
    if (sa->len != sb->len) return (sa->len < sb->len) ? -1 : 1;
    return memcmp(sa->ptr, sb->ptr, sa->len);
}

static inline Span span_join(const Span *a, const Span *b) {
    if (!a || !b) return (Span){0,0,0,0};
    return (Span){a->start_line, a->start_col, b->end_line, b->end_col};
}

static size_t ptr_identity_hash(void *p) {
    // Hash the pointer value itself
    return (size_t)p;
}

static int ptr_identity_cmp(void *a, void *b) {
    // Compare pointer identity
    return (a > b) - (a < b);
}

double now_seconds(void);
size_t get_peak_rss_kb(void);

/* ANSI Colors */
#define COLOR_RESET   "\x1b[0m"
#define COLOR_BOLD    "\x1b[1m"
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_GRAY    "\x1b[90m"