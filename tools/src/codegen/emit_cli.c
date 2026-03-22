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
                fprintf(f, "    if (strcmp(arg, \"--%s\") == 0", full_path);
                if (short_flag) fprintf(f, " || (arg[0] == '-' && arg[1] == '%c' && arg[2] == '\\0')", short_flag[0]);
                fprintf(f, ") {\n");
                
                if (strcmp(tname, "bool") == 0) {
                    fprintf(f, "        %s%s = true; return true;\n", prefix, fname);
                } else {
                    fprintf(f, "        if (i + 1 < argc) {\n");
                    fprintf(f, "            const char *val = argv[++i];\n");
                    if (strcmp(tname, "int") == 0) fprintf(f, "            %s%s = strtoll(val, NULL, 10);\n", prefix, fname);
                    else if (strcmp(tname, "float") == 0) fprintf(f, "            %s%s = strtod(val, NULL);\n", prefix, fname);
                    else if (strcmp(tname, "string") == 0 || strcmp(tname, "path") == 0) fprintf(f, "            %s%s = cfg_intern_string(ctx, val);\n", prefix, fname);
                    else if (strcmp(tname, "ipv4") == 0) fprintf(f, "            cfg_parse_ipv4(val, &%s%s);\n", prefix, fname);
                    fprintf(f, "            *index = i; return true;\n");
                    fprintf(f, "        }\n");
                }
                fprintf(f, "    }\n");
                
                /* Handle --key=val syntax */
                if (strcmp(tname, "bool") != 0) {
                    fprintf(f, "    if (strncmp(arg, \"--%s=\", %zu) == 0) {\n", full_path, strlen(full_path) + 3);
                    fprintf(f, "        const char *val = arg + %zu;\n", strlen(full_path) + 3);
                    if (strcmp(tname, "int") == 0) fprintf(f, "        %s%s = strtoll(val, NULL, 10);\n", prefix, fname);
                    else if (strcmp(tname, "float") == 0) fprintf(f, "        %s%s = strtod(val, NULL);\n", prefix, fname);
                    else if (strcmp(tname, "string") == 0 || strcmp(tname, "path") == 0) fprintf(f, "        %s%s = cfg_intern_string(ctx, val);\n", prefix, fname);
                    else if (strcmp(tname, "ipv4") == 0) fprintf(f, "        cfg_parse_ipv4(val, &%s%s);\n", prefix, fname);
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
                DynArray *members = type_node->data.ast_type.u.enum_type.enum_decl->data.enum_type.members;
                for (size_t k = 0; k < members->count; k++) {
                    InternResult *member = *(InternResult**)dynarray_get(members, k);
                    const char *mname = get_str(member);
                    fprintf(f, "            %sif (strcmp(val, \"%s\") == 0) %s%s = %s_%s_%s;\n", 
                        (k == 0 ? "" : "else "), mname, prefix, fname, parent_name, fname, mname);
                }
                fprintf(f, "            *index = i; return true;\n");
                fprintf(f, "        }\n");
                fprintf(f, "    }\n");
            } else if (type_node->data.ast_type.kind == AST_TYPE_ARRAY) {
                /* Array field handling */
                fprintf(f, "    if (strcmp(arg, \"--%s\") == 0", full_path);
                if (short_flag) fprintf(f, " || (arg[0] == '-' && arg[1] == '%c' && arg[2] == '\\0')", short_flag[0]);
                fprintf(f, ") {\n");
                fprintf(f, "        if (i + 1 < argc) {\n");
                fprintf(f, "            cfg_parse_array(ctx, argv[++i], (void**)&%s%s.data, &%s%s.count);\n", prefix, fname, prefix, fname);
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
