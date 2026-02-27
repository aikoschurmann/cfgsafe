#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "typecheck/properties.h"
#include "typecheck/type_report.h"

// Define property sets for different types
static const PropertyDef GENERIC_PROPS[] = {
    {"default", PROP_VAL_LITERAL, -1}, // -1 means matches field type
    {"required", PROP_VAL_LITERAL, BOOL_LITERAL},
    {"required_if", PROP_VAL_CONDITION, -1},
    {"env", PROP_VAL_LITERAL, STRING_LITERAL},
};

static const PropertyDef NUMERIC_PROPS[] = {
    {"range", PROP_VAL_RANGE, -1},
    {"min", PROP_VAL_LITERAL, -1},
    {"max", PROP_VAL_LITERAL, -1},
};

static const PropertyDef STRING_PROPS[] = {
    {"min_length", PROP_VAL_LITERAL, INT_LITERAL},
    {"max_length", PROP_VAL_LITERAL, INT_LITERAL},
    {"pattern", PROP_VAL_LITERAL, STRING_LITERAL},
};

static const PropertyDef PATH_PROPS[] = {
    {"exists", PROP_VAL_LITERAL, BOOL_LITERAL},
};

static const PropertyDef ARRAY_PROPS[] = {
    {"min_length", PROP_VAL_LITERAL, INT_LITERAL},
    {"max_length", PROP_VAL_LITERAL, INT_LITERAL},
    {"required", PROP_VAL_LITERAL, BOOL_LITERAL},
    {"required_if", PROP_VAL_CONDITION, -1},
};

static bool is_property_valid_for_type(Type *type, const char *prop_name, PropertyDef *out_def) {
    for (size_t i = 0; i < sizeof(GENERIC_PROPS) / sizeof(GENERIC_PROPS[0]); i++) {
        if (strcmp(GENERIC_PROPS[i].name, prop_name) == 0) {
            *out_def = GENERIC_PROPS[i];
            return true;
        }
    }

    if (type->kind == TYPE_PRIMITIVE) {
        switch (type->data.primitive) {
            case PRIM_INT:
            case PRIM_FLOAT:
                for (size_t i = 0; i < sizeof(NUMERIC_PROPS) / sizeof(NUMERIC_PROPS[0]); i++) {
                    if (strcmp(NUMERIC_PROPS[i].name, prop_name) == 0) {
                        *out_def = NUMERIC_PROPS[i];
                        return true;
                    }
                }
                break;
            case PRIM_STRING:
                for (size_t i = 0; i < sizeof(STRING_PROPS) / sizeof(STRING_PROPS[0]); i++) {
                    if (strcmp(STRING_PROPS[i].name, prop_name) == 0) {
                        *out_def = STRING_PROPS[i];
                        return true;
                    }
                }
                break;
            case PRIM_PATH:
                for (size_t i = 0; i < sizeof(PATH_PROPS) / sizeof(PATH_PROPS[0]); i++) {
                    if (strcmp(PATH_PROPS[i].name, prop_name) == 0) {
                        *out_def = PATH_PROPS[i];
                        return true;
                    }
                }
                break;
            default: break;
        }
    } else if (type->kind == TYPE_ARRAY) {
        for (size_t i = 0; i < sizeof(ARRAY_PROPS) / sizeof(ARRAY_PROPS[0]); i++) {
            if (strcmp(ARRAY_PROPS[i].name, prop_name) == 0) {
                *out_def = ARRAY_PROPS[i];
                return true;
            }
        }
    }

    return false;
}

static const char* literal_type_to_string(LiteralType type) {
    switch (type) {
        case INT_LITERAL: return "int";
        case FLOAT_LITERAL: return "float";
        case BOOL_LITERAL: return "bool";
        case STRING_LITERAL: return "string";
        case IDENTIFIER_LITERAL: return "identifier";
        default: return "unknown";
    }
}

static bool is_valid_ipv4(const char *s) {
    int dots = 0;
    int num = 0;
    int has_num = 0;
    while (*s) {
        if (isdigit(*s)) {
            num = num * 10 + (*s - '0');
            if (num > 255) return false;
            has_num = 1;
        } else if (*s == '.') {
            if (!has_num) return false;
            dots++;
            num = 0;
            has_num = 0;
        } else {
            return false;
        }
        s++;
    }
    return dots == 3 && has_num;
}

static bool is_valid_env_name(const char *s) {
    if (!s || !*s) return false;
    if (isdigit(*s)) return false;
    while (*s) {
        if (!isalnum(*s) && *s != '_') return false;
        s++;
    }
    return true;
}

static bool check_enum_member(Arena *arena, Type *field_type, AstNode *literal, char **out_err) {
    if (field_type->kind != TYPE_ENUM) return true;
    if (literal->data.literal.type != IDENTIFIER_LITERAL) return false;

    InternResult *val = literal->data.literal.value.string_val;
    DynArray *members = field_type->data.enum_type.members;
    for (size_t i = 0; i < members->count; i++) {
        InternResult *member = *(InternResult**)dynarray_get(members, i);
        if (member == val) return true;
    }

    size_t len = 128;
    *out_err = arena_alloc(arena, len);
    snprintf(*out_err, len, "'%.*s' is not a valid member of this enum", (int)((Slice*)val->key)->len, (char*)((Slice*)val->key)->ptr);
    return false;
}

static bool check_value_matches(Arena *arena, DynArray *errors, const char *filename, SymbolTable *symbols, TypeStore *store, Type *field_type, AstNode *value_node, PropertyDef def, char **out_err, const char *prop_name) {
    switch (def.value_kind) {
        case PROP_VAL_LITERAL: {
            if (value_node->node_type != AST_LITERAL) {
                *out_err = "expected literal value";
                return false;
            }
            LiteralType expected_lit = def.literal_type;
            if (expected_lit == (LiteralType)-1) { 
                if (field_type->kind == TYPE_PRIMITIVE) {
                    switch (field_type->data.primitive) {
                        case PRIM_INT:    expected_lit = INT_LITERAL;    break;
                        case PRIM_FLOAT:  expected_lit = FLOAT_LITERAL;  break;
                        case PRIM_BOOL:   expected_lit = BOOL_LITERAL;   break;
                        case PRIM_STRING:
                        case PRIM_PATH:
                        case PRIM_IPV4:   expected_lit = STRING_LITERAL; break;
                        default: return false;
                    }
                } else if (field_type->kind == TYPE_ENUM) {
                    expected_lit = IDENTIFIER_LITERAL;
                }
            }
            
            if (value_node->data.literal.type != expected_lit) {
                size_t len = 64;
                *out_err = arena_alloc(arena, len);
                snprintf(*out_err, len, "expected %s literal", literal_type_to_string(expected_lit));
                return false;
            }

            if (field_type->kind == TYPE_ENUM) {
                return check_enum_member(arena, field_type, value_node, out_err);
            }

            // Semantic format checks
            if (expected_lit == STRING_LITERAL) {
                const char *s = ((Slice*)value_node->data.literal.value.string_val->key)->ptr;
                if (field_type->kind == TYPE_PRIMITIVE && field_type->data.primitive == PRIM_IPV4 && strcmp(prop_name, "default") == 0) {
                    if (!is_valid_ipv4(s)) {
                        *out_err = "invalid IPv4 address format";
                        return false;
                    }
                }
                if (strcmp(prop_name, "env") == 0) {
                    if (!is_valid_env_name(s)) {
                        *out_err = "invalid environment variable name";
                        return false;
                    }
                }
            }

            if (expected_lit == INT_LITERAL) {
                long long val = value_node->data.literal.value.int_val;
                if (strcmp(prop_name, "min_length") == 0 || strcmp(prop_name, "max_length") == 0) {
                    if (val < 0) {
                        *out_err = "length cannot be negative";
                        return false;
                    }
                }
            }

            return true;
        }
        case PROP_VAL_RANGE: {
            if (value_node->node_type != AST_RANGE_EXPR) {
                *out_err = "expected range expression (e.g. 1..10)";
                return false;
            }
            AstRangeExpr *range = &value_node->data.range_expr;
            LiteralType expected = (field_type->kind == TYPE_PRIMITIVE && field_type->data.primitive == PRIM_FLOAT) ? FLOAT_LITERAL : INT_LITERAL;
            
            if (range->min->data.literal.type != expected || range->max->data.literal.type != expected) {
                size_t len = 64;
                *out_err = arena_alloc(arena, len);
                snprintf(*out_err, len, "range bounds must be %s literals", literal_type_to_string(expected));
                return false;
            }

            // min <= max
            if (expected == INT_LITERAL) {
                if (range->min->data.literal.value.int_val > range->max->data.literal.value.int_val) {
                    *out_err = "range min must be less than or equal to max";
                    return false;
                }
            } else {
                if (range->min->data.literal.value.float_val > range->max->data.literal.value.float_val) {
                    *out_err = "range min must be less than or equal to max";
                    return false;
                }
            }

            return true;
        }
        case PROP_VAL_CONDITION: {
            if (value_node->node_type != AST_CONDITION) {
                *out_err = "expected condition (e.g. field == value)";
                return false;
            }
            AstCondition *cond = &value_node->data.condition;
            Symbol *sym = symbol_table_lookup(symbols, cond->left);
            if (!sym) {
                report_type_error(arena, errors, TE_UNDECLARED, value_node->span, filename, cond->left);
                return true; 
            }
            
            LiteralType lit_type = cond->right->data.literal.type;
            bool match = false;
            if (sym->type->kind == TYPE_PRIMITIVE) {
                switch (sym->type->data.primitive) {
                    case PRIM_INT:    match = (lit_type == INT_LITERAL); break;
                    case PRIM_FLOAT:  match = (lit_type == FLOAT_LITERAL); break;
                    case PRIM_BOOL:   match = (lit_type == BOOL_LITERAL); break;
                    case PRIM_STRING:
                    case PRIM_PATH:
                    case PRIM_IPV4:   match = (lit_type == STRING_LITERAL); break;
                    default: break;
                }
            } else if (sym->type->kind == TYPE_ENUM) {
                match = (lit_type == IDENTIFIER_LITERAL);
                if (match) {
                    return check_enum_member(arena, sym->type, cond->right, out_err);
                }
            }

            if (!match) {
                *out_err = "condition identifier type mismatch with literal";
                return false;
            }
            return true;
        }
        default: return true;
    }
}

void typecheck_field_properties(Arena *arena, DynArray *errors, const char *filename, SymbolTable *symbols, TypeStore *store, Type *field_type, DynArray *properties) {
    if (!properties) return;

    for (size_t i = 0; i < properties->count; i++) {
        AstNode *prop_node = *(AstNode**)dynarray_get(properties, i);
        AstPropertyDecl *prop = &prop_node->data.property_decl;
        const char *prop_name_ptr = ((Slice*)prop->name->key)->ptr;
        size_t prop_len = ((Slice*)prop->name->key)->len;

        char buf[128];
        snprintf(buf, sizeof(buf), "%.*s", (int)prop_len, prop_name_ptr);

        // Check for duplicates
        for (size_t j = 0; j < i; j++) {
            AstNode *other_node = *(AstNode**)dynarray_get(properties, j);
            if (other_node->data.property_decl.name == prop->name) {
                report_type_error(arena, errors, TE_REDECLARATION, prop_node->span, filename, prop->name);
                break;
            }
        }

        PropertyDef def;
        if (!is_property_valid_for_type(field_type, buf, &def)) {
            report_type_error(arena, errors, TE_UNEXPECTED_PROPERTY, prop_node->span, filename, prop->name);
            continue;
        }

        char *err_msg = NULL;
        if (!check_value_matches(arena, errors, filename, symbols, store, field_type, prop->value, def, &err_msg, buf)) {
            report_type_error(arena, errors, TE_TYPE_MISMATCH, prop->value->span, filename, NULL, NULL, err_msg);
        }
    }
}
