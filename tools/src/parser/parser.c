#include "parser/parser.h"
#include <stdio.h>
#include <string.h>
#include "lexer/lexer.h"

Parser *parser_create(DynArray *tokens, char *filename, Arena *arena) {
    if (!arena) return NULL;

    Parser *p = arena_alloc(arena, sizeof(Parser));
    if (!p) return NULL;

    p->tokens = tokens;
    p->current = 0;
    p->end = tokens->count;
    p->arena = arena;
    p->filename = filename; /* assume already in arena */
   
    return p;
}

void parser_free(Parser *parser) {
    /* parser itself is owned by arena if created via parser_create */
    (void)parser;
}

Token *current_token(Parser *p) {
    if (!p) return NULL;
    if (p->current >= p->end) return NULL;
    return (Token*)dynarray_get(p->tokens, p->current);
}

Token *peek(Parser *p, size_t offset) {
    if (!p) return NULL;
    size_t index = p->current + offset;
    if (index >= p->end) return NULL;
    return (Token*)dynarray_get(p->tokens, index);
}

Token *parser_advance(Parser *p) {
    if (!p) return NULL;
    if (p->current >= p->end) return NULL;
    Token *tok = (Token*)dynarray_get(p->tokens, p->current);
    p->current++;
    return tok;
}

Token *consume(Parser *p, TokenType expected) {
    if (!p) return NULL;
    Token *tok = current_token(p);
    if (!tok || tok->type != expected) return NULL;
    p->current++;
    return tok;
}

int parser_match(Parser *p, TokenType expected) {
    if (!p) return 0;
    Token *tok = current_token(p);
    if (!tok || tok->type != expected) return 0;
    p->current++;
    return 1;
}

void create_parse_error(ParseError *err_out, Parser *p, const char *message, Token *token) {
    if (!err_out || !p || !message) return;
    
    /* If an error is already set, don't overwrite it. */
    if (err_out->message) return;

    err_out->p = p;
    err_out->message = (char*)arena_alloc(p->arena, strlen(message) + 1);
    if (err_out->message) strcpy(err_out->message, message);

    Token location_tok;
    if (token) {
        location_tok = *token;
    } else {
        Token *curr = current_token(p);
        if (curr) {
            location_tok = *curr;
        } else {
            // Fallback for EOF or empty token stream
            memset(&location_tok, 0, sizeof(Token));
            location_tok.type = TOKEN_EOF;
        }
    }

    if (err_out->use_prev_token) {
        Token *prev = peek(p, -1);
        if (prev) {
            location_tok = *prev;
            /* Point caret after the previous token */
            location_tok.span.start_col += (size_t)location_tok.slice.len;
            location_tok.span.end_col = location_tok.span.start_col + 1;
        }
    }

    err_out->token = location_tok;
}


#include "file.h"

// === Main error printing ===
void print_parse_error(ParseError *error) {
    if (!error || !error->message) return;

    const char *filename = error->p->filename ? error->p->filename : "<unknown>";

    /* Print the main error header */
    fprintf(stderr, COLOR_RED "error:" COLOR_RESET " %s\n", error->message);

    if (error->token.span.start_line == 0) {
        /* No valid token associated: print a simple location line and return */
        fprintf(stderr, "   %s\n", filename);
        return;
    }

    Token display_tok = error->token;

    /* Print file:line:col using the captured token */
    fprintf(stderr, "   %s:%zu:%zu\n",
            filename,
            display_tok.span.start_line,
            display_tok.span.start_col);

    /* Underline the whole token if it's on a single line */
    if (display_tok.span.start_line == display_tok.span.end_line && 
        display_tok.span.end_col > display_tok.span.start_col) {
        print_source_excerpt_span(filename, 
                                display_tok.span.start_line, 
                                display_tok.span.start_col, 
                                display_tok.span.end_col);
    } else {
            /* Fallback for multi-line tokens or zero-width */
        print_source_excerpt(filename, display_tok.span.start_line, display_tok.span.start_col);
    }
}
