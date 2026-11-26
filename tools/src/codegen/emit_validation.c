#include <string.h>
#include "codegen/codegen_common.h"

static void emit_condition_expression(FILE *f, AstNode *cond_node, const char *prefix) {
    AstCondition *cond = &cond_node->data.condition;
    fprintf(f, "%s%s == ", prefix, get_str(cond->left));
    AstNode *rhs = cond->right;
    switch (rhs->data.literal.type) {
        case INT_LITERAL: fprintf(f, "%lld", rhs->data.literal.value.int_val); break;
        case FLOAT_LITERAL: fprintf(f, "%f", rhs->data.literal.value.float_val); break;
        case BOOL_LITERAL: fprintf(f, "%s", rhs->data.literal.value.bool_val ? "true" : "false"); break;
        case STRING_LITERAL: emit_c_string(f, rhs->data.literal.value.string_val); break;
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
    
    if (field_needs_presence(field)) {
        fprintf(f, "!%s_present.%s", prefix, get_str(name));
    } else if (type->kind == AST_TYPE_ARRAY) {
        fprintf(f, "%s%s.count == 0", prefix, get_str(name));
    } else {
        fprintf(f, "%s%s == NULL", prefix, get_str(name));
    }
}

static void emit_validation_logic(FILE *f, AstNode *field, const char *prefix) {
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
                }
         else if (strcmp(pname, "min_length") == 0) {
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
            AstType *type = &field->data.field_decl.type->data.ast_type;
            if (type->kind == AST_TYPE_ARRAY) {
                fprintf(f, "    for (size_t i = 0; i < %s%s.count; i++) {\n", prefix, fname);
                fprintf(f, "        if (%s%s.data[i] != NULL && !CFG_FILE_EXISTS(%s%s.data[i])) { cfg_set_error(err, \"file does not exist in array\", \"%s\", 0); return false; }\n", prefix, fname, prefix, fname, fname);
                fprintf(f, "    }\n");
            } else {
                fprintf(f, "    if (%s%s != NULL && !CFG_FILE_EXISTS(%s%s)) { cfg_set_error(err, \"file does not exist\", \"%s\", 0); return false; }\n", prefix, fname, prefix, fname, fname);
            }
        } else if (strcmp(pname, "pattern") == 0) {
            fprintf(f, "    if (%s%s != NULL && !cfg_regex_match(", prefix, fname);
            emit_c_string(f, prop->value->data.literal.value.string_val);
            fprintf(f, ", %s%s)) { cfg_set_error(err, \"pattern mismatch\", \"%s\", 0); return false; }\n", prefix, fname, fname);
        } else if (strcmp(pname, "required") == 0 && prop->value->data.literal.value.bool_val) {
            if (can_be_empty(field) || field_needs_presence(field)) {
                fprintf(f, "    if (");
                emit_field_empty_check(f, field, prefix);
                fprintf(f, ") { cfg_set_error(err, \"required field missing\", \"%s\", 0); return false; }\n", fname);
            }
        } else if (strcmp(pname, "required_if") == 0) {
            if (can_be_empty(field) || field_needs_presence(field)) {
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

void emit_validation_function(UsageTracker *tracker, CodegenContext *ctx, FILE *f, AstNode *node, const char *name) {
    DynArray *items = (node->node_type == AST_SCHEMA_DECL) 
        ? node->data.schema_decl.items 
        : node->data.section_decl.items;

    fprintf(f, "bool %s_validate(const %s_t *cfg, cfg_error_t *err) {\n", name, name);
    fprintf(f, "    if (!cfg) return false;\n");
    fprintf(f, "    (void)err;\n");
    
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            emit_validation_logic(f, item, "cfg->");
            AstNode *type_node = item->data.field_decl.type;
            if (type_node->node_type == AST_TYPE && type_node->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                const char *tname = get_str(type_node->data.ast_type.u.primitive.name);
                if (is_schema_name(ctx, tname)) {
                    fprintf(f, "    if (!%s_validate(&cfg->%s, err)) return false;\n", tname, get_str(item->data.field_decl.name));
                }
            } else if (type_node->node_type == AST_TYPE && type_node->data.ast_type.kind == AST_TYPE_ARRAY) {
                AstNode *elem = type_node->data.ast_type.u.array.elem;
                if (elem->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                    const char *elem_tname = get_str(elem->data.ast_type.u.primitive.name);
                    if (is_schema_name(ctx, elem_tname)) {
                        fprintf(f, "    for (size_t i = 0; i < cfg->%s.count; i++) {\n", get_str(item->data.field_decl.name));
                        fprintf(f, "        if (!%s_validate(&cfg->%s.data[i], err)) return false;\n", elem_tname, get_str(item->data.field_decl.name));
                        fprintf(f, "    }\n");
                    }
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

void emit_validation_prototypes(FILE *f, AstNode *node, const char *name) {
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
