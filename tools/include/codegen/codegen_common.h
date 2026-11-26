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
    DynArray *regex_patterns; /* char* */
} UsageTracker;

/* Shared helper functions */
const char* get_str(InternResult *res);
bool is_schema_name(CodegenContext *ctx, const char *name);
AstNode* get_schema_by_name(CodegenContext *ctx, const char *name);
const char* map_primitive_to_c(const char* name);

void track_usage(AstNode *node, UsageTracker *tracker);
void emit_section_or_schema(CodegenContext *ctx, FILE *f, AstNode *node, const char* name);
void emit_validation_prototypes(FILE *f, AstNode *node, const char *name);
void emit_validation_function(UsageTracker *tracker, CodegenContext *ctx, FILE *f, AstNode *node, const char *name);
void emit_print_function(CodegenContext *ctx, FILE *f, AstNode *schema, const char *schema_name);
void emit_recursive_ini_handler(CodegenContext *ctx, FILE *f, AstNode *schema, const char *schema_name);
void emit_default_initialization_recursive(CodegenContext *ctx, FILE *f, AstNode *node, const char *prefix, const char *ctx_name);
void emit_env_overrides_recursive(CodegenContext *ctx, UsageTracker *tracker, FILE *f, AstNode *node, const char *prefix, const char *ctx_name);
void emit_cli_parser_recursive(CodegenContext *ctx, FILE *f, AstNode *node, const char *prefix, const char *path_prefix, const char *parent_name);
void emit_regex_validator(FILE *f, const char *re_str, int idx);

/* Emits a checked conversion of the C string expression `val` into the scalar
 * lvalue `lhs`. On failure the generated code records an error through
 * `ctx_expr` (a cfg_common_context_t*). `origin` is a C expression naming the
 * value's source for the message, e.g. "\"env PORT\"", or "NULL". */
void emit_scalar_parse(FILE *f, const char *indent, const char *tname, const char *lhs,
                       const char *val, const char *field, const char *ctx_expr, const char *origin);

#endif /* CODEGEN_COMMON_H */
