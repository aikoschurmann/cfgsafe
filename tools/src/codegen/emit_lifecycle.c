#include <string.h>
#include "codegen/codegen_common.h"

void emit_array_parse(FILE *f, const char *indent, AstNode *elem_type, const char *lhs,
                      const char *val, const char *field, const char *ctx_expr, const char *origin) {
    const char *fn = "cfg_elem_string", *kind = "string";
    AstType *elem = &elem_type->data.ast_type;
    if (elem->kind == AST_TYPE_PRIMITIVE) {
        const char *ename = get_str(elem->u.primitive.name);
        if (strcmp(ename, "int") == 0) { fn = "cfg_elem_int"; kind = "integer"; }
        else if (strcmp(ename, "float") == 0) { fn = "cfg_elem_float"; kind = "float"; }
        else if (strcmp(ename, "bool") == 0) { fn = "cfg_elem_bool"; kind = "boolean"; }
        else if (strcmp(ename, "ipv4") == 0) { fn = "cfg_elem_ipv4"; kind = "IPv4"; }
    }
    fprintf(f, "%scfg_parse_array(%s, %s, (void**)&%s.data, &%s.count, sizeof(*%s.data), %s, \"%s\", \"%s\", %s);\n",
            indent, ctx_expr, val, lhs, lhs, lhs, fn, kind, field, origin);
}

void emit_enum_parse(FILE *f, const char *indent, AstType *enum_type, const char *lhs, const char *enum_prefix,
                     const char *val, const char *field, const char *ctx_expr, const char *origin) {
    DynArray *members = enum_type->u.enum_type.enum_decl->data.enum_type.members;
    for (size_t k = 0; k < members->count; k++) {
        const char *mname = get_str(*(InternResult**)dynarray_get(members, k));
        fprintf(f, "%s%sif (strcmp(%s, \"%s\") == 0) %s = %s_%s;\n",
                indent, (k == 0 ? "" : "else "), val, mname, lhs, enum_prefix, mname);
    }
    fprintf(f, "%selse cfg_fail_enum(%s, \"%s\", %s, \"", indent, ctx_expr, field, val);
    for (size_t k = 0; k < members->count; k++) {
        fprintf(f, "%s%s", (k == 0 ? "" : ", "), get_str(*(InternResult**)dynarray_get(members, k)));
    }
    fprintf(f, "\", %s);\n", origin);
}

void emit_scalar_parse(FILE *f, const char *indent, const char *tname, const char *lhs,
                       const char *val, const char *field, const char *ctx_expr, const char *origin) {
    const char *fn = NULL, *kind = NULL;
    if (strcmp(tname, "int") == 0) { fn = "cfg_parse_int"; kind = "integer"; }
    else if (strcmp(tname, "float") == 0) { fn = "cfg_parse_float"; kind = "float"; }
    else if (strcmp(tname, "bool") == 0) { fn = "cfg_parse_bool"; kind = "boolean"; }
    else if (strcmp(tname, "ipv4") == 0) { fn = "cfg_parse_ipv4"; kind = "IPv4"; }

    if (fn) {
        fprintf(f, "%sif (!%s(%s, &%s)) cfg_fail_value(%s, \"%s\", \"%s\", %s, %s);\n",
                indent, fn, val, lhs, ctx_expr, kind, field, val, origin);
    } else if (strcmp(tname, "string") == 0 || strcmp(tname, "path") == 0) {
        fprintf(f, "%s%s = cfg_intern_string(%s, %s);\n", indent, lhs, ctx_expr, val);
    }
}

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
        emit_mark_present(f, "    ", field, prefix);
        
        if (type->kind == AST_TYPE_PRIMITIVE && strcmp(get_str(type->u.primitive.name), "ipv4") == 0) {
            fprintf(f, "    cfg_parse_ipv4(\"%s\", &%s%s);\n", get_str(default_node->data.literal.value.string_val), prefix, fname);
        } else {
            fprintf(f, "    %s%s = ", prefix, fname);
            switch (default_node->data.literal.type) {
                case INT_LITERAL: fprintf(f, "%lld;\n", default_node->data.literal.value.int_val); break;
                case FLOAT_LITERAL: fprintf(f, "%f;\n", default_node->data.literal.value.float_val); break;
                case BOOL_LITERAL: fprintf(f, "%s;\n", default_node->data.literal.value.bool_val ? "true" : "false"); break;
                case STRING_LITERAL:
                    fprintf(f, "cfg_intern_string(%s, ", ctx_name);
                    emit_c_string(f, default_node->data.literal.value.string_val);
                    fprintf(f, ");\n");
                    break;
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
        AstType *type = &field->data.field_decl.type->data.ast_type;
        if (type->kind == AST_TYPE_PRIMITIVE) {
            char lhs[512], origin[300];
            snprintf(lhs, sizeof(lhs), "%s%s", prefix, get_str(name));
            snprintf(origin, sizeof(origin), "\"env %s\"", env_var);
            fprintf(f, "    { const char *e = getenv(\"%s\"); if (e) {\n", env_var);
            emit_scalar_parse(f, "        ", get_str(type->u.primitive.name), lhs, "e", get_str(name), ctx_name, origin);
            emit_mark_present(f, "        ", field, prefix);
            fprintf(f, "    } }\n");
        }
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

    emit_indent(f, target_depth + 1); fprintf(f, "if (key && num_parts == %d) {\n", target_depth);
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            InternResult *name = item->data.field_decl.name;
            AstType *type = &item->data.field_decl.type->data.ast_type;
            const char *fname = get_str(name);

            emit_indent(f, target_depth + 2); fprintf(f, "if (strcmp(key, \"%s\") == 0) {\n", fname);
            if (type->kind == AST_TYPE_PRIMITIVE) {
                char lhs[512];
                snprintf(lhs, sizeof(lhs), "%s%s", prefix, fname);
                emit_indent(f, target_depth + 3);
                emit_scalar_parse(f, "", get_str(type->u.primitive.name), lhs, "val", fname, "ctx", "NULL");
            } else if (type->kind == AST_TYPE_ENUM) {
                char lhs[512], enum_prefix[512], indent[64];
                snprintf(lhs, sizeof(lhs), "%s%s", prefix, fname);
                snprintf(enum_prefix, sizeof(enum_prefix), "%s_%s", parent_name, fname);
                snprintf(indent, sizeof(indent), "%*s", (target_depth + 3) * 4, "");
                emit_enum_parse(f, indent, type, lhs, enum_prefix, "val", fname, "ctx", "NULL");
            } else if (type->kind == AST_TYPE_ARRAY) {
                AstNode *elem = type->u.array.elem;
                bool is_struct_array = elem->data.ast_type.kind == AST_TYPE_PRIMITIVE &&
                                       is_schema_name(ctx, get_str(elem->data.ast_type.u.primitive.name));
                if (!is_struct_array) {
                    char lhs[512];
                    snprintf(lhs, sizeof(lhs), "%s%s", prefix, fname);
                    emit_indent(f, target_depth + 3);
                    emit_array_parse(f, "", elem, lhs, "val", fname, "ctx", "NULL");
                }
            }
            char indent[64];
            snprintf(indent, sizeof(indent), "%*s", (target_depth + 3) * 4, "");
            emit_mark_present(f, indent, item, prefix);
            emit_indent(f, target_depth + 3); fprintf(f, "return true;\n");
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
            } else if (type_node->data.ast_type.kind == AST_TYPE_ARRAY) {
                AstNode *elem = type_node->data.ast_type.u.array.elem;
                if (elem->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                    const char *elem_tname = get_str(elem->data.ast_type.u.primitive.name);
                    if (is_schema_name(ctx, elem_tname)) {
                        const char *fname = get_str(item->data.field_decl.name);
                        emit_indent(f, target_depth + 1);
                        fprintf(f, "if (%d < num_parts && strcmp(parts[%d], \"%s\") == 0) {\n", target_depth, target_depth, fname);
                        
                        emit_indent(f, target_depth + 2);
                        fprintf(f, "if (!key && !val && %d == num_parts - 1) {\n", target_depth);
                        emit_indent(f, target_depth + 3); fprintf(f, "size_t old_count = %s%s.count;\n", prefix, fname);
                        emit_indent(f, target_depth + 3); fprintf(f, "%s_t *new_data = (%s_t*)cfg_pool_alloc(&ctx->pool, (old_count + 1) * sizeof(%s_t));\n", elem_tname, elem_tname, elem_tname);
                        emit_indent(f, target_depth + 3); fprintf(f, "if (old_count > 0) memcpy(new_data, %s%s.data, old_count * sizeof(%s_t));\n", prefix, fname, elem_tname);
                        emit_indent(f, target_depth + 3); fprintf(f, "memset(&new_data[old_count], 0, sizeof(%s_t));\n", elem_tname);
                        emit_indent(f, target_depth + 3); fprintf(f, "%s%s.data = new_data;\n", prefix, fname);
                        emit_indent(f, target_depth + 3); fprintf(f, "%s%s.count = old_count + 1;\n", prefix, fname);
                        
                        char new_prefix[256];
                        snprintf(new_prefix, sizeof(new_prefix), "%s%s.data[old_count].", prefix, fname);
                        AstNode *target_schema = get_schema_by_name(ctx, elem_tname);
                        emit_default_initialization_recursive(ctx, f, target_schema, new_prefix, "ctx");

                        emit_indent(f, target_depth + 3); fprintf(f, "return true;\n");
                        emit_indent(f, target_depth + 2); fprintf(f, "}\n");
                        
                        emit_indent(f, target_depth + 2);
                        fprintf(f, "if (key && val && %s%s.count > 0) {\n", prefix, fname);
                        snprintf(new_prefix, sizeof(new_prefix), "%s%s.data[%s%s.count - 1].", prefix, fname, prefix, fname);
                        emit_ini_handler_body(ctx, f, target_schema, new_prefix, elem_tname, target_depth + 1);
                        emit_indent(f, target_depth + 2); fprintf(f, "}\n");
                        
                        emit_indent(f, target_depth + 1); fprintf(f, "}\n");
                    }
                }
            }
        }
    }
}

static void emit_cli_parser(CodegenContext *ctx, FILE *f, AstNode *schema, const char *schema_name) {
    fprintf(f, "static bool %s_parse_arg(cfg_common_context_t *ctx, int argc, const char **argv, int *index) {\n", schema_name);
    fprintf(f, "    int i = *index;\n");
    fprintf(f, "    const char *arg = argv[i];\n");
    char prefix[256]; snprintf(prefix, sizeof(prefix), "((%s_t*)ctx->cfg)->", schema_name);
    emit_cli_parser_recursive(ctx, f, schema, prefix, schema_name, schema_name);
    fprintf(f, "    *index = i;\n");
    fprintf(f, "    return false;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static void %s_parse_cli_ctx(cfg_common_context_t *ctx, int argc, const char **argv) {\n", schema_name);
    fprintf(f, "    if (!argv || argc <= 1) return;\n");
    fprintf(f, "    for (int i = 1; i < argc; i++) {\n");
    fprintf(f, "        if (argv[i][0] != '-') continue;\n");
    fprintf(f, "        if (%s_parse_arg(ctx, argc, argv, &i)) continue;\n", schema_name);
    fprintf(f, "#ifndef CFG_ALLOW_UNKNOWN_KEYS\n");
    fprintf(f, "        /* Options outside the --%s. namespace belong to the application. */\n", schema_name);
    fprintf(f, "        if (strncmp(argv[i], \"--%s.\", %zu) == 0) cfg_fail_arg(ctx, \"unknown option\", argv[i], argv[i]);\n", schema_name, strlen(schema_name) + 3);
    fprintf(f, "#endif\n");
    fprintf(f, "    }\n");
    fprintf(f, "}\n\n");

    fprintf(f, "void %s_parse_cli(%s_t *cfg, int argc, const char **argv) {\n", schema_name, schema_name);
    fprintf(f, "    if (!cfg) return;\n");
    fprintf(f, "    cfg_common_context_t ctx = { cfg, (cfg_pool_node_t*)cfg->internal_pool, NULL, 0, CFG_SUCCESS };\n");
    fprintf(f, "    %s_parse_cli_ctx(&ctx, argc, argv);\n", schema_name);
    fprintf(f, "    cfg->internal_pool = ctx.pool;\n");
    fprintf(f, "}\n\n");
}

void emit_recursive_ini_handler(CodegenContext *ctx, FILE *f, AstNode *schema, const char *schema_name) {
    fprintf(f, "/* Returns false if no field matches the section path and key. */\n");
    fprintf(f, "static bool %s_ini_handler_recursive(cfg_common_context_t *ctx, const char *key, const char *val, char **parts, int num_parts, int depth) {\n", schema_name);
    fprintf(f, "    (void)parts; (void)depth;\n");
    char prefix[256]; snprintf(prefix, sizeof(prefix), "((%s_t*)ctx->cfg)->", schema_name);
    emit_ini_handler_body(ctx, f, schema, prefix, schema_name, 0);
    fprintf(f, "    return false;\n");
    fprintf(f, "}\n\n");
    emit_cli_parser(ctx, f, schema, schema_name);
}
