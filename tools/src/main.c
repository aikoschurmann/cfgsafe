#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "parser/parse_statements.h"
#include "typecheck/typecheck.h"
#include "codegen/codegen.h"
#include "datastructures/arena.h"
#include "datastructures/utils.h"
#include "file.h"

static void print_usage(const char *prog_name) {
    printf("cfgsafe - type-safe C configuration generator\n\n");
    printf("Usage: %s [options] <input.schema>\n\n", prog_name);
    printf("Options:\n");
    printf("  -o, --output <file>    Set output header filename (default: config.h)\n");
    printf("  -a, --ast              Print the Abstract Syntax Tree instead of generating code\n");
    printf("  -v, --version          Show version information\n");
    printf("  -h, --help             Show this help message\n\n");
    printf("Example:\n");
    printf("  %s -o my_config.h app.schema\n", prog_name);
}

static void print_version() {
    printf("cfgsafe version 0.1.0\n");
}

int main(int argc, char const *argv[]) {
    bool print_ast_flag = false;
    const char *filename = NULL;
    const char *output_filename = "config.h";

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ast") == 0 || strcmp(argv[i], "-a") == 0) {
            print_ast_flag = true;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                output_filename = argv[++i];
            } else {
                fprintf(stderr, COLOR_RED "error:" COLOR_RESET " -o/--output requires a filename\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (argv[i][0] != '-') {
            filename = argv[i];
        } else {
            fprintf(stderr, COLOR_RED "error:" COLOR_RESET " Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!filename) {
        fprintf(stderr, COLOR_RED "error:" COLOR_RESET " No input schema file specified\n");
        print_usage(argv[0]);
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
        CodegenContext *cg_ctx = codegen_context_create(
            arena,
            program,
            store,
            lexer->identifiers,
            lexer->keywords,
            filename
        );

        if (codegen_generate_header(cg_ctx, output_filename)) {
            printf(COLOR_GREEN "success:" COLOR_RESET " Generated '%s'\n", output_filename);
        } else {
            fprintf(stderr, COLOR_RED "error:" COLOR_RESET " Failed to generate '%s'\n", output_filename);
        }
    }

    lexer_destroy(lexer);
    arena_destroy(arena);
    free_file_content(source);

    return 0;
}
