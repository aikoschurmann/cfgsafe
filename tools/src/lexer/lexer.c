#include "lexer/lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

#define INITIAL_TOKEN_CAPACITY 256

// Keyword table
static const struct {
    const char *word;
    TokenType type;
} KEYWORDS[] = {
    {"import",  TOKEN_KW_IMPORT},
    {"schema",  TOKEN_KW_SCHEMA},
    {"section", TOKEN_KW_SECTION},
    {"enum",    TOKEN_KW_ENUM},
    {"true",    TOKEN_KW_TRUE},
    {"false",   TOKEN_KW_FALSE},
    {NULL,      TOKEN_UNKNOWN}
};


static inline char lexer_peek(const Lexer *lexer) {
    return lexer->cur < lexer->end ? *lexer->cur : '\0';
}

static inline char lexer_advance(Lexer *lexer) {
    if (lexer->cur >= lexer->end) return '\0';
    char c = *lexer->cur++;
    lexer->pos++;
    if (c == '\n') {
        lexer->line++;
        lexer->col = 1;
    } else {
        lexer->col++;
    }
    return c;
}

static inline bool is_alpha(char c) {
    return c == '_' || isalpha((unsigned char)c);
}

static inline bool is_digit(char c) {
    return isdigit((unsigned char)c);
}

static Slice lexer_make_slice_from_ptrs(const char *start_ptr, const char *end_ptr) {
    return (Slice) { .ptr = (char*)start_ptr, .len = (uint32_t)(end_ptr - start_ptr) };
}

static void lexer_skip_whitespace(Lexer *lexer) {
    while (lexer->cur < lexer->end) {
        char c = *lexer->cur;

        if (isspace((unsigned char)c)) {
            lexer_advance(lexer);
            continue;
        }

        // Line comment // 
        if (c == '/' && (lexer->cur + 1) < lexer->end && *(lexer->cur + 1) == '/') {
            lexer_advance(lexer); // /
            lexer_advance(lexer); // /
            while (lexer->cur < lexer->end && *lexer->cur != '\n') lexer_advance(lexer);
            continue;
        }

        // Block comment /* */
        if (c == '/' && (lexer->cur + 1) < lexer->end && *(lexer->cur + 1) == '*') {
            lexer_advance(lexer); // /
            lexer_advance(lexer); // *
            while (lexer->cur < lexer->end) {
                if (*lexer->cur == '*' && (lexer->cur + 1) < lexer->end && *(lexer->cur + 1) == '/') {
                    lexer_advance(lexer); // *
                    lexer_advance(lexer); // /
                    break;
                }
                lexer_advance(lexer);
            }
            continue;
        }
        break;
    }
}

// Unescape string content into arena
static Slice unescape_string_into_arena(const Slice raw, Arena *arena) {
    if (raw.len == 0) return (Slice){ .ptr = NULL, .len = 0 };
    char *out = arena_alloc(arena, raw.len + 1);
    if (!out) return (Slice){ .ptr = NULL, .len = 0 };
    
    char *w = out;
    const char *r = raw.ptr;
    const char *end = raw.ptr + raw.len;

    while (r < end) {
        if (*r == '\\' && (r + 1) < end) {
            r++;
            switch (*r) {
                case 'n':  *w++ = '\n'; break;
                case 't':  *w++ = '\t'; break;
                case 'r':  *w++ = '\r'; break;
                case '\\': *w++ = '\\'; break;
                case '"':  *w++ = '"';  break;
                default:   *w++ = *r;   break;
            }
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
    return (Slice){ .ptr = out, .len = (uint32_t)(w - out) };
}

// Identifier or keyword: uses intern_peek to avoid insertion on keyword check
static void *lexer_lex_identifier(Lexer *lexer, const char *start_ptr, const char *end_ptr, TokenType *out_type) {
    Slice slice = lexer_make_slice_from_ptrs(start_ptr, end_ptr);

    // Lookup keyword WITHOUT inserting
    InternResult *kwres = intern_peek(lexer->keywords, &slice);
    if (kwres) {
        *out_type = (TokenType)(uintptr_t)kwres->entry->meta;
        return kwres;
    }

    // Not a keyword: intern as identifier 
    InternResult *idres = intern(lexer->identifiers, &slice, NULL);
    *out_type = idres ? TOKEN_IDENTIFIER : TOKEN_UNKNOWN;
    return idres;
}

Lexer* lexer_create(const char *source, size_t source_len, Arena *arena) {
    if (!source || !arena) return NULL;

    Allocator alloc = arena_allocator_create(arena);
    Lexer *lexer = arena_alloc(arena, sizeof(Lexer));
    if (!lexer) return NULL;

    lexer->source = source;
    lexer->source_len = source_len;
    lexer->arena = arena;
    lexer->cur = source;
    lexer->end = source + source_len;
    lexer->line = 1;
    lexer->col = 1;
    lexer->pos = 0;

    // Initialize interners using allocators 
    lexer->keywords = intern_table_create(hashmap_create(32, alloc), alloc, string_copy_func, slice_hash, slice_cmp);
    lexer->identifiers = intern_table_create(hashmap_create(128, alloc), alloc, string_copy_func, slice_hash, slice_cmp);
    lexer->strings = intern_table_create(hashmap_create(64, alloc), alloc, string_copy_func, slice_hash, slice_cmp);

    // Pre-intern keywords
    for (size_t i = 0; KEYWORDS[i].word; i++) {
        Slice slice = { .ptr = (char*)KEYWORDS[i].word, .len = (uint32_t)strlen(KEYWORDS[i].word) };
        intern(lexer->keywords, &slice, (void*)(uintptr_t)KEYWORDS[i].type);
    }

    lexer->tokens = arena_alloc(arena, sizeof(DynArray));
    dynarray_init(lexer->tokens, sizeof(Token), alloc);

    return lexer;
}

void lexer_destroy(Lexer *lexer) {
    if (!lexer) return;
    intern_table_destroy(lexer->keywords, NULL, NULL);
    intern_table_destroy(lexer->identifiers, NULL, NULL);
    intern_table_destroy(lexer->strings, NULL, NULL);
    if (lexer->tokens) {
        dynarray_free(lexer->tokens);
    }
}

Token lexer_next_token(Lexer *lexer) {
    lexer_skip_whitespace(lexer);

    if (lexer_at_end(lexer)) {
        return (Token){ TOKEN_EOF, {lexer->cur, 0}, {lexer->line, lexer->col, lexer->line, lexer->col}, NULL };
    }

    const char *start_ptr = lexer->cur;
    size_t start_line = lexer->line;
    size_t start_col = lexer->col;

    char c = lexer_advance(lexer);
    TokenType type = TOKEN_UNKNOWN;
    void *rec = NULL;

    if (is_alpha(c)) {
        while (is_alpha(lexer_peek(lexer)) || is_digit(lexer_peek(lexer))) lexer_advance(lexer);
        rec = lexer_lex_identifier(lexer, start_ptr, lexer->cur, &type);
    } else if (is_digit(c) || (c == '-' && is_digit(lexer_peek(lexer)))) {
        // Number literal (potentially negative)
        while (is_digit(lexer_peek(lexer))) lexer_advance(lexer);
        if (lexer_peek(lexer) == '.' && is_digit(*(lexer->cur + 1))) {
            lexer_advance(lexer); /* . */
            while (is_digit(lexer_peek(lexer))) lexer_advance(lexer);
            type = TOKEN_FLOAT_LIT;
        } else {
            type = TOKEN_INT_LIT;
        }
    } else if (c == '"') {
        while (lexer->cur < lexer->end && *lexer->cur != '"') {
            if (*lexer->cur == '\\') lexer_advance(lexer);
            lexer_advance(lexer);
        }
        if (lexer->cur < lexer->end) lexer_advance(lexer);
        type = TOKEN_STRING_LIT;
        Slice raw_content = lexer_make_slice_from_ptrs(start_ptr + 1, lexer->cur - 1);
        Slice unescaped = unescape_string_into_arena(raw_content, lexer->arena);
        rec = intern(lexer->strings, &unescaped, NULL);
    } else {
        switch (c) {
            case '{': type = TOKEN_LBRACE;   break;
            case '}': type = TOKEN_RBRACE;   break;
            case '[': type = TOKEN_LBRACKET; break;
            case ']': type = TOKEN_RBRACKET; break;
            case '(': type = TOKEN_LPAREN;   break;
            case ')': type = TOKEN_RPAREN;   break;
            case '<': type = TOKEN_LT;       break;
            case '>': type = TOKEN_GT;       break;
            case ':': type = TOKEN_COLON;    break;
            case ',': type = TOKEN_COMMA;    break;
            case '=':
                if (lexer_peek(lexer) == '=') {
                    lexer_advance(lexer);
                    type = TOKEN_EQ_EQ;
                }
                break;
            case '.':
                if (lexer_peek(lexer) == '.') {
                    lexer_advance(lexer);
                    type = TOKEN_RANGE;
                } else {
                    type = TOKEN_DOT;
                }
                break;
        }
    }

    Span span = { start_line, start_col, lexer->line, lexer->col };
    Slice slice = lexer_make_slice_from_ptrs(start_ptr, lexer->cur);
    return (Token){ type, slice, span, rec };
}

// Helper to add token directly to dynarray
static bool lexer_add_token(Lexer *lexer, const Token *tok) {
    if (!lexer || !tok) return false;
    return dynarray_push_value(lexer->tokens, tok) == 0;
}

bool lexer_lex_all(Lexer *lexer) {
    if (!lexer) return false;
    for (;;) {
        Token token = lexer_next_token(lexer);
        if (!lexer_add_token(lexer, &token)) return false;
        if (token.type == TOKEN_EOF) break;
    }
    return true;
}

Token* lexer_get_tokens(Lexer *lexer, size_t *count) {
    if (!lexer || !lexer->tokens) { if (count) *count = 0; return NULL; }
    if (count) *count = lexer->tokens->count;
    return (Token*)lexer->tokens->data;
}

bool lexer_at_end(const Lexer *lexer) {
    return lexer->cur >= lexer->end;
}

void lexer_reset(Lexer *lexer) {
    lexer->cur = lexer->source;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->col = 1;
    if (lexer->tokens) lexer->tokens->count = 0;
}

const char* token_type_to_string(TokenType type) {
    switch (type) {
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_STRING_LIT: return "STRING_LIT";
        case TOKEN_INT_LIT:    return "INT_LIT";
        case TOKEN_FLOAT_LIT:  return "FLOAT_LIT";
        case TOKEN_KW_IMPORT:  return "IMPORT";
        case TOKEN_KW_SCHEMA:  return "SCHEMA";
        case TOKEN_KW_SECTION: return "SECTION";
        case TOKEN_KW_ENUM:    return "ENUM";
        case TOKEN_KW_TRUE:    return "TRUE";
        case TOKEN_KW_FALSE:   return "FALSE";
        case TOKEN_LBRACE:     return "LBRACE '{'";
        case TOKEN_RBRACE:     return "RBRACE '}'";
        case TOKEN_LBRACKET:   return "LBRACKET '['";
        case TOKEN_RBRACKET:   return "RBRACKET ']'";
        case TOKEN_LPAREN:     return "LPAREN '('";
        case TOKEN_RPAREN:     return "RPAREN ')'";
        case TOKEN_LT:         return "LT '<'";
        case TOKEN_GT:         return "GT '>'";
        case TOKEN_COLON:      return "COLON";
        case TOKEN_COMMA:      return "COMMA";
        case TOKEN_DOT:        return "DOT";
        case TOKEN_EQ_EQ:      return "EQ_EQ '=='";
        case TOKEN_RANGE:      return "RANGE '..'";
        case TOKEN_EOF:        return "EOF";
        default:               return "UNKNOWN";
    }
}

void print_token(const Token *tok) {
    const char *type_str = token_type_to_string(tok->type);
    printf("│ %3zu:%-3zu │ %-13s │ ",
           tok->span.start_line,
           tok->span.start_col,
           type_str);
    if (tok->slice.ptr && tok->slice.len > 0) {
        printf("'%.*s'", (int)tok->slice.len, tok->slice.ptr);
    } else {
        printf("(no-lexeme)");
    }

    printf("\n");
}