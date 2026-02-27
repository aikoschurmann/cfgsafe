#include <stdlib.h>
#include "typecheck/symbol_table.h"

SymbolTable* symbol_table_create(Arena *arena) {
    SymbolTable *st = arena_alloc(arena, sizeof(SymbolTable));
    st->arena = arena;
    st->current = NULL;
    symbol_table_push_scope(st);
    return st;
}

void symbol_table_push_scope(SymbolTable *st) {
    Scope *scope = arena_alloc(st->arena, sizeof(Scope));
    scope->parent = st->current;
    scope->symbols = hashmap_create(16, arena_allocator_create(st->arena));
    st->current = scope;
}

void symbol_table_pop_scope(SymbolTable *st) {
    if (st->current) {
        st->current = st->current->parent;
    }
}

bool symbol_table_define(SymbolTable *st, Symbol *symbol) {
    if (hashmap_get(st->current->symbols, symbol->name, ptr_hash, ptr_cmp)) {
        return false;
    }
    hashmap_put(st->current->symbols, symbol->name, symbol, ptr_hash, ptr_cmp);
    return true;
}

Symbol* symbol_table_lookup(SymbolTable *st, InternResult *name) {
    Scope *scope = st->current;
    while (scope) {
        Symbol *symbol = (Symbol*)hashmap_get(scope->symbols, name, ptr_hash, ptr_cmp);
        if (symbol) return symbol;
        scope = scope->parent;
    }
    return NULL;
}

Symbol* symbol_table_lookup_local(SymbolTable *st, InternResult *name) {
    if (!st->current) return NULL;
    return (Symbol*)hashmap_get(st->current->symbols, name, ptr_hash, ptr_cmp);
}
