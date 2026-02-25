#include "parser/parse_statements.h"
#include "parser/parser.h"
#include "datastructures/dynamic_array.h"
#include "lexer/lexer.h"
#include "parser/ast.h"
#include <limits.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>

/* Helper: create AST node or set OOM parse error */
static AstNode *new_node_or_err(Parser *p, AstNodeType kind, ParseError *err, const char *oom_msg) {
    AstNode *n = ast_create_node(kind, p->arena);
    if (!n) {
        if (err) create_parse_error(err, p, oom_msg, NULL);
    }
    return n;
}

/* Helper: allocate + init a DynArray in arena, on error sets parse error and returns NULL */
static DynArray *alloc_dynarray(Parser *p, ParseError *err, size_t elem_size, int initial_capacity, const char *oom_msg) {
    DynArray *arr = arena_alloc(p->arena, sizeof(DynArray));
    if (!arr) {
        if (err) create_parse_error(err, p, oom_msg, NULL);
        return NULL;
    }
    Allocator alloc = arena_allocator_create(p->arena);
    dynarray_init(arr, elem_size, alloc);
    if (initial_capacity > 0) {
        if (dynarray_reserve(arr, initial_capacity) != 0) {
            if (err) create_parse_error(err, p, oom_msg, NULL);
            return NULL;
        }
    }
    return arr;
}

static inline bool parse_int_lit(const char *s, size_t len, long long *out) {
    uint64_t val = 0;
    size_t i = 0;
    bool neg = false;
    if (len > 0 && s[0] == '-') {
        neg = true;
        i = 1;
    }
    for (; i < len; i++) {
        unsigned d = (unsigned)(s[i] - '0');
        if (d > 9) return false;
        if (val > (ULLONG_MAX - d) / 10) return false;
        val = val * 10 + d;
    }
    if (neg) {
        if (val > (uint64_t)LLONG_MAX + 1) return false;
        *out = -(long long)val;
    } else {
        if (val > LLONG_MAX) return false;
        *out = (long long)val;
    }
    return true;
}

static inline bool parse_float_lit(const char *s, size_t len, double *out) {
    char *endptr;
    char *buf = malloc(len + 1);
    memcpy(buf, s, len);
    buf[len] = '\0';
    *out = strtod(buf, &endptr);
    bool success = (endptr == buf + len);
    free(buf);
    return success;
}

/* Forward declarations */
static AstNode *parse_import_decl(Parser *p, ParseError *err);
static AstNode *parse_schema_decl(Parser *p, ParseError *err);
static AstNode *parse_section_decl(Parser *p, ParseError *err);
static AstNode *parse_field_decl(Parser *p, ParseError *err);
static AstNode *parse_property_decl(Parser *p, ParseError *err);
static AstNode *parse_type_expr(Parser *p, ParseError *err);
static AstNode *parse_property_value(Parser *p, ParseError *err);
static AstNode *parse_literal(Parser *p, ParseError *err);

/* <Program> ::= { <ImportDecl> } { <SchemaDecl> } */
AstNode *parse_program(Parser *p, ParseError *err) {
    AstNode *prog = new_node_or_err(p, AST_PROGRAM, err, "OOM creating program");
    if (!prog) return NULL;

    prog->data.program.imports = alloc_dynarray(p, err, sizeof(AstNode*), 4, "OOM imports");
    prog->data.program.schemas = alloc_dynarray(p, err, sizeof(AstNode*), 4, "OOM schemas");

    Token *first = current_token(p);
    if (first) prog->span = first->span;

    while (p->current < p->end && parser_match(p, TOKEN_KW_IMPORT)) {
        p->current--; 
        AstNode *imp = parse_import_decl(p, err);
        if (!imp) return NULL;
        dynarray_push_value(prog->data.program.imports, &imp);
        prog->span = span_join(&prog->span, &imp->span);
    }

    while (p->current < p->end) {
        if (parser_match(p, TOKEN_KW_SCHEMA)) {
            p->current--;
            AstNode *schema = parse_schema_decl(p, err);
            if (!schema) return NULL;
            dynarray_push_value(prog->data.program.schemas, &schema);
            prog->span = span_join(&prog->span, &schema->span);
        } else if (current_token(p)->type == TOKEN_EOF) {
            break;
        } else {
            create_parse_error(err, p, "unexpected top-level token", current_token(p));
            return NULL;
        }
    }

    return prog;
}

/* <ImportDecl> ::= "import" STRING_LIT */
static AstNode *parse_import_decl(Parser *p, ParseError *err) {
    Token *start = consume(p, TOKEN_KW_IMPORT);
    if (!start) return NULL;
    Token *path = consume(p, TOKEN_STRING_LIT);
    if (!path) {
        create_parse_error(err, p, "expected string literal after import", current_token(p));
        return NULL;
    }
    AstNode *n = new_node_or_err(p, AST_IMPORT_DECL, err, "OOM import");
    if (!n) return NULL;
    n->data.import_decl.path = path->record;
    n->span = span_join(&start->span, &path->span);
    return n;
}

/* <SchemaDecl> ::= "schema" IDENTIFIER "{" { <BlockItem> } "}" */
static AstNode *parse_schema_decl(Parser *p, ParseError *err) {
    Token *start = consume(p, TOKEN_KW_SCHEMA);
    if (!start) return NULL;
    Token *name = consume(p, TOKEN_IDENTIFIER);
    if (!name) {
        create_parse_error(err, p, "expected schema name", current_token(p));
        return NULL;
    }
    if (!consume(p, TOKEN_LBRACE)) {
        create_parse_error(err, p, "expected '{' after schema name", current_token(p));
        return NULL;
    }

    AstNode *n = new_node_or_err(p, AST_SCHEMA_DECL, err, "OOM schema");
    if (!n) return NULL;
    n->data.schema_decl.name = name->record;
    n->data.schema_decl.items = alloc_dynarray(p, err, sizeof(AstNode*), 8, "OOM schema items");

    while (p->current < p->end && !parser_match(p, TOKEN_RBRACE)) {
        AstNode *item = NULL;
        if (parser_match(p, TOKEN_KW_SECTION)) {
            p->current--;
            item = parse_section_decl(p, err);
        } else {
            item = parse_field_decl(p, err);
        }
        if (!item) return NULL;
        dynarray_push_value(n->data.schema_decl.items, &item);
    }
    
    Token *end = peek(p, -1);
    n->span = span_join(&start->span, &end->span);
    return n;
}

/* <SectionDecl> ::= "section" IDENTIFIER "{" { <BlockItem> } "}" */
static AstNode *parse_section_decl(Parser *p, ParseError *err) {
    Token *start = consume(p, TOKEN_KW_SECTION);
    if (!start) return NULL;
    Token *name = consume(p, TOKEN_IDENTIFIER);
    if (!name) {
        create_parse_error(err, p, "expected section name", current_token(p));
        return NULL;
    }
    if (!consume(p, TOKEN_LBRACE)) {
        create_parse_error(err, p, "expected '{' after section name", current_token(p));
        return NULL;
    }

    AstNode *n = new_node_or_err(p, AST_SECTION_DECL, err, "OOM section");
    if (!n) return NULL;
    n->data.section_decl.name = name->record;
    n->data.section_decl.items = alloc_dynarray(p, err, sizeof(AstNode*), 4, "OOM section items");

    while (p->current < p->end && !parser_match(p, TOKEN_RBRACE)) {
        AstNode *item = NULL;
        if (parser_match(p, TOKEN_KW_SECTION)) {
            p->current--;
            item = parse_section_decl(p, err);
        } else {
            item = parse_field_decl(p, err);
        }
        if (!item) return NULL;
        dynarray_push_value(n->data.section_decl.items, &item);
    }
    
    Token *end = peek(p, -1);
    n->span = span_join(&start->span, &end->span);
    return n;
}

/* <FieldDecl> ::= IDENTIFIER ":" <TypeExpr> "{" { <PropertyDecl> } "}" */
static AstNode *parse_field_decl(Parser *p, ParseError *err) {
    Token *name = consume(p, TOKEN_IDENTIFIER);
    if (!name) {
        create_parse_error(err, p, "expected field name", current_token(p));
        return NULL;
    }

    if (!consume(p, TOKEN_COLON)) {
        create_parse_error(err, p, "expected ':' after field name", current_token(p));
        return NULL;
    }

    AstNode *type = parse_type_expr(p, err);
    if (!type) return NULL;

    if (!consume(p, TOKEN_LBRACE)) {
        create_parse_error(err, p, "expected '{' for property block", current_token(p));
        return NULL;
    }

    AstNode *n = new_node_or_err(p, AST_FIELD_DECL, err, "OOM field");
    if (!n) return NULL;
    n->data.field_decl.name = name->record;
    n->data.field_decl.type = type;
    n->data.field_decl.properties = alloc_dynarray(p, err, sizeof(AstNode*), 4, "OOM properties");

    while (p->current < p->end && !parser_match(p, TOKEN_RBRACE)) {
        AstNode *prop = parse_property_decl(p, err);
        if (!prop) return NULL;
        dynarray_push_value(n->data.field_decl.properties, &prop);
    }
    
    Token *end = peek(p, -1);
    n->span = span_join(&name->span, &end->span);
    return n;
}

/* <PropertyDecl> ::= IDENTIFIER ":" <PropertyValue> */
static AstNode *parse_property_decl(Parser *p, ParseError *err) {
    Token *name = consume(p, TOKEN_IDENTIFIER);
    if (!name) {
        create_parse_error(err, p, "expected property name", current_token(p));
        return NULL;
    }

    if (!consume(p, TOKEN_COLON)) {
        create_parse_error(err, p, "expected ':' after property name", current_token(p));
        return NULL;
    }

    AstNode *val = parse_property_value(p, err);
    if (!val) return NULL;

    AstNode *n = new_node_or_err(p, AST_PROPERTY_DECL, err, "OOM property");
    if (!n) return NULL;
    n->data.property_decl.name = name->record;
    n->data.property_decl.value = val;
    n->span = span_join(&name->span, &val->span);
    return n;
}

/* <TypeExpr> ::= <TypeAtom> [ "[" "]" ] */
static AstNode *parse_type_expr(Parser *p, ParseError *err) {
    AstNode *atom = NULL;
    Token *tok = current_token(p);
    Span start_span;
    
    if (tok && tok->type == TOKEN_KW_ENUM) {
        Token *start = consume(p, TOKEN_KW_ENUM);
        start_span = start->span;
        if (!consume(p, TOKEN_LPAREN)) {
            create_parse_error(err, p, "expected '(' after enum", current_token(p));
            return NULL;
        }
        AstNode *en = new_node_or_err(p, AST_ENUM_TYPE, err, "OOM enum");
        if (!en) return NULL;
        en->data.enum_type.members = alloc_dynarray(p, err, sizeof(InternResult*), 4, "OOM enum members");
        while (1) {
            Token *m = consume(p, TOKEN_IDENTIFIER);
            if (!m) { create_parse_error(err, p, "expected enum member", current_token(p)); return NULL; }
            dynarray_push_value(en->data.enum_type.members, &m->record);
            if (parser_match(p, TOKEN_RPAREN)) break;
            if (!consume(p, TOKEN_COMMA)) { create_parse_error(err, p, "expected ',' or ')'", current_token(p)); return NULL; }
        }
        Token *end = peek(p, -1);
        en->span = span_join(&start_span, &end->span);
        
        atom = new_node_or_err(p, AST_TYPE, err, "OOM type");
        if (!atom) return NULL;
        atom->data.ast_type.kind = AST_TYPE_ENUM;
        atom->data.ast_type.u.enum_type.enum_decl = en;
        atom->span = en->span;
    } else {
        Token *name = consume(p, TOKEN_IDENTIFIER);
        if (!name) { create_parse_error(err, p, "expected type name", current_token(p)); return NULL; }
        atom = new_node_or_err(p, AST_TYPE, err, "OOM type");
        if (!atom) return NULL;
        atom->data.ast_type.kind = AST_TYPE_PRIMITIVE;
        atom->data.ast_type.u.primitive.name = name->record;
        atom->span = name->span;
    }

    if (parser_match(p, TOKEN_LBRACKET)) {
        if (!consume(p, TOKEN_RBRACKET)) { create_parse_error(err, p, "expected ']'", current_token(p)); return NULL; }
        AstNode *arr = new_node_or_err(p, AST_TYPE, err, "OOM array type");
        if (!arr) return NULL;
        arr->data.ast_type.kind = AST_TYPE_ARRAY;
        arr->data.ast_type.u.array.elem = atom;
        Token *end = peek(p, -1);
        arr->span = span_join(&atom->span, &end->span);
        return arr;
    }
    return atom;
}

/* <PropertyValue> ::= <Literal> | <RangeExpr> | <Condition> */
static AstNode *parse_property_value(Parser *p, ParseError *err) {
    Token *t1 = current_token(p);
    
    /* Peek ahead to see if this is a range. */
    if (t1 && (t1->type == TOKEN_INT_LIT || t1->type == TOKEN_FLOAT_LIT)) {
        Token *t2 = peek(p, 1);
        if (t2 && t2->type == TOKEN_RANGE) {
            AstNode *min = parse_literal(p, err);
            consume(p, TOKEN_RANGE);
            AstNode *max = parse_literal(p, err);
            if (!max) return NULL;
            AstNode *n = new_node_or_err(p, AST_RANGE_EXPR, err, "OOM range");
            if (!n) return NULL;
            n->data.range_expr.min = min;
            n->data.range_expr.max = max;
            n->span = span_join(&min->span, &max->span);
            return n;
        }
    }

    /* Condition: IDENTIFIER == LITERAL */
    if (t1 && t1->type == TOKEN_IDENTIFIER) {
        Token *t2 = peek(p, 1);
        if (t2 && t2->type == TOKEN_EQ_EQ) {
            Token *lhs = consume(p, TOKEN_IDENTIFIER);
            consume(p, TOKEN_EQ_EQ);
            AstNode *rhs = parse_literal(p, err);
            if (!rhs) return NULL;
            AstNode *n = new_node_or_err(p, AST_CONDITION, err, "OOM condition");
            if (!n) return NULL;
            n->data.condition.left = lhs->record;
            n->data.condition.right = rhs;
            n->span = span_join(&lhs->span, &rhs->span);
            return n;
        }
    }

    return parse_literal(p, err);
}

/* <Literal> ::= STRING_LIT | INT_LIT | FLOAT_LIT | "true" | "false" | IDENTIFIER */
static AstNode *parse_literal(Parser *p, ParseError *err) {
    Token *tok = current_token(p);
    if (!tok) return NULL;

    AstNode *n = new_node_or_err(p, AST_LITERAL, err, "OOM literal");
    if (!n) return NULL;
    n->span = tok->span;

    switch (tok->type) {
        case TOKEN_INT_LIT:
            n->data.literal.type = INT_LITERAL;
            if (!parse_int_lit(tok->slice.ptr, tok->slice.len, &n->data.literal.value.int_val)) {
                create_parse_error(err, p, "invalid integer literal", tok);
                return NULL;
            }
            break;
        case TOKEN_FLOAT_LIT:
            n->data.literal.type = FLOAT_LITERAL;
            if (!parse_float_lit(tok->slice.ptr, tok->slice.len, &n->data.literal.value.float_val)) {
                create_parse_error(err, p, "invalid float literal", tok);
                return NULL;
            }
            break;
        case TOKEN_STRING_LIT:
            n->data.literal.type = STRING_LITERAL;
            n->data.literal.value.string_val = tok->record;
            break;
        case TOKEN_KW_TRUE:
            n->data.literal.type = BOOL_LITERAL;
            n->data.literal.value.bool_val = 1;
            break;
        case TOKEN_KW_FALSE:
            n->data.literal.type = BOOL_LITERAL;
            n->data.literal.value.bool_val = 0;
            break;
        case TOKEN_IDENTIFIER:
            n->data.literal.type = IDENTIFIER_LITERAL;
            n->data.literal.value.string_val = tok->record;
            break;
        default:
            create_parse_error(err, p, "expected literal", tok);
            return NULL;
    }
    parser_advance(p);
    return n;
}
