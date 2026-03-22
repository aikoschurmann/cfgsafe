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

void emit_validation_function(UsageTracker *tracker, CodegenContext *ctx, FILE *f, AstNode *node, const char *name) {
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

void emit_regex_validator(FILE *f, const char *re_str, int idx) {
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
