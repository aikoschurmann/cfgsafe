#pragma once

#include "datastructures/hash_map.h"
#include "typecheck/type.h"
#include "parser/ast.h"

typedef enum {
    SYMBOL_SCHEMA,
    SYMBOL_SECTION,
    SYMBOL_FIELD,
    SYMBOL_ENUM_MEMBER
} SymbolKind;

typedef struct {
    SymbolKind kind;
    InternResult *name;
    Type *type;
    AstNode *decl; /* Pointer to the AST node for location info */
    bool resolved;
} Symbol;

typedef struct Scope Scope;

struct Scope {
    Scope *parent;
    HashMap *symbols; /* InternResult* -> Symbol* */
};

typedef struct {
    Scope *current;
    Arena *arena;
} SymbolTable;

SymbolTable* symbol_table_create(Arena *arena);
void symbol_table_push_scope(SymbolTable *st);
void symbol_table_pop_scope(SymbolTable *st);

bool symbol_table_define(SymbolTable *st, Symbol *symbol);
Symbol* symbol_table_lookup(SymbolTable *st, InternResult *name);
Symbol* symbol_table_lookup_local(SymbolTable *st, InternResult *name);
