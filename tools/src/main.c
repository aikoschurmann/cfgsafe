#include <stdio.h>
#include <stdlib.h>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "parser/parse_statements.h"
#include "datastructures/arena.h"
#include "file.h"

int main(int argc, char const *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file.schema>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    char *source = read_file(filename);
    
    if (!source) {
        return 1;
    }

    Arena *arena = arena_create(1024 * 1024); 
    Lexer *lexer = lexer_create(source, strlen(source), arena);

    if (!lexer_lex_all(lexer)) {
        fprintf(stderr, "Lexing failed!\n");
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

    printf("--- AST for %s ---\n", filename);
    print_ast(program, 0, lexer->keywords, lexer->identifiers, lexer->strings);

    lexer_destroy(lexer);
    arena_destroy(arena);
    free_file_content(source);

    return 0;
}
