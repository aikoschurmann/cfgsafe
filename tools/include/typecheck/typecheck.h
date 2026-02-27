#pragma once

#include "parser/ast.h"
#include "typecheck/type.h"
#include "typecheck/symbol_table.h"
#include "typecheck/type_report.h"

typedef struct {
    AstNode *program;
    TypeStore *store;
    SymbolTable *symbols;
    
    DenseInterner *identifiers;
    DenseInterner *keywords;
    
    const char *filename;
    DynArray errors; /* DynArray of TypeError* */
    DynArray *resolving_stack; /* DynArray of InternResult* */
    
    Arena *arena;
} TypeCheckContext;

TypeCheckContext* typecheck_context_create(
    Arena *arena,
    AstNode *program,
    TypeStore *store,
    DenseInterner *identifiers,
    DenseInterner *keywords,
    const char *filename
);

void typecheck_program(TypeCheckContext *ctx);
void typecheck_schema(TypeCheckContext *ctx, AstNode *node);
void typecheck_section(TypeCheckContext *ctx, AstNode *node);
void typecheck_field(TypeCheckContext *ctx, AstNode *node);
Type* resolve_ast_type(TypeCheckContext *ctx, AstNode *type_node);
