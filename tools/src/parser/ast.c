#include "parser/ast.h"
#include <stdio.h>

static const char *node_type_to_string(AstNodeType type) {
    switch (type) {
        case AST_PROGRAM: return "Program";
        case AST_IMPORT_DECL: return "Import";
        case AST_SCHEMA_DECL: return "Schema";
        case AST_SECTION_DECL: return "Section";
        case AST_FIELD_DECL: return "Field";
        case AST_PROPERTY_DECL: return "Property";
        case AST_LITERAL: return "Literal";
        case AST_RANGE_EXPR: return "Range";
        case AST_CONDITION: return "Condition";
        case AST_TYPE: return "Type";
        case AST_ENUM_TYPE: return "Enum";
        default: return "Unknown";
    }
}

AstNode *ast_create_node(AstNodeType type, Arena *arena) {
    if (!arena) return NULL;
    AstNode *node = (AstNode*)arena_calloc(arena, sizeof(AstNode));
    if (!node) return NULL;
    node->node_type = type;
    return node;
}

// Track branches for tree drawing
#define MAX_DEPTH 128
static bool depth_last[MAX_DEPTH];

static void print_tree_prefix(int depth, bool is_last) {
    for (int i = 0; i < depth - 1; i++) {
        printf(COLOR_GRAY "%s   " COLOR_RESET, depth_last[i] ? " " : "│");
    }
    printf(COLOR_GRAY "%s " COLOR_RESET, is_last ? "└──" : "├──");
}

void print_ast_recursive(AstNode *node, int depth, bool is_last, DenseInterner *keywords, DenseInterner *identifiers, DenseInterner *strings) {
    if (!node) return;
    if (depth >= MAX_DEPTH) return;

    if (depth > 0) {
        depth_last[depth - 1] = is_last;
        print_tree_prefix(depth, is_last);
    }

    printf(COLOR_BOLD COLOR_MAGENTA "%s" COLOR_RESET, node_type_to_string(node->node_type));
    printf(COLOR_GRAY " [%zu:%zu-%zu:%zu]" COLOR_RESET, 
           node->span.start_line, node->span.start_col, 
           node->span.end_line, node->span.end_col);
    
    switch (node->node_type) {
        case AST_PROGRAM:
            printf("\n");
            size_t total = 0;
            if (node->data.program.imports) total += node->data.program.imports->count;
            if (node->data.program.schemas) total += node->data.program.schemas->count;
            
            size_t current = 0;
            if (node->data.program.imports) {
                for (size_t i = 0; i < node->data.program.imports->count; i++) {
                    print_ast_recursive(*(AstNode**)dynarray_get(node->data.program.imports, i), depth + 1, ++current == total, keywords, identifiers, strings);
                }
            }
            if (node->data.program.schemas) {
                for (size_t i = 0; i < node->data.program.schemas->count; i++) {
                    print_ast_recursive(*(AstNode**)dynarray_get(node->data.program.schemas, i), depth + 1, ++current == total, keywords, identifiers, strings);
                }
            }
            break;

        case AST_SCHEMA_DECL:
        case AST_SECTION_DECL: {
            InternResult *name = (node->node_type == AST_SCHEMA_DECL) ? node->data.schema_decl.name : node->data.section_decl.name;
            const char *name_str = (name && name->entry) ? interner_get_cstr(identifiers, name->entry->dense_index) : "???";
            printf(" " COLOR_CYAN "'%s'" COLOR_RESET "\n", name_str);
            DynArray *items = (node->node_type == AST_SCHEMA_DECL) ? node->data.schema_decl.items : node->data.section_decl.items;
            if (items) {
                for (size_t i = 0; i < items->count; i++) {
                    print_ast_recursive(*(AstNode**)dynarray_get(items, i), depth + 1, i == items->count - 1, keywords, identifiers, strings);
                }
            }
            break;
        }

        case AST_FIELD_DECL: {
            const char *name_str = (node->data.field_decl.name && node->data.field_decl.name->entry) ? interner_get_cstr(identifiers, node->data.field_decl.name->entry->dense_index) : "???";
            printf(" " COLOR_CYAN "'%s'" COLOR_RESET "\n", name_str);
            
            size_t prop_count = node->data.field_decl.properties ? node->data.field_decl.properties->count : 0;
            print_ast_recursive(node->data.field_decl.type, depth + 1, prop_count == 0, keywords, identifiers, strings);
            
            if (node->data.field_decl.properties) {
                for (size_t i = 0; i < prop_count; i++) {
                    print_ast_recursive(*(AstNode**)dynarray_get(node->data.field_decl.properties, i), depth + 1, i == prop_count - 1, keywords, identifiers, strings);
                }
            }
            break;
        }

        case AST_PROPERTY_DECL: {
            const char *name_str = (node->data.property_decl.name && node->data.property_decl.name->entry) ? interner_get_cstr(identifiers, node->data.property_decl.name->entry->dense_index) : "???";
            printf(" " COLOR_YELLOW "%s" COLOR_RESET ":\n", name_str);
            print_ast_recursive(node->data.property_decl.value, depth + 1, true, keywords, identifiers, strings);
            break;
        }

        case AST_RANGE_EXPR:
            printf("\n");
            print_ast_recursive(node->data.range_expr.min, depth + 1, false, keywords, identifiers, strings);
            print_ast_recursive(node->data.range_expr.max, depth + 1, true, keywords, identifiers, strings);
            break;

        case AST_CONDITION: {
            const char *left_str = (node->data.condition.left && node->data.condition.left->entry) ? interner_get_cstr(identifiers, node->data.condition.left->entry->dense_index) : "???";
            printf(" " COLOR_GRAY "==" COLOR_RESET "\n");
            
            // First child: The identifier
            depth_last[depth] = false; 
            print_tree_prefix(depth + 1, false);
            printf(COLOR_BOLD COLOR_MAGENTA "Identifier" COLOR_RESET " " COLOR_BLUE "%s" COLOR_RESET "\n", left_str);
            
            // Second child: The literal (using recursive call)
            print_ast_recursive(node->data.condition.right, depth + 1, true, keywords, identifiers, strings);
            break;
        }

        case AST_LITERAL:
            switch (node->data.literal.type) {
                case INT_LITERAL: printf(" " COLOR_GREEN "%lld" COLOR_RESET "\n", node->data.literal.value.int_val); break;
                case FLOAT_LITERAL: printf(" " COLOR_GREEN "%f" COLOR_RESET "\n", node->data.literal.value.float_val); break;
                case BOOL_LITERAL: printf(" " COLOR_GREEN "%s" COLOR_RESET "\n", node->data.literal.value.bool_val ? "true" : "false"); break;
                case STRING_LITERAL: {
                    const char *s = (node->data.literal.value.string_val && node->data.literal.value.string_val->entry) ? interner_get_cstr(strings, node->data.literal.value.string_val->entry->dense_index) : "";
                    printf(" " COLOR_GREEN "\"%s\"" COLOR_RESET "\n", s);
                    break;
                }
                case IDENTIFIER_LITERAL: {
                    const char *s = (node->data.literal.value.string_val && node->data.literal.value.string_val->entry) ? interner_get_cstr(identifiers, node->data.literal.value.string_val->entry->dense_index) : "";
                    printf(" " COLOR_BLUE "%s" COLOR_RESET "\n", s);
                    break;
                }
                default: printf("\n"); break;
            }
            break;

        case AST_TYPE:
            switch (node->data.ast_type.kind) {
                case AST_TYPE_PRIMITIVE: {
                    const char *t = (node->data.ast_type.u.primitive.name && node->data.ast_type.u.primitive.name->entry) ? interner_get_cstr(identifiers, node->data.ast_type.u.primitive.name->entry->dense_index) : "???";
                    printf(" " COLOR_BLUE "%s" COLOR_RESET "\n", t);
                    break;
                }
                case AST_TYPE_ARRAY:
                    printf(" []\n");
                    print_ast_recursive(node->data.ast_type.u.array.elem, depth + 1, true, keywords, identifiers, strings);
                    break;
                case AST_TYPE_ENUM:
                    printf("\n");
                    print_ast_recursive(node->data.ast_type.u.enum_type.enum_decl, depth + 1, true, keywords, identifiers, strings);
                    break;
            }
            break;

        case AST_ENUM_TYPE:
            printf("\n");
            if (node->data.enum_type.members) {
                for (size_t i = 0; i < node->data.enum_type.members->count; i++) {
                    InternResult *ir = *(InternResult**)dynarray_get(node->data.enum_type.members, i);
                    const char *m = (ir && ir->entry) ? interner_get_cstr(identifiers, ir->entry->dense_index) : "???";
                    print_tree_prefix(depth + 1, i == node->data.enum_type.members->count - 1);
                    printf(COLOR_CYAN "%s" COLOR_RESET "\n", m);
                }
            }
            break;

        case AST_IMPORT_DECL: {
            const char *s = (node->data.import_decl.path && node->data.import_decl.path->entry) ? interner_get_cstr(strings, node->data.import_decl.path->entry->dense_index) : "";
            printf(" " COLOR_GREEN "\"%s\"" COLOR_RESET "\n", s);
            break;
        }

        default:
            printf("\n");
            break;
    }
}

void print_ast(AstNode *node, int depth, DenseInterner *keywords, DenseInterner *identifiers, DenseInterner *strings) {
    print_ast_recursive(node, 0, true, keywords, identifiers, strings);
}
