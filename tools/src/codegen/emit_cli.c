#include <string.h>
#include "codegen/codegen_common.h"

static const char* get_short_flag(AstNode *field) {
    DynArray *props = field->data.field_decl.properties;
    for (size_t i = 0; i < props->count; i++) {
        AstNode *pnode = *(AstNode**)dynarray_get(props, i);
        if (strcmp(get_str(pnode->data.property_decl.name), "short") == 0) {
            return get_str(pnode->data.property_decl.value->data.literal.value.string_val);
        }
    }
    return NULL;
}

/* Emits the statements that store the C string `val` into the field. */
static void emit_cli_value_body(FILE *f, AstNode *field, AstType *type, const char *prefix, const char *parent_name) {
    const char *fname = get_str(field->data.field_decl.name);
    char lhs[600];
    snprintf(lhs, sizeof(lhs), "%s%s", prefix, fname);
    if (type->kind == AST_TYPE_PRIMITIVE) {
        emit_scalar_parse(f, "        ", get_str(type->u.primitive.name), lhs, "val", fname, "ctx", "\"command line\"", field_is_secret(field));
    } else if (type->kind == AST_TYPE_ENUM) {
        char enum_prefix[512];
        snprintf(enum_prefix, sizeof(enum_prefix), "%s_%s", parent_name, fname);
        emit_enum_parse(f, "        ", type, lhs, enum_prefix, "val", fname, "ctx", "\"command line\"", field_is_secret(field));
    } else {
        emit_array_parse(f, "        ", type->u.array.elem, lhs, "val", fname, "ctx", "\"command line\"", field_is_secret(field));
    }
    emit_mark_present(f, "        ", field, prefix);
}

/* Emits matching for `--path value`, `-s value` and `--path=value`. A bool
 * given as `--path` or `-s` alone is set to true. */
static void emit_cli_option(FILE *f, AstNode *field, AstType *type, const char *prefix,
                            const char *full_path, const char *short_flag, const char *parent_name) {
    const char *fname = get_str(field->data.field_decl.name);
    bool is_bool = type->kind == AST_TYPE_PRIMITIVE && strcmp(get_str(type->u.primitive.name), "bool") == 0;

    fprintf(f, "    if (strcmp(arg, \"--%s\") == 0", full_path);
    if (short_flag) fprintf(f, " || (arg[0] == '-' && arg[1] == '%c' && arg[2] == '\\0')", short_flag[0]);
    fprintf(f, ") {\n");
    if (is_bool) {
        fprintf(f, "        %s%s = true;\n", prefix, fname);
        emit_mark_present(f, "        ", field, prefix);
    } else {
        fprintf(f, "        if (i + 1 >= argc) { cfg_fail_arg(ctx, \"missing value for option\", arg, \"%s\"); return true; }\n", fname);
        fprintf(f, "        const char *val = argv[++i];\n");
        fprintf(f, "        *index = i;\n");
        emit_cli_value_body(f, field, type, prefix, parent_name);
    }
    fprintf(f, "        return true;\n");
    fprintf(f, "    }\n");

    fprintf(f, "    if (strncmp(arg, \"--%s=\", %zu) == 0) {\n", full_path, strlen(full_path) + 3);
    fprintf(f, "        const char *val = arg + %zu;\n", strlen(full_path) + 3);
    emit_cli_value_body(f, field, type, prefix, parent_name);
    fprintf(f, "        return true;\n");
    fprintf(f, "    }\n");
}

void emit_cli_parser_recursive(CodegenContext *ctx, FILE *f, AstNode *node, const char *prefix, const char *path_prefix, const char *parent_name) {
    DynArray *items = (node->node_type == AST_SCHEMA_DECL) ? node->data.schema_decl.items : node->data.section_decl.items;
    
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            AstNode *type_node = item->data.field_decl.type;
            const char *fname = get_str(item->data.field_decl.name);
            const char *short_flag = get_short_flag(item);
            
            char full_path[512];
            if (path_prefix && path_prefix[0]) {
                snprintf(full_path, sizeof(full_path), "%s.%s", path_prefix, fname);
            } else {
                strncpy(full_path, fname, sizeof(full_path));
            }

            if (type_node->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                const char *tname = get_str(type_node->data.ast_type.u.primitive.name);
                if (is_schema_name(ctx, tname)) {
                    char new_prefix[512];
                    snprintf(new_prefix, sizeof(new_prefix), "%s%s.", prefix, fname);
                    AstNode *target_schema = get_schema_by_name(ctx, tname);
                    emit_cli_parser_recursive(ctx, f, target_schema, new_prefix, full_path, tname);
                    continue;
                }

                emit_cli_option(f, item, &type_node->data.ast_type, prefix, full_path, short_flag, parent_name);
            } else if (type_node->data.ast_type.kind == AST_TYPE_ENUM) {
                emit_cli_option(f, item, &type_node->data.ast_type, prefix, full_path, short_flag, parent_name);
            } else if (type_node->data.ast_type.kind == AST_TYPE_ARRAY) {
                /* Arrays of structs come only from [[...]] INI sections. */
                AstType *elem_type = &type_node->data.ast_type.u.array.elem->data.ast_type;
                if (elem_type->kind == AST_TYPE_PRIMITIVE && is_schema_name(ctx, get_str(elem_type->u.primitive.name))) continue;
                emit_cli_option(f, item, &type_node->data.ast_type, prefix, full_path, short_flag, parent_name);
            }
        } else if (item->node_type == AST_SECTION_DECL) {
            const char *sec_name = get_str(item->data.section_decl.name);
            char new_prefix[512];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s.", prefix, sec_name);
            char new_path_prefix[512];
            if (path_prefix && path_prefix[0]) {
                snprintf(new_path_prefix, sizeof(new_path_prefix), "%s.%s", path_prefix, sec_name);
            } else {
                strncpy(new_path_prefix, sec_name, sizeof(new_path_prefix));
            }
            emit_cli_parser_recursive(ctx, f, item, new_prefix, new_path_prefix, sec_name);
        }
    }
}
