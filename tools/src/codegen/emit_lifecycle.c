#include <string.h>
#include "codegen/codegen_common.h"

static void emit_default_initialization(FILE *f, AstNode *field, const char *prefix, const char *parent_name, const char *ctx_name) {
    InternResult *name = field->data.field_decl.name;
    DynArray *props = field->data.field_decl.properties;
    AstNode *default_node = NULL;
    for (size_t i = 0; i < props->count; i++) {
        AstNode *pnode = *(AstNode**)dynarray_get(props, i);
        if (strcmp(get_str(pnode->data.property_decl.name), "default") == 0) {
            default_node = pnode->data.property_decl.value;
            break;
        }
    }

    if (default_node) {
        AstType *type = &field->data.field_decl.type->data.ast_type;
        const char *fname = get_str(name);
        
        if (type->kind == AST_TYPE_PRIMITIVE && strcmp(get_str(type->u.primitive.name), "ipv4") == 0) {
            fprintf(f, "    cfg_parse_ipv4(\"%s\", &%s%s);\n", get_str(default_node->data.literal.value.string_val), prefix, fname);
        } else {
            fprintf(f, "    %s%s = ", prefix, fname);
            switch (default_node->data.literal.type) {
                case INT_LITERAL: fprintf(f, "%lld;\n", default_node->data.literal.value.int_val); break;
                case FLOAT_LITERAL: fprintf(f, "%f;\n", default_node->data.literal.value.float_val); break;
                case BOOL_LITERAL: fprintf(f, "%s;\n", default_node->data.literal.value.bool_val ? "true" : "false"); break;
                case STRING_LITERAL: fprintf(f, "cfg_intern_string(%s, \"%s\");\n", ctx_name, get_str(default_node->data.literal.value.string_val)); break;
                case IDENTIFIER_LITERAL: {
                    fprintf(f, "%s_%s_%s;\n", parent_name, fname, get_str(default_node->data.literal.value.string_val)); 
                    break;
                }
                default: break;
            }
        }
    } else {
        AstType *type = &field->data.field_decl.type->data.ast_type;
        if (type->kind == AST_TYPE_PRIMITIVE) {
             const char* tname = get_str(type->u.primitive.name);
             if (strcmp(tname, "int") == 0 || strcmp(tname, "float") == 0) {
                 fprintf(f, "    %s%s = 0;\n", prefix, get_str(name));
             } else if (strcmp(tname, "bool") == 0) {
                 fprintf(f, "    %s%s = false;\n", prefix, get_str(name));
             }
        }
    }
}

static void emit_env_overrides(UsageTracker *tracker, FILE *f, AstNode *field, const char *prefix, const char *ctx_name) {
    if (!tracker->uses_getenv) return;
    InternResult *name = field->data.field_decl.name;
    DynArray *props = field->data.field_decl.properties;
    const char *env_var = NULL;
    for (size_t i = 0; i < props->count; i++) {
        AstNode *pnode = *(AstNode**)dynarray_get(props, i);
        if (strcmp(get_str(pnode->data.property_decl.name), "env") == 0) {
            env_var = get_str(pnode->data.property_decl.value->data.literal.value.string_val);
            break;
        }
    }

    if (env_var) {
        fprintf(f, "    { const char *e = getenv(\"%s\"); if (e) { ", env_var);
        AstType *type = &field->data.field_decl.type->data.ast_type;
        if (type->kind == AST_TYPE_PRIMITIVE) {
            const char *tname = get_str(type->u.primitive.name);
            if (strcmp(tname, "int") == 0) fprintf(f, "%s%s = strtoll(e, NULL, 10);", prefix, get_str(name));
            else if (strcmp(tname, "float") == 0) fprintf(f, "%s%s = strtod(e, NULL);", prefix, get_str(name));
            else if (strcmp(tname, "bool") == 0) fprintf(f, "%s%s = (strcmp(e, \"true\") == 0 || strcmp(e, \"1\") == 0);", prefix, get_str(name));
            else if (strcmp(tname, "string") == 0 || strcmp(tname, "path") == 0) fprintf(f, "%s%s = cfg_intern_string(%s, e);", prefix, get_str(name), ctx_name);
            else if (strcmp(tname, "ipv4") == 0) fprintf(f, "cfg_parse_ipv4(e, &%s%s);", prefix, get_str(name));
        }
        fprintf(f, " } }\n");
    }
}

void emit_default_initialization_recursive(CodegenContext *ctx, FILE *f, AstNode *node, const char *prefix, const char *ctx_name) {
    DynArray *items = (node->node_type == AST_SCHEMA_DECL) ? node->data.schema_decl.items : node->data.section_decl.items;
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            AstNode *type_node = item->data.field_decl.type;
            if (type_node->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                const char *tname = get_str(type_node->data.ast_type.u.primitive.name);
                if (is_schema_name(ctx, tname)) {
                    char new_prefix[256];
                    snprintf(new_prefix, sizeof(new_prefix), "%s%s.", prefix, get_str(item->data.field_decl.name));
                    AstNode *target_schema = get_schema_by_name(ctx, tname);
                    emit_default_initialization_recursive(ctx, f, target_schema, new_prefix, ctx_name);
                } else {
                    const char *parent_name = (node->node_type == AST_SCHEMA_DECL) ? get_str(node->data.schema_decl.name) : get_str(node->data.section_decl.name);
                    emit_default_initialization(f, item, prefix, parent_name, ctx_name);
                }
            } else {
                const char *parent_name = (node->node_type == AST_SCHEMA_DECL) ? get_str(node->data.schema_decl.name) : get_str(node->data.section_decl.name);
                emit_default_initialization(f, item, prefix, parent_name, ctx_name);
            }
        } else if (item->node_type == AST_SECTION_DECL) {
            const char *sec_name = get_str(item->data.section_decl.name);
            char new_prefix[256];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s.", prefix, sec_name);
            emit_default_initialization_recursive(ctx, f, item, new_prefix, ctx_name);
        }
    }
}

void emit_env_overrides_recursive(CodegenContext *ctx, UsageTracker *tracker, FILE *f, AstNode *node, const char *prefix, const char *ctx_name) {
    DynArray *items = (node->node_type == AST_SCHEMA_DECL) ? node->data.schema_decl.items : node->data.section_decl.items;
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            AstNode *type_node = item->data.field_decl.type;
            if (type_node->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                const char *tname = get_str(type_node->data.ast_type.u.primitive.name);
                if (is_schema_name(ctx, tname)) {
                    char new_prefix[256];
                    snprintf(new_prefix, sizeof(new_prefix), "%s%s.", prefix, get_str(item->data.field_decl.name));
                    AstNode *target_schema = get_schema_by_name(ctx, tname);
                    emit_env_overrides_recursive(ctx, tracker, f, target_schema, new_prefix, ctx_name);
                } else {
                    emit_env_overrides(tracker, f, item, prefix, ctx_name);
                }
            } else {
                emit_env_overrides(tracker, f, item, prefix, ctx_name);
            }
        } else if (item->node_type == AST_SECTION_DECL) {
            const char *sec_name = get_str(item->data.section_decl.name);
            char new_prefix[256];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s.", prefix, sec_name);
            emit_env_overrides_recursive(ctx, tracker, f, item, new_prefix, ctx_name);
        }
    }
}

static void emit_indent(FILE *f, int level) {
    for (int i = 0; i < level; i++) fprintf(f, "    ");
}

static void emit_ini_handler_body(CodegenContext *ctx, FILE *f, AstNode *node, const char *prefix, const char *parent_name, int target_depth) {
    DynArray *items = (node->node_type == AST_SCHEMA_DECL) 
        ? node->data.schema_decl.items 
        : node->data.section_decl.items;

    emit_indent(f, target_depth + 1); fprintf(f, "if (num_parts == %d) {\n", target_depth);
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            InternResult *name = item->data.field_decl.name;
            AstType *type = &item->data.field_decl.type->data.ast_type;
            const char *fname = get_str(name);

            emit_indent(f, target_depth + 2); fprintf(f, "if (strcmp(key, \"%s\") == 0) {\n", fname);
            if (type->kind == AST_TYPE_PRIMITIVE) {
                const char *tname = get_str(type->u.primitive.name);
                if (strcmp(tname, "int") == 0) { emit_indent(f, target_depth + 3); fprintf(f, "%s%s = strtoll(val, NULL, 10);\n", prefix, fname); }
                else if (strcmp(tname, "float") == 0) { emit_indent(f, target_depth + 3); fprintf(f, "%s%s = strtod(val, NULL);\n", prefix, fname); }
                else if (strcmp(tname, "bool") == 0) { emit_indent(f, target_depth + 3); fprintf(f, "%s%s = (strcmp(val, \"true\") == 0 || strcmp(val, \"1\") == 0);\n", prefix, fname); }
                else if (strcmp(tname, "string") == 0 || strcmp(tname, "path") == 0) { emit_indent(f, target_depth + 3); fprintf(f, "%s%s = cfg_intern_string(ctx, val);\n", prefix, fname); }
                else if (strcmp(tname, "ipv4") == 0) { emit_indent(f, target_depth + 3); fprintf(f, "cfg_parse_ipv4(val, &%s%s);\n", prefix, fname); }
            } else if (type->kind == AST_TYPE_ENUM) {
                DynArray *members = type->u.enum_type.enum_decl->data.enum_type.members;
                for (size_t k = 0; k < members->count; k++) {
                    InternResult *member = *(InternResult**)dynarray_get(members, k);
                    const char *mname = get_str(member);
                    emit_indent(f, target_depth + 3);
                    fprintf(f, "%sif (strcmp(val, \"%s\") == 0) %s%s = %s_%s_%s;\n", 
                        (k == 0 ? "" : "else "), mname, prefix, fname, parent_name, fname, mname);
                }
            } else if (type->kind == AST_TYPE_ARRAY) {
                emit_indent(f, target_depth + 3); fprintf(f, "cfg_parse_array(ctx, val, (void**)&%s%s.data, &%s%s.count);\n", prefix, fname, prefix, fname);
            }
            emit_indent(f, target_depth + 3); fprintf(f, "return;\n");
            emit_indent(f, target_depth + 2); fprintf(f, "}\n");
        }
    }
    emit_indent(f, target_depth + 1); fprintf(f, "}\n");

    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_SECTION_DECL) {
            const char *sec_name = get_str(item->data.section_decl.name);
            emit_indent(f, target_depth + 1);
            fprintf(f, "if (%d < num_parts && strcmp(parts[%d], \"%s\") == 0) {\n", target_depth, target_depth, sec_name);
            char new_prefix[256];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s.", prefix, sec_name);
            emit_ini_handler_body(ctx, f, item, new_prefix, sec_name, target_depth + 1);
            emit_indent(f, target_depth + 1); fprintf(f, "}\n");
        } else if (item->node_type == AST_FIELD_DECL) {
            AstNode *type_node = item->data.field_decl.type;
            if (type_node->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                const char *tname = get_str(type_node->data.ast_type.u.primitive.name);
                if (is_schema_name(ctx, tname)) {
                    const char *fname = get_str(item->data.field_decl.name);
                    emit_indent(f, target_depth + 1);
                    fprintf(f, "if (%d < num_parts && strcmp(parts[%d], \"%s\") == 0) {\n", target_depth, target_depth, fname);
                    char new_prefix[256];
                    snprintf(new_prefix, sizeof(new_prefix), "%s%s.", prefix, fname);
                    AstNode *target_schema = get_schema_by_name(ctx, tname);
                    emit_ini_handler_body(ctx, f, target_schema, new_prefix, tname, target_depth + 1);
                    emit_indent(f, target_depth + 1); fprintf(f, "}\n");
                }
            }
        }
    }
}

void emit_recursive_ini_handler(CodegenContext *ctx, FILE *f, AstNode *schema, const char *schema_name) {
    fprintf(f, "static void %s_ini_handler_recursive(cfg_common_context_t *ctx, const char *key, const char *val, char **parts, int num_parts, int depth) {\n", schema_name);
    char prefix[256]; snprintf(prefix, sizeof(prefix), "((%s_t*)ctx->cfg)->", schema_name);
    emit_ini_handler_body(ctx, f, schema, prefix, schema_name, 0);
    fprintf(f, "}\n\n");
}
