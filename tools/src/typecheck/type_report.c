#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "typecheck/type_report.h"
#include "parser/ast.h"
#include "datastructures/utils.h"
#include "file.h"

void report_type_error(Arena *arena, DynArray *errors, TypeErrorKind kind, Span span, const char *filename, ...) {
    TypeError *err = arena_alloc(arena, sizeof(TypeError)); 
    err->kind = kind;
    err->span = span;
    err->filename = filename;

    va_list args;
    va_start(args, filename);
    if (kind == TE_UNKNOWN_TYPE || kind == TE_REDECLARATION || kind == TE_UNDECLARED || kind == TE_CIRCULAR_DEPENDENCY) {
        err->as.name.name = va_arg(args, InternResult*);
    } else if (kind == TE_TYPE_MISMATCH || kind == TE_INVALID_FORMAT || kind == TE_INVALID_RANGE || kind == TE_INVALID_LENGTH) {
        err->as.mismatch.expected = va_arg(args, Type*);
        err->as.mismatch.actual = va_arg(args, Type*);
        err->as.mismatch.expected_format = va_arg(args, const char*);
    } else if (kind == TE_UNEXPECTED_PROPERTY) {
        err->as.property.name = va_arg(args, InternResult*);
    }
    va_end(args);

    dynarray_push_ptr(errors, err);
}

void print_type_errors(DynArray *errors, DenseInterner *identifiers) {
    for (size_t i = 0; i < errors->count; i++) {
        TypeError *err = *(TypeError**)dynarray_get(errors, i);
        
        /* Style matching parser: error: <msg> \n   file:line:col */
        fprintf(stderr, COLOR_RED "error:" COLOR_RESET " ");

        switch (err->kind) {
            case TE_UNKNOWN_TYPE:
                fprintf(stderr, "unknown type '%.*s'\n", 
                        (int)((Slice*)err->as.name.name->key)->len,
                        ((Slice*)err->as.name.name->key)->ptr);
                break;
            case TE_REDECLARATION:
                fprintf(stderr, "redeclaration of '%.*s'\n", 
                        (int)((Slice*)err->as.name.name->key)->len,
                        ((Slice*)err->as.name.name->key)->ptr);
                break;
            case TE_UNDECLARED:
                fprintf(stderr, "use of undeclared identifier '%.*s'\n", 
                        (int)((Slice*)err->as.name.name->key)->len,
                        ((Slice*)err->as.name.name->key)->ptr);
                break;
            case TE_TYPE_MISMATCH:
                fprintf(stderr, "type mismatch");
                if (err->as.mismatch.expected && err->as.mismatch.actual) {
                    fprintf(stderr, ": expected %s, got %s", 
                            type_to_string(err->as.mismatch.expected),
                            type_to_string(err->as.mismatch.actual));
                } else if (err->as.mismatch.expected_format) {
                    fprintf(stderr, ": %s", err->as.mismatch.expected_format);
                } else {
                    fprintf(stderr, ": value does not match expected format");
                }
                fprintf(stderr, "\n");
                break;
            case TE_EXPECTED_ARRAY:
                fprintf(stderr, "expected array type\n");
                break;
            case TE_UNEXPECTED_PROPERTY:
                fprintf(stderr, "property '%.*s' is not valid for this type\n",
                        (int)((Slice*)err->as.property.name->key)->len,
                        ((Slice*)err->as.property.name->key)->ptr);
                break;
            case TE_MISSING_REQUIRED_PROPERTY:
                fprintf(stderr, "missing required property\n");
                break;
            case TE_CIRCULAR_DEPENDENCY:
                fprintf(stderr, "circular dependency detected in schema '%.*s'\n",
                        (int)((Slice*)err->as.name.name->key)->len,
                        ((Slice*)err->as.name.name->key)->ptr);
                break;
            case TE_INVALID_FORMAT:
                fprintf(stderr, "invalid format: %s\n", err->as.mismatch.expected_format);
                break;
            case TE_INVALID_RANGE:
                fprintf(stderr, "invalid range: %s\n", err->as.mismatch.expected_format);
                break;
            case TE_INVALID_LENGTH:
                fprintf(stderr, "invalid length: %s\n", err->as.mismatch.expected_format);
                break;
        }

        fprintf(stderr, "   %s:%zu:%zu\n\n",
                err->filename,
                err->span.start_line,
                err->span.start_col);
    }
}
