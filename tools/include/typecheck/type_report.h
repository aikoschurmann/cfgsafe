#pragma once

#include "lexer/lexer.h"
#include "typecheck/type.h"

typedef enum {
    TE_UNKNOWN_TYPE,
    TE_REDECLARATION,
    TE_UNDECLARED,
    TE_TYPE_MISMATCH,
    TE_EXPECTED_ARRAY,
    TE_UNEXPECTED_PROPERTY,
    TE_MISSING_REQUIRED_PROPERTY,
    TE_CIRCULAR_DEPENDENCY,
    TE_INVALID_FORMAT,
    TE_INVALID_RANGE,
    TE_INVALID_LENGTH
} TypeErrorKind;

typedef struct {
    TypeErrorKind kind;
    Span span;
    const char *filename;
    
    union {
        struct { InternResult *name; } name;
        struct {
            Type *expected;
            Type *actual;
            const char *expected_format; /* For non-type mismatches like Range vs Literal */
        } mismatch;
        struct { InternResult *name; } property;
    } as;
} TypeError;

void report_type_error(Arena *arena, DynArray *errors, TypeErrorKind kind, Span span, const char *filename, ...);
void print_type_errors(DynArray *errors, DenseInterner *identifiers);
