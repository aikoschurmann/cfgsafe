#include <string.h>
#include "codegen/codegen_common.h"

static bool is_field_secret(AstNode *field) {
    DynArray *props = field->data.field_decl.properties;
    if (!props) return false;
    for (size_t i = 0; i < props->count; i++) {
        AstNode *pnode = *(AstNode**)dynarray_get(props, i);
        if (strcmp(get_str(pnode->data.property_decl.name), "secret") == 0) {
            return pnode->data.property_decl.value->data.literal.value.bool_val;
        }
    }
    return false;
}

static void emit_print_recursive(CodegenContext *ctx, FILE *f, AstNode *node, const char *prefix, int depth) {
    DynArray *items = (node->node_type == AST_SCHEMA_DECL) 
        ? node->data.schema_decl.items 
        : node->data.section_decl.items;

    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            InternResult *name = item->data.field_decl.name;
            AstType *type = &item->data.field_decl.type->data.ast_type;
            const char *fname = get_str(name);
            bool secret = is_field_secret(item);

            fprintf(f, "    fprintf(f, \"%%*s%s = \", %d, \"\");\n", fname, depth * 2);
            
            if (secret) {
                fprintf(f, "    fprintf(f, \"********\\n\");\n");
                continue;
            }

            if (type->kind == AST_TYPE_PRIMITIVE) {
                const char *tname = get_str(type->u.primitive.name);
                if (is_schema_name(ctx, tname)) {
                    fprintf(f, "    fprintf(f, \"{\\n\");\n");
                    char new_prefix[256];
                    snprintf(new_prefix, sizeof(new_prefix), "%s%s.", prefix, fname);
                    AstNode *target_schema = get_schema_by_name(ctx, tname);
                    emit_print_recursive(ctx, f, target_schema, new_prefix, depth + 1);
                    fprintf(f, "    fprintf(f, \"%%*s}\\n\", %d, \"\");\n", depth * 2);
                } else if (strcmp(tname, "int") == 0) {
                    fprintf(f, "    fprintf(f, \"%%lld\\n\", (long long)%s%s);\n", prefix, fname);
                } else if (strcmp(tname, "float") == 0) {
                    fprintf(f, "    fprintf(f, \"%%f\\n\", %s%s);\n", prefix, fname);
                } else if (strcmp(tname, "bool") == 0) {
                    fprintf(f, "    fprintf(f, \"%%s\\n\", %s%s ? \"true\" : \"false\");\n", prefix, fname);
                } else if (strcmp(tname, "string") == 0 || strcmp(tname, "path") == 0) {
                    fprintf(f, "    fprintf(f, \"\\\"%%s\\\"\\n\", %s%s ? %s%s : \"null\");\n", prefix, fname, prefix, fname);
                } else if (strcmp(tname, "ipv4") == 0) {
                    fprintf(f, "    fprintf(f, \"%%u.%%u.%%u.%%u\\n\", %s%s.octets[0], %s%s.octets[1], %s%s.octets[2], %s%s.octets[3]);\n", prefix, fname, prefix, fname, prefix, fname, prefix, fname);
                }
            } else if (type->kind == AST_TYPE_ENUM) {
                fprintf(f, "    fprintf(f, \"%%d (enum)\\n\", (int)%s%s);\n", prefix, fname);
            } else if (type->kind == AST_TYPE_ARRAY) {
                fprintf(f, "    fprintf(f, \"[ \");\n");
                fprintf(f, "    for (size_t i = 0; i < %s%s.count; i++) fprintf(f, \"%%s%%s\", i > 0 ? \", \" : \"\", ((const char**)%s%s.data)[i]);\n", prefix, fname, prefix, fname);
                fprintf(f, "    fprintf(f, \" ]\\n\");\n");
            }
        } else if (item->node_type == AST_SECTION_DECL) {
            const char *sec_name = get_str(item->data.section_decl.name);
            fprintf(f, "    fprintf(f, \"%%*s[%s]\\n\", %d, \"\");\n", sec_name, depth * 2);
            char new_prefix[256];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s.", prefix, sec_name);
            emit_print_recursive(ctx, f, item, new_prefix, depth + 1);
        }
    }
}

void emit_print_function(CodegenContext *ctx, FILE *f, AstNode *schema, const char *schema_name) {
    fprintf(f, "void %s_print(const %s_t *cfg, FILE *f) {\n", schema_name, schema_name);
    fprintf(f, "    if (!cfg) return;\n");
    fprintf(f, "    if (!f) f = stdout;\n");
    fprintf(f, "    fprintf(f, \"--- %s Configuration ---\\n\");\n", schema_name);
    emit_print_recursive(ctx, f, schema, "cfg->", 0);
    fprintf(f, "    fprintf(f, \"--------------------------\\n\");\n");
    fprintf(f, "}\n\n");
}
