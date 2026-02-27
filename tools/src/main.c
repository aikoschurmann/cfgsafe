#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "parser/parse_statements.h"
#include "typecheck/typecheck.h"
#include "datastructures/arena.h"
#include "datastructures/utils.h"
#include "file.h"

int main(int argc, char const *argv[]) {
    bool print_ast_flag = false;
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ast") == 0 || strcmp(argv[i], "-a") == 0) {
            print_ast_flag = true;
        } else if (argv[i][0] != '-') {
            filename = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [--ast] <config_file.schema>\n", argv[0]);
            return 1;
        }
    }

    if (!filename) {
        fprintf(stderr, "Usage: %s [--ast] <config_file.schema>\n", argv[0]);
        return 1;
    }

    char *source = read_file(filename);
    
    if (!source) {
        return 1;
    }

    Arena *arena = arena_create(4 * 1024 * 1024); 
    Lexer *lexer = lexer_create(source, strlen(source), arena);

    if (!lexer_lex_all(lexer)) {
        fprintf(stderr, COLOR_RED "error:" COLOR_RESET " Lexing failed!\n");
        free_file_content(source);
        arena_destroy(arena);
        return 1;
    }

    Parser *parser = parser_create(lexer->tokens, (char*)filename, arena);
    ParseError err = {0};
    
    AstNode *program = parse_program(parser, &err);

    if (err.message) {
        print_parse_error(&err);
        free_file_content(source);
        arena_destroy(arena);
        return 1;
    }

    // --- Type Checking ---
    TypeStore *store = typestore_create(arena, lexer->identifiers);
    TypeCheckContext *type_ctx = typecheck_context_create(
        arena,
        program,
        store,
        lexer->identifiers,
        lexer->keywords,
        filename
    );

    typecheck_program(type_ctx);

    if (type_ctx->errors.count > 0) {
        print_type_errors(&type_ctx->errors, lexer->identifiers);
        lexer_destroy(lexer);
        arena_destroy(arena);
        free_file_content(source);
        return 1;
    }

    if (print_ast_flag) {
        printf("--- AST for %s ---\n", filename);
        print_ast(program, 0, lexer->keywords, lexer->identifiers, lexer->strings);
    } else {
        printf(COLOR_GREEN "success:" COLOR_RESET " '%s'\n", filename);
    }

    lexer_destroy(lexer);
    arena_destroy(arena);
    free_file_content(source);

    return 0;
}
