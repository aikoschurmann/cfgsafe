#include <string.h>
#include "codegen/codegen_common.h"

const char* map_primitive_to_c(const char* name) {
    if (strcmp(name, "int") == 0) return "int64_t";
    if (strcmp(name, "float") == 0) return "double";
    if (strcmp(name, "bool") == 0) return "bool";
    if (strcmp(name, "string") == 0) return "const char*";
    if (strcmp(name, "ipv4") == 0) return "cfg_ipv4_t";
    if (strcmp(name, "path") == 0) return "const char*";
    return "void*";
}

static void emit_enum(FILE *f, AstNode *enum_node, const char *full_enum_name) {
    fprintf(f, "typedef enum {\n");
    DynArray *members = enum_node->data.enum_type.members;
    for (size_t i = 0; i < members->count; i++) {
        InternResult *member = *(InternResult**)dynarray_get(members, i);
        fprintf(f, "    %s_%s,\n", full_enum_name, get_str(member));
    }
    fprintf(f, "} %s_t;\n\n", full_enum_name);
}

static void emit_struct_field(CodegenContext *ctx, FILE *f, AstNode *field, const char* parent_name) {
    InternResult *name = field->data.field_decl.name;
    AstNode *type = field->data.field_decl.type;

    switch (type->data.ast_type.kind) {
        case AST_TYPE_PRIMITIVE: {
            const char* type_name = get_str(type->data.ast_type.u.primitive.name);
            const char* c_type = map_primitive_to_c(type_name);
            
            if (strcmp(c_type, "void*") == 0) {
                if (is_schema_name(ctx, type_name)) {
                    fprintf(f, "    %s_t %s;\n", type_name, get_str(name));
                } else {
                    fprintf(f, "    void* %s;\n", get_str(name));
                }
            } else {
                fprintf(f, "    %s %s;\n", c_type, get_str(name));
            }
            break;
        }
        case AST_TYPE_ARRAY: {
            AstNode *elem = type->data.ast_type.u.array.elem;
            const char* c_type = "void*";
            if (elem->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                const char* elem_type_name = get_str(elem->data.ast_type.u.primitive.name);
                c_type = map_primitive_to_c(elem_type_name);
                if (strcmp(c_type, "void*") == 0 && is_schema_name(ctx, elem_type_name)) {
                    char schema_type[256];
                    snprintf(schema_type, sizeof(schema_type), "%s_t", elem_type_name);
                    c_type = schema_type;
                }
            }
            fprintf(f, "    struct { %s *data; size_t count; } %s;\n", c_type, get_str(name));
            break;
        }
        case AST_TYPE_ENUM: {
            fprintf(f, "    %s_%s_t %s;\n", parent_name, get_str(name), get_str(name));
            break;
        }
    }
}

void emit_section_or_schema(CodegenContext *ctx, FILE *f, AstNode *node, const char* name) {
    DynArray *items = (node->node_type == AST_SCHEMA_DECL) 
        ? node->data.schema_decl.items 
        : node->data.section_decl.items;

    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL && item->data.field_decl.type->data.ast_type.kind == AST_TYPE_ENUM) {
            char enum_name[256];
            snprintf(enum_name, sizeof(enum_name), "%s_%s", name, get_str(item->data.field_decl.name));
            emit_enum(f, item->data.field_decl.type->data.ast_type.u.enum_type.enum_decl, enum_name);
        }
    }

    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_SECTION_DECL) {
            emit_section_or_schema(ctx, f, item, get_str(item->data.section_decl.name));
        }
    }

    fprintf(f, "typedef struct %s_t {\n", name);
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            emit_struct_field(ctx, f, item, name);
        } else if (item->node_type == AST_SECTION_DECL) {
            fprintf(f, "    %s_t %s;\n", get_str(item->data.section_decl.name), get_str(item->data.section_decl.name));
        }
    }
    if (node->node_type == AST_SCHEMA_DECL) {
        fprintf(f, "    void* internal_pool;\n");
    }
    fprintf(f, "} %s_t;\n\n", name);
}
