#include <stdio.h>
#include <stdlib.h>

#include "lexer/lexer.h"
#include "datastructures/arena.h"

int main(int argc, char const *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file.schema>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    FILE *f = fopen(filename, "rb");
    
    if (!f) {
        fprintf(stderr, "Error: Could not open schema file at %s\n", filename);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = malloc(fsize + 1);
    if (!source) {
        fprintf(stderr, "Error: Memory allocation failed for source buffer\n");
        fclose(f);
        return 1;
    }
    
    fread(source, 1, fsize, f);
    fclose(f);
    source[fsize] = '\0';


    Arena *lex_arena = arena_create(4096); 
    Lexer *lexer = lexer_create(source, fsize, lex_arena);

    printf("--- Lexing: %s ---\n", filename);

    if (!lexer_lex_all(lexer)) {
        fprintf(stderr, "Lexing failed during processing!\n");
        free(source);
        arena_destroy(lex_arena);
        return 1;
    }

    size_t token_count = 0;
    Token *tokens = lexer_get_tokens(lexer, &token_count);

    for (size_t i = 0; i < token_count; i++) {
        print_token(&tokens[i]); 
    }

    lexer_destroy(lexer);
    arena_destroy(lex_arena);
    free(source);

    return 0;
}