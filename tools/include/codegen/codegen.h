#pragma once

#include "parser/ast.h"
#include "typecheck/typecheck.h"

typedef struct {
    Arena *arena;
    AstNode *program;
    TypeStore *store;
    DenseInterner *identifiers;
    DenseInterner *keywords;
    const char *filename;
} CodegenContext;

CodegenContext* codegen_context_create(
    Arena *arena,
    AstNode *program,
    TypeStore *store,
    DenseInterner *identifiers,
    DenseInterner *keywords,
    const char *filename
);

bool codegen_generate_header(CodegenContext *ctx, const char *output_filename);
