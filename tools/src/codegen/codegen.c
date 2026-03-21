#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "codegen/codegen.h"
#include "datastructures/utils.h"

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

CodegenContext* codegen_context_create(
    Arena *arena,
    AstNode *program,
    TypeStore *store,
    DenseInterner *identifiers,
    DenseInterner *keywords,
    const char *filename
) {
    CodegenContext *ctx = (CodegenContext*)arena_alloc(arena, sizeof(CodegenContext));
    ctx->arena = arena;
    ctx->program = program;
    ctx->store = store;
    ctx->identifiers = identifiers;
    ctx->keywords = keywords;
    ctx->filename = filename;
    return ctx;
}

static const char* get_str(InternResult *res) {
    if (!res || !res->key) return "unknown";
    return ((Slice*)res->key)->ptr;
}

static bool is_schema_name(CodegenContext *ctx, const char *name) {
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

static AstNode* get_schema_by_name(CodegenContext *ctx, const char *name) {
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

static void track_usage(AstNode *node, UsageTracker *tracker) {
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

static const char* map_primitive_to_c(const char* name) {
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

static void emit_section_or_schema(CodegenContext *ctx, FILE *f, AstNode *node, const char* name) {
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

static void emit_condition_expression(FILE *f, AstNode *cond_node, const char *prefix) {
    AstCondition *cond = &cond_node->data.condition;
    fprintf(f, "%s%s == ", prefix, get_str(cond->left));
    AstNode *rhs = cond->right;
    switch (rhs->data.literal.type) {
        case INT_LITERAL: fprintf(f, "%lld", rhs->data.literal.value.int_val); break;
        case FLOAT_LITERAL: fprintf(f, "%f", rhs->data.literal.value.float_val); break;
        case BOOL_LITERAL: fprintf(f, "%s", rhs->data.literal.value.bool_val ? "true" : "false"); break;
        case STRING_LITERAL: fprintf(f, "\"%s\"", get_str(rhs->data.literal.value.string_val)); break;
        case IDENTIFIER_LITERAL: {
            fprintf(f, "%s", get_str(rhs->data.literal.value.string_val));
            break;
        }
        default: break;
    }
}

static bool can_be_empty(AstNode *field) {
    AstType *type = &field->data.field_decl.type->data.ast_type;
    if (type->kind == AST_TYPE_ARRAY) return true;
    if (type->kind == AST_TYPE_PRIMITIVE) {
        const char *tname = get_str(type->u.primitive.name);
        return (strcmp(tname, "string") == 0 || strcmp(tname, "path") == 0);
    }
    return false;
}

static void emit_field_empty_check(FILE *f, AstNode *field, const char *prefix) {
    InternResult *name = field->data.field_decl.name;
    AstType *type = &field->data.field_decl.type->data.ast_type;
    
    if (type->kind == AST_TYPE_ARRAY) {
        fprintf(f, "%s%s.count == 0", prefix, get_str(name));
    } else {
        fprintf(f, "%s%s == NULL", prefix, get_str(name));
    }
}

static int get_regex_index(UsageTracker *tracker, const char *pattern) {
    for (size_t i = 0; i < tracker->regex_patterns->count; i++) {
        if (strcmp(*(char**)dynarray_get(tracker->regex_patterns, i), pattern) == 0) return (int)i;
    }
    return -1;
}

static void emit_validation_logic(UsageTracker *tracker, FILE *f, AstNode *field, const char *prefix) {
    InternResult *name = field->data.field_decl.name;
    DynArray *props = field->data.field_decl.properties;
    const char *fname = get_str(name);
    
    for (size_t i = 0; i < props->count; i++) {
        AstNode *pnode = *(AstNode**)dynarray_get(props, i);
        AstPropertyDecl *prop = &pnode->data.property_decl;
        const char *pname = get_str(prop->name);

        if (strcmp(pname, "range") == 0) {
            AstRangeExpr *range = &prop->value->data.range_expr;
            if (range->min->data.literal.type == INT_LITERAL) {
                fprintf(f, "    if (%s%s < %lld || %s%s > %lld) { cfg_set_error(err, \"value out of range\", \"%s\", 0); return false; }\n", 
                    prefix, fname, range->min->data.literal.value.int_val,
                    prefix, fname, range->max->data.literal.value.int_val, fname);
            } else {
                fprintf(f, "    if (%s%s < %f || %s%s > %f) { cfg_set_error(err, \"value out of range\", \"%s\", 0); return false; }\n", 
                    prefix, fname, range->min->data.literal.value.float_val,
                    prefix, fname, range->max->data.literal.value.float_val, fname);
            }
        } else if (strcmp(pname, "min") == 0) {
            if (prop->value->data.literal.type == INT_LITERAL) {
                fprintf(f, "    if (%s%s < %lld) { cfg_set_error(err, \"value too small\", \"%s\", 0); return false; }\n", prefix, fname, prop->value->data.literal.value.int_val, fname);
            } else {
                fprintf(f, "    if (%s%s < %f) { cfg_set_error(err, \"value too small\", \"%s\", 0); return false; }\n", prefix, fname, prop->value->data.literal.value.float_val, fname);
            }
        } else if (strcmp(pname, "max") == 0) {
            if (prop->value->data.literal.type == INT_LITERAL) {
                fprintf(f, "    if (%s%s > %lld) { cfg_set_error(err, \"value too large\", \"%s\", 0); return false; }\n", prefix, fname, prop->value->data.literal.value.int_val, fname);
            } else {
                fprintf(f, "    if (%s%s > %f) { cfg_set_error(err, \"value too large\", \"%s\", 0); return false; }\n", prefix, fname, prop->value->data.literal.value.float_val, fname);
            }
        } else if (strcmp(pname, "min_length") == 0) {
            if (field->data.field_decl.type->data.ast_type.kind == AST_TYPE_ARRAY) {
                fprintf(f, "    if (%s%s.count < %lld) { cfg_set_error(err, \"array too short\", \"%s\", 0); return false; }\n", prefix, fname, prop->value->data.literal.value.int_val, fname);
            } else {
                fprintf(f, "    if (%s%s != NULL && strlen(%s%s) < %lld) { cfg_set_error(err, \"string too short\", \"%s\", 0); return false; }\n", 
                        prefix, fname, prefix, fname, prop->value->data.literal.value.int_val, fname);
            }
        } else if (strcmp(pname, "max_length") == 0) {
            if (field->data.field_decl.type->data.ast_type.kind == AST_TYPE_ARRAY) {
                fprintf(f, "    if (%s%s.count > %lld) { cfg_set_error(err, \"array too long\", \"%s\", 0); return false; }\n", prefix, fname, prop->value->data.literal.value.int_val, fname);
            } else {
                fprintf(f, "    if (%s%s != NULL && strlen(%s%s) > %lld) { cfg_set_error(err, \"string too long\", \"%s\", 0); return false; }\n", 
                        prefix, fname, prefix, fname, prop->value->data.literal.value.int_val, fname);
            }
        } else if (strcmp(pname, "exists") == 0 && prop->value->data.literal.value.bool_val) {
            fprintf(f, "    if (%s%s != NULL && !CFG_FILE_EXISTS(%s%s)) { cfg_set_error(err, \"file does not exist\", \"%s\", 0); return false; }\n", prefix, fname, prefix, fname, fname);
        } else if (strcmp(pname, "pattern") == 0) {
            const char *pattern = get_str(prop->value->data.literal.value.string_val);
            int idx = get_regex_index(tracker, pattern);
            fprintf(f, "    if (%s%s != NULL && !cfg_pattern_%d_match(%s%s)) { cfg_set_error(err, \"pattern mismatch\", \"%s\", 0); return false; }\n", 
                    prefix, fname, idx, prefix, fname, fname);
        } else if (strcmp(pname, "required") == 0 && prop->value->data.literal.value.bool_val) {
            if (can_be_empty(field)) {
                fprintf(f, "    if (");
                emit_field_empty_check(f, field, prefix);
                fprintf(f, ") { cfg_set_error(err, \"required field missing\", \"%s\", 0); return false; }\n", fname);
            }
        } else if (strcmp(pname, "required_if") == 0) {
            if (can_be_empty(field)) {
                fprintf(f, "    if (");
                emit_condition_expression(f, prop->value, prefix);
                fprintf(f, ") {\n        if (");
                emit_field_empty_check(f, field, prefix);
                fprintf(f, ") { cfg_set_error(err, \"field required by condition missing\", \"%s\", 0); return false; }\n    }\n", fname);
            }
        } else if (strcmp(pname, "hook") == 0) {
            const char *hook_name = get_str(prop->value->data.literal.value.string_val);
            AstType *type = &field->data.field_decl.type->data.ast_type;
            bool is_str = (type->kind == AST_TYPE_PRIMITIVE && 
                          (strcmp(get_str(type->u.primitive.name), "string") == 0 || 
                           strcmp(get_str(type->u.primitive.name), "path") == 0));
            
            if (is_str) {
                fprintf(f, "    if (!%s(%s%s, err)) return false;\n", hook_name, prefix, fname);
            } else {
                fprintf(f, "    if (!%s(&%s%s, err)) return false;\n", hook_name, prefix, fname);
            }
        }
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

static void emit_default_initialization_recursive(CodegenContext *ctx, FILE *f, AstNode *node, const char *prefix, const char *ctx_name) {
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

static void emit_env_overrides_recursive(CodegenContext *ctx, UsageTracker *tracker, FILE *f, AstNode *node, const char *prefix, const char *ctx_name) {
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

static void emit_validation_function(UsageTracker *tracker, CodegenContext *ctx, FILE *f, AstNode *node, const char *name) {
    DynArray *items = (node->node_type == AST_SCHEMA_DECL) 
        ? node->data.schema_decl.items 
        : node->data.section_decl.items;

    fprintf(f, "bool %s_validate(const %s_t *cfg, cfg_error_t *err) {\n", name, name);
    fprintf(f, "    if (!cfg) return false;\n");
    
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            emit_validation_logic(tracker, f, item, "cfg->");
            AstNode *type_node = item->data.field_decl.type;
            if (type_node->node_type == AST_TYPE && type_node->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                const char *tname = get_str(type_node->data.ast_type.u.primitive.name);
                if (is_schema_name(ctx, tname)) {
                    fprintf(f, "    if (!%s_validate(&cfg->%s, err)) return false;\n", tname, get_str(item->data.field_decl.name));
                }
            }
        } else if (item->node_type == AST_SECTION_DECL) {
            fprintf(f, "    if (!%s_validate(&cfg->%s, err)) return false;\n", get_str(item->data.section_decl.name), get_str(item->data.section_decl.name));
        }
    }
    fprintf(f, "    return true;\n}\n\n");

    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_SECTION_DECL) {
            emit_validation_function(tracker, ctx, f, item, get_str(item->data.section_decl.name));
        }
    }
}

static void emit_validation_prototypes(FILE *f, AstNode *node, const char *name) {
    DynArray *items = (node->node_type == AST_SCHEMA_DECL) 
        ? node->data.schema_decl.items 
        : node->data.section_decl.items;

    fprintf(f, "bool %s_validate(const %s_t *cfg, cfg_error_t *err);\n", name, name);
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_SECTION_DECL) {
            emit_validation_prototypes(f, item, get_str(item->data.section_decl.name));
        }
    }
}

static void emit_char_class_check(FILE *f, const char **re) {
    const char *p = *re;
    bool negated = false;
    if (*p == '^') { negated = true; p++; }
    
    fprintf(f, "(");
    bool first = true;
    while (*p && *p != ']') {
        if (!first) fprintf(f, " %s ", negated ? "&&" : "||");
        if (*(p+1) == '-' && *(p+2) != ']' && *(p+2) != '\0') {
            fprintf(f, "(c %s '%c' %s c %s '%c')", negated? "<" : ">=", *p, negated? "||" : "&&", negated? ">" : "<=", *(p+2));
            p += 3;
        } else {
            fprintf(f, "c %s '%c'", negated ? "!=" : "==", *p);
            p++;
        }
        first = false;
    }
    fprintf(f, ")");
    if (*p == ']') p++;
    *re = p;
}

static void emit_regex_validator(FILE *f, const char *re_str, int idx) {
    fprintf(f, "static bool cfg_pattern_%d_match(const char *s) {\n", idx);
    fprintf(f, "    if (!s) return false;\n");
    fprintf(f, "    const char *start = s;\n");
    
    const char *re = re_str;
    bool anchored_start = (*re == '^');
    if (anchored_start) re++;
    
    bool anchored_end = false;
    size_t re_len = strlen(re_str);
    if (re_len > 0 && re_str[re_len-1] == '$') anchored_end = true;

    if (!anchored_start) {
        fprintf(f, "    for (; *start != '\\0'; start++) {\n");
    }
    fprintf(f, "        const char *p = start;\n");
    const char *indent = (!anchored_start) ? "        " : "    ";
    
    const char *re_p = re;
    while (*re_p && (!anchored_end || *(re_p+1) != '\0')) {
        if (*re_p == '.') {
            fprintf(f, "%sif (*p == '\\0') %s;\n", indent, anchored_start ? "return false" : "goto next");
            fprintf(f, "%sp++;\n", indent);
            re_p++;
        } else if (*re_p == '[') {
            const char *class_start = re_p + 1;
            fprintf(f, "%s{ char c = *p; if (c == '\\0' || !", indent);
            emit_char_class_check(f, &class_start);
            fprintf(f, ") %s; }\n", anchored_start ? "return false" : "goto next");
            fprintf(f, "%sp++;\n", indent);
            if (*class_start == '+') {
                fprintf(f, "%swhile (*p != '\\0') { char c = *p; if (!", indent);
                const char *class_start_2 = re_p + 1;
                emit_char_class_check(f, &class_start_2);
                fprintf(f, ") break; p++; }\n");
                re_p = class_start + 1;
            } else if (*class_start == '*') {
                fprintf(f, "%sp--; while (*p != '\\0') { char c = *p; if (!", indent);
                const char *class_start_2 = re_p + 1;
                emit_char_class_check(f, &class_start_2);
                fprintf(f, ") break; p++; }\n");
                re_p = class_start + 1;
            } else {
                re_p = class_start;
            }
        } else if (*re_p == '*' || *re_p == '+' || *re_p == '?') {
            re_p++;
        } else {
            char target = *re_p;
            fprintf(f, "%sif (*p != '%c') %s;\n", indent, target, anchored_start ? "return false" : "goto next");
            fprintf(f, "%sp++;\n", indent);
            if (*(re_p+1) == '+') {
                fprintf(f, "%swhile (*p == '%c') p++;\n", indent, target);
                re_p += 2;
            } else if (*(re_p+1) == '*') {
                fprintf(f, "%sp--; while (*p == '%c') p++;\n", indent, target);
                re_p += 2;
            } else {
                re_p++;
            }
        }
    }

    if (anchored_end) {
        fprintf(f, "%sif (*p == '\\0') return true;\n", indent);
    } else {
        fprintf(f, "%sreturn true;\n", indent);
    }

    if (!anchored_start) {
        fprintf(f, "    next:;\n");
        fprintf(f, "    }\n");
        fprintf(f, "    return false;\n");
    } else {
        fprintf(f, "    return false;\n");
    }
    
    fprintf(f, "}\n\n");
}

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

static void emit_print_function(CodegenContext *ctx, FILE *f, AstNode *schema, const char *schema_name) {
    fprintf(f, "void %s_print(const %s_t *cfg, FILE *f) {\n", schema_name, schema_name);
    fprintf(f, "    if (!cfg) return;\n");
    fprintf(f, "    if (!f) f = stdout;\n");
    fprintf(f, "    fprintf(f, \"--- %s Configuration ---\\n\");\n", schema_name);
    emit_print_recursive(ctx, f, schema, "cfg->", 0);
    fprintf(f, "    fprintf(f, \"--------------------------\\n\");\n");
    fprintf(f, "}\n\n");
}

static void emit_recursive_ini_handler(CodegenContext *ctx, FILE *f, AstNode *schema, const char *schema_name) {
    fprintf(f, "static void %s_ini_handler_recursive(cfg_common_context_t *ctx, const char *key, const char *val, char **parts, int num_parts, int depth) {\n", schema_name);
    char prefix[256]; snprintf(prefix, sizeof(prefix), "((%s_t*)ctx->cfg)->", schema_name);
    emit_ini_handler_body(ctx, f, schema, prefix, schema_name, 0);
    fprintf(f, "}\n\n");
}

bool codegen_generate_header(CodegenContext *ctx, const char *output_filename) {
    UsageTracker tracker = {0};
    tracker.regex_patterns = allocator_alloc(arena_allocator_create(ctx->arena), sizeof(DynArray));
    dynarray_init(tracker.regex_patterns, sizeof(char*), arena_allocator_create(ctx->arena));
    
    track_usage(ctx->program, &tracker);

    FILE *f = fopen(output_filename, "w");
    if (!f) return false;

    char guard[256];
    const char* last_slash = strrchr(output_filename, '/');
    const char* base_name = last_slash ? last_slash + 1 : output_filename;
    strncpy(guard, base_name, sizeof(guard));
    for (int i = 0; guard[i]; i++) {
        if (guard[i] == '.') guard[i] = '_';
        else guard[i] = toupper(guard[i]);
    }

    fprintf(f, "/* Generated by cfg-gen for %s */\n", ctx->filename);
    fprintf(f, "#ifndef %s\n", guard);
    fprintf(f, "#define %s\n\n", guard);
    
    fprintf(f, "#include <stdio.h>\n");
    if (tracker.uses_int) fprintf(f, "#include <stdint.h>\n");
    if (tracker.uses_bool) fprintf(f, "#include <stdbool.h>\n");
    if (tracker.uses_size_t) fprintf(f, "#include <stddef.h>\n");
    
    fprintf(f, "\ntypedef enum {\n");
    fprintf(f, "    CFG_SUCCESS = 0,\n");
    fprintf(f, "    CFG_ERR_OPEN_FILE,\n");
    fprintf(f, "    CFG_ERR_SYNTAX,\n");
    fprintf(f, "    CFG_ERR_VALIDATION,\n");
    fprintf(f, "} cfg_status_t;\n\n");

    fprintf(f, "typedef struct {\n");
    fprintf(f, "    char message[512];\n");
    fprintf(f, "    char field[256];\n");
    fprintf(f, "    size_t line;\n");
    fprintf(f, "} cfg_error_t;\n\n");

    AstProgram *prog = &ctx->program->data.program;
    if (prog->imports) {
        for (size_t i = 0; i < prog->imports->count; i++) {
            AstNode *imp = *(AstNode**)dynarray_get(prog->imports, i);
            fprintf(f, "#include \"%s\"\n", get_str(imp->data.import_decl.path));
        }
    }

    if (tracker.uses_int || tracker.uses_bool || tracker.uses_size_t || (prog->imports && prog->imports->count > 0)) fprintf(f, "\n");

    if (tracker.uses_ipv4) {
        if (!tracker.uses_int) fprintf(f, "#include <stdint.h>\n");
        fprintf(f, "typedef struct {\n");
        fprintf(f, "    uint8_t octets[4];\n");
        fprintf(f, "} cfg_ipv4_t;\n\n");
    }

    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema = *(AstNode**)dynarray_get(prog->schemas, i);
        const char* schema_name = get_str(schema->data.schema_decl.name);
        fprintf(f, "typedef struct %s_t %s_t;\n", schema_name, schema_name);
    }
    fprintf(f, "\n");

    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema = *(AstNode**)dynarray_get(prog->schemas, i);
        emit_section_or_schema(ctx, f, schema, get_str(schema->data.schema_decl.name));
    }

    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema = *(AstNode**)dynarray_get(prog->schemas, i);
        const char* schema_name = get_str(schema->data.schema_decl.name);
        fprintf(f, "cfg_status_t %s_load(%s_t *cfg, const char *filename, cfg_error_t *err);\n", schema_name, schema_name);
        fprintf(f, "void %s_print(const %s_t *cfg, FILE *f);\n", schema_name, schema_name);
        fprintf(f, "void %s_free(%s_t *cfg);\n", schema_name, schema_name);
        emit_validation_prototypes(f, schema, schema_name);
    }

    fprintf(f, "\n#endif /* %s */\n\n", guard);

    fprintf(f, "#ifdef CONFIG_IMPLEMENTATION\n\n");
    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "#include <stdlib.h>\n");
    fprintf(f, "#include <string.h>\n");
    fprintf(f, "#include <ctype.h>\n");
    
    if (tracker.uses_access) {
        fprintf(f, "\n#ifndef CFG_FILE_EXISTS\n");
        fprintf(f, "#ifdef _WIN32\n");
        fprintf(f, "#include <io.h>\n");
        fprintf(f, "#define CFG_FILE_EXISTS(path) (_access((path), 0) == 0)\n");
        fprintf(f, "#else\n");
        fprintf(f, "#include <unistd.h>\n");
        fprintf(f, "#define CFG_FILE_EXISTS(path) (access((path), F_OK) == 0)\n");
        fprintf(f, "#endif\n");
        fprintf(f, "#endif /* CFG_FILE_EXISTS */\n");
    }

    fprintf(f, "\ntypedef struct cfg_pool_node {\n");
    fprintf(f, "    void *ptr;\n");
    fprintf(f, "    struct cfg_pool_node *next;\n");
    fprintf(f, "} cfg_pool_node_t;\n\n");
    
    fprintf(f, "static void* cfg_pool_alloc(cfg_pool_node_t **head, size_t size) {\n");
    fprintf(f, "    void *ptr = malloc(size);\n");
    fprintf(f, "    if (!ptr) return NULL;\n");
    fprintf(f, "    cfg_pool_node_t *node = (cfg_pool_node_t*)malloc(sizeof(cfg_pool_node_t));\n");
    fprintf(f, "    if (!node) { free(ptr); return NULL; }\n");
    fprintf(f, "    node->ptr = ptr;\n");
    fprintf(f, "    node->next = *head;\n");
    fprintf(f, "    *head = node;\n");
    fprintf(f, "    return ptr;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static void cfg_pool_free(cfg_pool_node_t *head) {\n");
    fprintf(f, "    while (head) {\n");
    fprintf(f, "        cfg_pool_node_t *next = head->next;\n");
    fprintf(f, "        free(head->ptr);\n");
    fprintf(f, "        free(head);\n");
    fprintf(f, "        head = next;\n");
    fprintf(f, "    }\n");
    fprintf(f, "}\n\n");

    fprintf(f, "typedef struct {\n    void *cfg;\n    cfg_pool_node_t *pool;\n} cfg_common_context_t;\n\n");

    fprintf(f, "static const char* cfg_intern_string(cfg_common_context_t *ctx, const char *s) {\n");
    fprintf(f, "    if (!s) return NULL;\n");
    fprintf(f, "    char *copy = (char*)cfg_pool_alloc(&ctx->pool, strlen(s) + 1);\n");
    fprintf(f, "    if (copy) strcpy(copy, s);\n");
    fprintf(f, "    return copy;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static void cfg_parse_array(cfg_common_context_t *ctx, const char *val, void **data, size_t *count) {\n");
    fprintf(f, "    char *val_copy = (char*)malloc(strlen(val) + 1);\n");
    fprintf(f, "    if (!val_copy) return;\n");
    fprintf(f, "    strcpy(val_copy, val);\n");
    fprintf(f, "    char *s = val_copy;\n");
    fprintf(f, "    char *token = strtok(s, \",\");\n");
    fprintf(f, "    size_t n = 0;\n");
    fprintf(f, "    while (token) { n++; token = strtok(NULL, \",\"); }\n");
    fprintf(f, "    free(val_copy);\n");
    fprintf(f, "    *count = n;\n");
    fprintf(f, "    if (n == 0) { *data = NULL; return; }\n");
    fprintf(f, "    *data = cfg_pool_alloc(&ctx->pool, n * sizeof(const char*));\n");
    fprintf(f, "    val_copy = (char*)malloc(strlen(val) + 1);\n");
    fprintf(f, "    if (!val_copy) return;\n");
    fprintf(f, "    strcpy(val_copy, val);\n");
    fprintf(f, "    s = val_copy;\n");
    fprintf(f, "    token = strtok(s, \",\");\n");
    fprintf(f, "    for (size_t i = 0; i < n; i++) {\n");
    fprintf(f, "        while(isspace(*token)) token++;\n");
    fprintf(f, "        char *end = token + strlen(token) - 1;\n");
    fprintf(f, "        while(end > token && isspace(*end)) *end-- = '\\0';\n");
    fprintf(f, "        ((const char**)*data)[i] = cfg_intern_string(ctx, token);\n");
    fprintf(f, "        token = strtok(NULL, \",\");\n");
    fprintf(f, "    }\n");
    fprintf(f, "    free(val_copy);\n");
    fprintf(f, "}\n\n");

    if (tracker.uses_ipv4) {
        fprintf(f, "static bool cfg_parse_ipv4(const char *s, cfg_ipv4_t *out) {\n");
        fprintf(f, "    int octets[4];\n");
        fprintf(f, "    if (sscanf(s, \"%%d.%%d.%%d.%%d\", &octets[0], &octets[1], &octets[2], &octets[3]) != 4) return false;\n");
        fprintf(f, "    for (int i = 0; i < 4; i++) { if (octets[i] < 0 || octets[i] > 255) return false; out->octets[i] = (uint8_t)octets[i]; }\n");
        fprintf(f, "    return true;\n");
        fprintf(f, "}\n\n");
    }

    fprintf(f, "static void cfg_set_error(cfg_error_t *err, const char *msg, const char *field, size_t line) {\n");
    fprintf(f, "    if (!err) return;\n");
    fprintf(f, "    if (msg) { strncpy(err->message, msg, sizeof(err->message) - 1); err->message[sizeof(err->message) - 1] = '\\0'; }\n");
    fprintf(f, "    if (field) { strncpy(err->field, field, sizeof(err->field) - 1); err->field[sizeof(err->field) - 1] = '\\0'; }\n");
    fprintf(f, "    err->line = line;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "typedef void (*cfg_ini_cb)(void *user, const char *sec, const char *key, const char *val);\n");
    fprintf(f, "static cfg_status_t cfg_parse_ini(const char *filename, cfg_ini_cb cb, void *user, cfg_error_t *err) {\n");
    fprintf(f, "    FILE *f = fopen(filename, \"r\"); if (!f) { cfg_set_error(err, \"failed to open file\", filename, 0); return CFG_ERR_OPEN_FILE; }\n");
    fprintf(f, "    char line[4096], section[256] = \"\";\n");
    fprintf(f, "    size_t line_num = 0; bool success = true;\n");
    fprintf(f, "    while (fgets(line, sizeof(line), f)) {\n");
    fprintf(f, "        line_num++; char *p = line; while(isspace(*p)) p++;\n");
    fprintf(f, "        if (*p == '\\0' || *p == ';' || *p == '#') continue;\n");
    fprintf(f, "        if (*p == '[') {\n");
    fprintf(f, "            char *end = strchr(p, ']');\n");
    fprintf(f, "            if (end) {\n");
    fprintf(f, "                size_t len = end - (p + 1);\n");
    fprintf(f, "                if (len >= sizeof(section)) len = sizeof(section) - 1;\n");
    fprintf(f, "                strncpy(section, p + 1, len);\n");
    fprintf(f, "                section[len] = '\\0';\n");
    fprintf(f, "            } else { cfg_set_error(err, \"missing closing bracket for section\", section, line_num); success = false; }\n");
    fprintf(f, "        } else {\n");
    fprintf(f, "            char *eq = strchr(p, '=');\n");
    fprintf(f, "            if (eq) {\n");
    fprintf(f, "                *eq = '\\0'; char *key = p, *val = eq + 1;\n");
    fprintf(f, "                char *k_end = key + strlen(key) - 1; while(k_end > key && isspace(*k_end)) *k_end-- = '\\0';\n");
    fprintf(f, "                while(isspace(*val)) val++;\n");
    fprintf(f, "                char *v_end = val + strlen(val) - 1; while(v_end > val && isspace(*v_end)) *v_end-- = '\\0';\n");
    fprintf(f, "                cb(user, section, key, val);\n");
    fprintf(f, "            } else {\n");
    fprintf(f, "                cfg_set_error(err, \"missing assignment operator\", p, line_num); success = false;\n");
    fprintf(f, "            }\n");
    fprintf(f, "        }\n");
    fprintf(f, "    }\n");
    fprintf(f, "    fclose(f);\n");
    fprintf(f, "    return success ? CFG_SUCCESS : CFG_ERR_SYNTAX;\n");
    fprintf(f, "}\n\n");

    for (size_t i = 0; i < tracker.regex_patterns->count; i++) {
        emit_regex_validator(f, *(char**)dynarray_get(tracker.regex_patterns, i), (int)i);
    }

    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema = *(AstNode**)dynarray_get(prog->schemas, i);
        const char* schema_name = get_str(schema->data.schema_decl.name);
        
        emit_validation_function(&tracker, ctx, f, schema, schema_name);
        emit_print_function(ctx, f, schema, schema_name);
        emit_recursive_ini_handler(ctx, f, schema, schema_name);

        fprintf(f, "static void %s_ini_handler(void *user, const char *sec, const char *key, const char *val) {\n", schema_name);
        fprintf(f, "    cfg_common_context_t *ctx = (cfg_common_context_t*)user;\n");
        fprintf(f, "    char sec_copy[256];\n");
        fprintf(f, "    strncpy(sec_copy, sec, sizeof(sec_copy) - 1);\n");
        fprintf(f, "    sec_copy[sizeof(sec_copy) - 1] = '\\0';\n");
        fprintf(f, "    char *parts[32]; int num_parts = 0;\n");
        fprintf(f, "    char *token = strtok(sec_copy, \".\");\n");
        fprintf(f, "    while(token && num_parts < 32) { parts[num_parts++] = token; token = strtok(NULL, \".\"); }\n\n");

        fprintf(f, "    if (num_parts == 0 || strcmp(parts[0], \"%s\") != 0) return;\n", schema_name);
        fprintf(f, "    for (int i = 0; i < num_parts - 1; i++) parts[i] = parts[i+1];\n");
        fprintf(f, "    num_parts--;\n\n");

        fprintf(f, "    %s_ini_handler_recursive(ctx, key, val, parts, num_parts, 0);\n", schema_name);
        fprintf(f, "}\n\n");

        fprintf(f, "cfg_status_t %s_load(%s_t *cfg, const char *filename, cfg_error_t *err) {\n", schema_name, schema_name);
        fprintf(f, "    if (!cfg) return CFG_ERR_VALIDATION;\n");
        fprintf(f, "    memset(cfg, 0, sizeof(%s_t));\n", schema_name);
        fprintf(f, "    cfg_common_context_t ctx = { cfg, NULL };\n");
        fprintf(f, "    if (err) memset(err, 0, sizeof(cfg_error_t));\n");
        
        emit_default_initialization_recursive(ctx, f, schema, "cfg->", "&ctx");

        fprintf(f, "    if (filename) {\n");
        fprintf(f, "        cfg_status_t status = cfg_parse_ini(filename, %s_ini_handler, &ctx, err);\n", schema_name);
        fprintf(f, "        if (status != CFG_SUCCESS) { cfg_pool_free(ctx.pool); return status; }\n");
        fprintf(f, "    }\n");

        emit_env_overrides_recursive(ctx, &tracker, f, schema, "cfg->", "&ctx");

        fprintf(f, "    cfg->internal_pool = ctx.pool;\n");
        fprintf(f, "    if (!%s_validate(cfg, err)) { %s_free(cfg); return CFG_ERR_VALIDATION; }\n", schema_name, schema_name);
        fprintf(f, "    return CFG_SUCCESS;\n");
        fprintf(f, "}\n\n");

        fprintf(f, "void %s_free(%s_t *cfg) {\n", schema_name, schema_name);
        fprintf(f, "    if (!cfg) return;\n");
        fprintf(f, "    cfg_pool_free((cfg_pool_node_t*)cfg->internal_pool);\n");
        fprintf(f, "    memset(cfg, 0, sizeof(%s_t));\n", schema_name);
        fprintf(f, "}\n\n");
    }

    fprintf(f, "#endif /* CONFIG_IMPLEMENTATION */\n");

    fclose(f);
    return true;
}
