#include <string.h>
#include "codegen/codegen_common.h"

static AstNode *find_sibling_field(DynArray *items, InternResult *name) {
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL && item->data.field_decl.name == name) return item;
    }
    return NULL;
}

/* Emits a C expression for `field == literal`. The typechecker guarantees the
 * field is a sibling in `items` and the literal matches its type. */
static void emit_condition_expression(FILE *f, AstNode *cond_node, const char *prefix,
                                      const char *parent_name, DynArray *items) {
    AstCondition *cond = &cond_node->data.condition;
    const char *lhs = get_str(cond->left);
    AstNode *rhs = cond->right;
    AstNode *field = find_sibling_field(items, cond->left);
    const char *tname = (field && field->data.field_decl.type->data.ast_type.kind == AST_TYPE_PRIMITIVE)
        ? get_str(field->data.field_decl.type->data.ast_type.u.primitive.name) : "";

    switch (rhs->data.literal.type) {
        case INT_LITERAL: fprintf(f, "%s%s == %lld", prefix, lhs, rhs->data.literal.value.int_val); break;
        case FLOAT_LITERAL: fprintf(f, "%s%s == %.17g", prefix, lhs, rhs->data.literal.value.float_val); break;
        case BOOL_LITERAL: fprintf(f, "%s%s == %s", prefix, lhs, rhs->data.literal.value.bool_val ? "true" : "false"); break;
        case STRING_LITERAL:
            if (strcmp(tname, "ipv4") == 0) {
                /* Typechecked as a valid dotted quad. */
                unsigned o[4] = {0, 0, 0, 0};
                sscanf(get_str(rhs->data.literal.value.string_val), "%u.%u.%u.%u", &o[0], &o[1], &o[2], &o[3]);
                fprintf(f, "(%s%s.octets[0] == %u && %s%s.octets[1] == %u && %s%s.octets[2] == %u && %s%s.octets[3] == %u)",
                        prefix, lhs, o[0], prefix, lhs, o[1], prefix, lhs, o[2], prefix, lhs, o[3]);
            } else {
                fprintf(f, "(%s%s != NULL && strcmp(%s%s, ", prefix, lhs, prefix, lhs);
                emit_c_string(f, rhs->data.literal.value.string_val);
                fprintf(f, ") == 0)");
            }
            break;
        case IDENTIFIER_LITERAL:
            /* Enum constants are named <Parent>_<field>_<MEMBER>. */
            fprintf(f, "%s%s == %s_%s_%s", prefix, lhs, parent_name, lhs, get_str(rhs->data.literal.value.string_val));
            break;
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

static void emit_validation_logic(FILE *f, AstNode *field, const char *prefix, const char *parent_name, DynArray *items) {
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
                fprintf(f, "    if (%s%s < %lld || %s%s > %lld) { CFG_FAIL(\"value out of range\", %s); }\n", 
                    prefix, fname, range->min->data.literal.value.int_val,
                    prefix, fname, range->max->data.literal.value.int_val, fname);
            } else {
                fprintf(f, "    if (%s%s < %.17g || %s%s > %.17g) { CFG_FAIL(\"value out of range\", %s); }\n", 
                    prefix, fname, range->min->data.literal.value.float_val,
                    prefix, fname, range->max->data.literal.value.float_val, fname);
            }
                } else if (strcmp(pname, "min") == 0) {
                    if (prop->value->data.literal.type == INT_LITERAL) {
                        fprintf(f, "    if (%s%s < %lld) { CFG_FAIL(\"value too small\", %s); }\n", prefix, fname, prop->value->data.literal.value.int_val, fname);
                    } else {
                        fprintf(f, "    if (%s%s < %.17g) { CFG_FAIL(\"value too small\", %s); }\n", prefix, fname, prop->value->data.literal.value.float_val, fname);
                    }
                } else if (strcmp(pname, "max") == 0) {
                    if (prop->value->data.literal.type == INT_LITERAL) {
                        fprintf(f, "    if (%s%s > %lld) { CFG_FAIL(\"value too large\", %s); }\n", prefix, fname, prop->value->data.literal.value.int_val, fname);
                    } else {
                        fprintf(f, "    if (%s%s > %.17g) { CFG_FAIL(\"value too large\", %s); }\n", prefix, fname, prop->value->data.literal.value.float_val, fname);
                    }
                }
         else if (strcmp(pname, "min_length") == 0) {
            if (field->data.field_decl.type->data.ast_type.kind == AST_TYPE_ARRAY) {
                fprintf(f, "    if (%s%s.count < %lld) { CFG_FAIL(\"array too short\", %s); }\n", prefix, fname, prop->value->data.literal.value.int_val, fname);
            } else {
                fprintf(f, "    if (%s%s != NULL && strlen(%s%s) < %lld) { CFG_FAIL(\"string too short\", %s); }\n", 
                        prefix, fname, prefix, fname, prop->value->data.literal.value.int_val, fname);
            }
        } else if (strcmp(pname, "max_length") == 0) {
            if (field->data.field_decl.type->data.ast_type.kind == AST_TYPE_ARRAY) {
                fprintf(f, "    if (%s%s.count > %lld) { CFG_FAIL(\"array too long\", %s); }\n", prefix, fname, prop->value->data.literal.value.int_val, fname);
            } else {
                fprintf(f, "    if (%s%s != NULL && strlen(%s%s) > %lld) { CFG_FAIL(\"string too long\", %s); }\n", 
                        prefix, fname, prefix, fname, prop->value->data.literal.value.int_val, fname);
            }
        } else if (strcmp(pname, "exists") == 0 && prop->value->data.literal.value.bool_val) {
            AstType *type = &field->data.field_decl.type->data.ast_type;
            if (type->kind == AST_TYPE_ARRAY) {
                fprintf(f, "    for (size_t i = 0; i < %s%s.count; i++) {\n", prefix, fname);
                fprintf(f, "        if (%s%s.data[i] != NULL && !CFG_FILE_EXISTS(%s%s.data[i])) { CFG_FAIL(\"file does not exist in array\", %s); }\n", prefix, fname, prefix, fname, fname);
                fprintf(f, "    }\n");
            } else {
                fprintf(f, "    if (%s%s != NULL && !CFG_FILE_EXISTS(%s%s)) { CFG_FAIL(\"file does not exist\", %s); }\n", prefix, fname, prefix, fname, fname);
            }
        } else if (strcmp(pname, "pattern") == 0) {
            fprintf(f, "    if (%s%s != NULL && !cfg_regex_match(", prefix, fname);
            emit_c_string(f, prop->value->data.literal.value.string_val);
            fprintf(f, ", %s%s)) { CFG_FAIL(\"pattern mismatch\", %s); }\n", prefix, fname, fname);
        } else if (strcmp(pname, "required") == 0 && prop->value->data.literal.value.bool_val) {
            if (can_be_empty(field) || field_needs_presence(field)) {
                fprintf(f, "    if (");
                emit_field_empty_check(f, field, prefix);
                fprintf(f, ") { CFG_FAIL(\"required field missing\", %s); }\n", fname);
            }
        } else if (strcmp(pname, "required_if") == 0) {
            if (can_be_empty(field) || field_needs_presence(field)) {
                fprintf(f, "    if (");
                emit_condition_expression(f, prop->value, prefix, parent_name, items);
                fprintf(f, ") {\n        if (");
                emit_field_empty_check(f, field, prefix);
                fprintf(f, ") { CFG_FAIL(\"field required by condition missing\", %s); }\n    }\n", fname);
            }
        } else if (strcmp(pname, "hook") == 0) {
            const char *hook_name = get_str(prop->value->data.literal.value.string_val);
            AstType *type = &field->data.field_decl.type->data.ast_type;
            bool is_str = (type->kind == AST_TYPE_PRIMITIVE && 
                          (strcmp(get_str(type->u.primitive.name), "string") == 0 || 
                           strcmp(get_str(type->u.primitive.name), "path") == 0));
            
            /* A hook may set its own message; fill in whatever it left empty. */
            fprintf(f, "    if (!%s(%s%s%s, err)) {\n", hook_name, is_str ? "" : "&", prefix, fname);
            fprintf(f, "        if (err && !err->field[0]) cfg_set_error(err, err->message[0] ? NULL : \"rejected by hook '%s'\", \"%s\", 0);\n", hook_name, fname);
            fprintf(f, "        if (err && !err->line) err->line = cfg_line_of(lctx, &%s%s);\n", prefix, fname);
            fprintf(f, "        return false;\n");
            fprintf(f, "    }\n");
        }
    }
}

void emit_validation_function(UsageTracker *tracker, CodegenContext *ctx, FILE *f, AstNode *node, const char *name) {
    DynArray *items = (node->node_type == AST_SCHEMA_DECL) 
        ? node->data.schema_decl.items 
        : node->data.section_decl.items;

    /* The _ctx variant can look up the INI line that set each field; lctx is
     * NULL when called through the public Name_validate(). */
    fprintf(f, "static bool %s_validate_ctx(const %s_t *cfg, cfg_error_t *err, const cfg_common_context_t *lctx) {\n", name, name);
    fprintf(f, "    if (!cfg) return false;\n");
    fprintf(f, "    (void)err; (void)lctx;\n");
    
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            emit_validation_logic(f, item, "cfg->", name, items);
            AstNode *type_node = item->data.field_decl.type;
            if (type_node->node_type == AST_TYPE && type_node->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                const char *tname = get_str(type_node->data.ast_type.u.primitive.name);
                if (is_schema_name(ctx, tname)) {
                    fprintf(f, "    if (!%s_validate_ctx(&cfg->%s, err, lctx)) { cfg_prefix_field(err, \"%s\", -1); return false; }\n",
                            tname, get_str(item->data.field_decl.name), get_str(item->data.field_decl.name));
                }
            } else if (type_node->node_type == AST_TYPE && type_node->data.ast_type.kind == AST_TYPE_ARRAY) {
                AstNode *elem = type_node->data.ast_type.u.array.elem;
                if (elem->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                    const char *elem_tname = get_str(elem->data.ast_type.u.primitive.name);
                    if (is_schema_name(ctx, elem_tname)) {
                        fprintf(f, "    for (size_t i = 0; i < cfg->%s.count; i++) {\n", get_str(item->data.field_decl.name));
                        fprintf(f, "        if (!%s_validate_ctx(&cfg->%s.data[i], err, lctx)) { cfg_prefix_field(err, \"%s\", (long)i); return false; }\n",
                                elem_tname, get_str(item->data.field_decl.name), get_str(item->data.field_decl.name));
                        fprintf(f, "    }\n");
                    }
                }
            }
        } else if (item->node_type == AST_SECTION_DECL) {
            fprintf(f, "    if (!%s_validate_ctx(&cfg->%s, err, lctx)) { cfg_prefix_field(err, \"%s\", -1); return false; }\n",
                    get_str(item->data.section_decl.name), get_str(item->data.section_decl.name), get_str(item->data.section_decl.name));
        }
    }
    fprintf(f, "    return true;\n}\n\n");
    fprintf(f, "bool %s_validate(const %s_t *cfg, cfg_error_t *err) { return %s_validate_ctx(cfg, err, NULL); }\n\n", name, name, name);

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

void emit_validation_ctx_prototypes(FILE *f, AstNode *node, const char *name) {
    DynArray *items = (node->node_type == AST_SCHEMA_DECL)
        ? node->data.schema_decl.items
        : node->data.section_decl.items;

    fprintf(f, "static bool %s_validate_ctx(const %s_t *cfg, cfg_error_t *err, const cfg_common_context_t *lctx);\n", name, name);
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_SECTION_DECL) {
            emit_validation_ctx_prototypes(f, item, get_str(item->data.section_decl.name));
        }
    }
}
