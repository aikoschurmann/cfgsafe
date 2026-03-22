#include <string.h>
#include "codegen/codegen_common.h"

const char* get_str(InternResult *res) {
    if (!res || !res->key) return "unknown";
    return ((Slice*)res->key)->ptr;
}

bool is_schema_name(CodegenContext *ctx, const char *name) {
    if (!name || strcmp(name, "unknown") == 0) return false;
    AstProgram *prog = &ctx->program->data.program;
    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema = *(AstNode**)dynarray_get(prog->schemas, i);
        if (schema->node_type == AST_SCHEMA_DECL && strcmp(get_str(schema->data.schema_decl.name), name) == 0) {
            return true;
        }
    }
    return false;
}

AstNode* get_schema_by_name(CodegenContext *ctx, const char *name) {
    if (!name || strcmp(name, "unknown") == 0) return NULL;
    AstProgram *prog = &ctx->program->data.program;
    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema = *(AstNode**)dynarray_get(prog->schemas, i);
        if (schema->node_type == AST_SCHEMA_DECL && strcmp(get_str(schema->data.schema_decl.name), name) == 0) {
            return schema;
        }
    }
    return NULL;
}

void track_usage(AstNode *node, UsageTracker *tracker) {
    if (!node) return;

    switch (node->node_type) {
        case AST_PROGRAM: {
            AstProgram *prog = &node->data.program;
            for (size_t i = 0; i < prog->schemas->count; i++) {
                track_usage(*(AstNode**)dynarray_get(prog->schemas, i), tracker);
            }
            break;
        }
        case AST_SCHEMA_DECL: {
            DynArray *items = node->data.schema_decl.items;
            for (size_t i = 0; i < items->count; i++) {
                track_usage(*(AstNode**)dynarray_get(items, i), tracker);
            }
            break;
        }
        case AST_SECTION_DECL: {
            DynArray *items = node->data.section_decl.items;
            for (size_t i = 0; i < items->count; i++) {
                track_usage(*(AstNode**)dynarray_get(items, i), tracker);
            }
            break;
        }
        case AST_FIELD_DECL: {
            track_usage(node->data.field_decl.type, tracker);
            DynArray *props = node->data.field_decl.properties;
            for (size_t i = 0; i < props->count; i++) {
                AstNode *pnode = *(AstNode**)dynarray_get(props, i);
                const char *pname = get_str(pnode->data.property_decl.name);
                if (strcmp(pname, "min_length") == 0 || strcmp(pname, "max_length") == 0) tracker->uses_strlen = true;
                if (strcmp(pname, "exists") == 0) tracker->uses_access = true;
                if (strcmp(pname, "env") == 0) tracker->uses_getenv = true;
                if (strcmp(pname, "pattern") == 0) {
                    const char *pattern = get_str(pnode->data.property_decl.value->data.literal.value.string_val);
                    bool found = false;
                    for (size_t j = 0; j < tracker->regex_patterns->count; j++) {
                        if (strcmp(*(char**)dynarray_get(tracker->regex_patterns, j), pattern) == 0) {
                            found = true; break;
                        }
                    }
                    if (!found) dynarray_push_value(tracker->regex_patterns, &pattern);
                }
            }
            break;
        }
        case AST_TYPE: {
            AstType *type = &node->data.ast_type;
            if (type->kind == AST_TYPE_PRIMITIVE) {
                const char *name = get_str(type->u.primitive.name);
                if (strcmp(name, "int") == 0) tracker->uses_int = true;
                else if (strcmp(name, "bool") == 0) tracker->uses_bool = true;
                else if (strcmp(name, "float") == 0) tracker->uses_float = true;
                else if (strcmp(name, "ipv4") == 0) tracker->uses_ipv4 = true;
            } else if (type->kind == AST_TYPE_ARRAY) {
                tracker->uses_size_t = true;
                track_usage(type->u.array.elem, tracker);
            }
            break;
        }
        default: break;
    }
}
