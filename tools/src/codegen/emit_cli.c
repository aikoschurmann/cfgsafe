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

                /* Primitive field handling */
                char lhs[600];
                snprintf(lhs, sizeof(lhs), "%s%s", prefix, fname);
                fprintf(f, "    if (strcmp(arg, \"--%s\") == 0", full_path);
                if (short_flag) fprintf(f, " || (arg[0] == '-' && arg[1] == '%c' && arg[2] == '\\0')", short_flag[0]);
                fprintf(f, ") {\n");
                
                if (strcmp(tname, "bool") == 0) {
                    fprintf(f, "        %s%s = true;\n", prefix, fname);
                    emit_mark_present(f, "        ", item, prefix);
                    fprintf(f, "        return true;\n");
                } else {
                    fprintf(f, "        if (i + 1 < argc) {\n");
                    fprintf(f, "            const char *val = argv[++i];\n");
                    emit_scalar_parse(f, "            ", tname, lhs, "val", fname, "ctx", "\"command line\"");
                    emit_mark_present(f, "            ", item, prefix);
                    fprintf(f, "            *index = i; return true;\n");
                    fprintf(f, "        }\n");
                }
                fprintf(f, "    }\n");
                
                /* Handle --key=val syntax */
                if (strcmp(tname, "bool") != 0) {
                    fprintf(f, "    if (strncmp(arg, \"--%s=\", %zu) == 0) {\n", full_path, strlen(full_path) + 3);
                    fprintf(f, "        const char *val = arg + %zu;\n", strlen(full_path) + 3);
                    emit_scalar_parse(f, "        ", tname, lhs, "val", fname, "ctx", "\"command line\"");
                    emit_mark_present(f, "        ", item, prefix);
                    fprintf(f, "        return true;\n");
                    fprintf(f, "    }\n");
                }

            } else if (type_node->data.ast_type.kind == AST_TYPE_ENUM) {
                /* Enum field handling */
                fprintf(f, "    if (strcmp(arg, \"--%s\") == 0", full_path);
                if (short_flag) fprintf(f, " || (arg[0] == '-' && arg[1] == '%c' && arg[2] == '\\0')", short_flag[0]);
                fprintf(f, ") {\n");
                fprintf(f, "        if (i + 1 < argc) {\n");
                fprintf(f, "            const char *val = argv[++i];\n");
                char lhs[600], enum_prefix[512];
                snprintf(lhs, sizeof(lhs), "%s%s", prefix, fname);
                snprintf(enum_prefix, sizeof(enum_prefix), "%s_%s", parent_name, fname);
                emit_enum_parse(f, "            ", &type_node->data.ast_type, lhs, enum_prefix, "val", fname, "ctx", "\"command line\"");
                emit_mark_present(f, "            ", item, prefix);
                fprintf(f, "            *index = i; return true;\n");
                fprintf(f, "        }\n");
                fprintf(f, "    }\n");
            } else if (type_node->data.ast_type.kind == AST_TYPE_ARRAY) {
                /* Array field handling. Arrays of structs come only from [[...]] INI sections. */
                AstType *elem_type = &type_node->data.ast_type.u.array.elem->data.ast_type;
                if (elem_type->kind == AST_TYPE_PRIMITIVE && is_schema_name(ctx, get_str(elem_type->u.primitive.name))) continue;
                fprintf(f, "    if (strcmp(arg, \"--%s\") == 0", full_path);
                if (short_flag) fprintf(f, " || (arg[0] == '-' && arg[1] == '%c' && arg[2] == '\\0')", short_flag[0]);
                fprintf(f, ") {\n");
                fprintf(f, "        if (i + 1 < argc) {\n");
                char lhs[600];
                snprintf(lhs, sizeof(lhs), "%s%s", prefix, fname);
                fprintf(f, "            const char *val = argv[++i];\n");
                emit_array_parse(f, "            ", type_node->data.ast_type.u.array.elem, lhs, "val", fname, "ctx", "\"command line\"");
                fprintf(f, "            *index = i; return true;\n");
                fprintf(f, "        }\n");
                fprintf(f, "    }\n");
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
