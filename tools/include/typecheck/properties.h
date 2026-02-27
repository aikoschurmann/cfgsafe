#pragma once

#include "parser/ast.h"
#include "typecheck/type.h"

typedef enum {
    PROP_VAL_LITERAL,
    PROP_VAL_RANGE,
    PROP_VAL_CONDITION,
    PROP_VAL_ANY
} AllowedValueKind;

typedef struct {
    const char *name;
    AllowedValueKind value_kind;
    LiteralType literal_type; /* Only used if value_kind == PROP_VAL_LITERAL. -1 means "matches field type" */
} PropertyDef;

typedef struct {
    const PropertyDef *defs;
    size_t count;
} PropertySet;

#include "typecheck/symbol_table.h"

void typecheck_field_properties(Arena *arena, DynArray *errors, const char *filename, SymbolTable *symbols, TypeStore *store, Type *field_type, DynArray *properties);
