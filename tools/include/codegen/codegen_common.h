#ifndef CODEGEN_COMMON_H
#define CODEGEN_COMMON_H

#include <stdio.h>
#include <stdbool.h>
#include "codegen/codegen.h"
#include "datastructures/dynamic_array.h"

typedef struct {
    bool uses_int;
    bool uses_bool;
    bool uses_float;
    bool uses_size_t;
    bool uses_ipv4;
    bool uses_strlen;
    bool uses_access;
    bool uses_getenv;
    bool uses_enum;
    bool uses_array;        /* any non-struct array */
    bool uses_array_int;
    bool uses_array_float;
    bool uses_array_bool;
    bool uses_array_ipv4;
    bool uses_array_string; /* string, path and any other element type */
    DynArray *regex_patterns; /* char* */
} UsageTracker;

/* Shared helper functions */
const char* get_str(InternResult *res);
bool is_schema_name(CodegenContext *ctx, const char *name);
AstNode* get_schema_by_name(CodegenContext *ctx, const char *name);
const char* map_primitive_to_c(const char* name);
/* Writes a string literal from the schema as a quoted, escaped C string literal.
 * An empty or missing literal is written as "". */
void emit_c_string(FILE *f, InternResult *literal);

/* True for required/required_if fields whose C value cannot represent "unset"
 * (int, float, bool, ipv4, enum). Their presence is tracked in a generated
 * `_present` member of the enclosing struct. */
bool field_needs_presence(AstNode *field);
/* Emits `<prefix>_present.<field> = true;` if the field's presence is tracked. */
void emit_mark_present(FILE *f, const char *indent, AstNode *field, const char *prefix);

void track_usage(AstNode *node, UsageTracker *tracker);
void emit_section_or_schema(CodegenContext *ctx, FILE *f, AstNode *node, const char* name);
void emit_validation_prototypes(FILE *f, AstNode *node, const char *name);
void emit_validation_function(UsageTracker *tracker, CodegenContext *ctx, FILE *f, AstNode *node, const char *name);
void emit_print_function(CodegenContext *ctx, FILE *f, AstNode *schema, const char *schema_name);
void emit_recursive_ini_handler(CodegenContext *ctx, FILE *f, AstNode *schema, const char *schema_name);
void emit_default_initialization_recursive(CodegenContext *ctx, FILE *f, AstNode *node, const char *prefix, const char *ctx_name);
void emit_env_overrides_recursive(CodegenContext *ctx, UsageTracker *tracker, FILE *f, AstNode *node, const char *prefix, const char *ctx_name);
void emit_cli_parser_recursive(CodegenContext *ctx, FILE *f, AstNode *node, const char *prefix, const char *path_prefix, const char *parent_name);

/* Emits a checked conversion of the C string expression `val` into the scalar
 * lvalue `lhs`. On failure the generated code records an error through
 * `ctx_expr` (a cfg_common_context_t*). `origin` is a C expression naming the
 * value's source for the message, e.g. "\"env PORT\"", or "NULL". */
/* Emits parsing of the comma-separated list `val` into the array lvalue `lhs`
 * (a { T *data; size_t count; } struct) using the element type's parser. */
void emit_array_parse(FILE *f, const char *indent, AstNode *elem_type, const char *lhs,
                      const char *val, const char *field, const char *ctx_expr, const char *origin);
/* Emits matching of `val` against the members of `enum_type`, assigning
 * <enum_prefix>_<member> to `lhs`, or recording an error if nothing matches. */
void emit_enum_parse(FILE *f, const char *indent, AstType *enum_type, const char *lhs, const char *enum_prefix,
                     const char *val, const char *field, const char *ctx_expr, const char *origin);
void emit_scalar_parse(FILE *f, const char *indent, const char *tname, const char *lhs,
                       const char *val, const char *field, const char *ctx_expr, const char *origin);

#endif /* CODEGEN_COMMON_H */
