#pragma once

#include <stddef.h>
#include "lexer/lexer.h"
#include "datastructures/dynamic_array.h"
#include "datastructures/dense_interner.h"
#include "datastructures/arena.h"

/* ----------------------- Forward Declarations ----------------------- */
typedef struct AstNode AstNode;

/* ----------------------- AST node kinds ----------------------- */

typedef enum {
    AST_PROGRAM,
    AST_IMPORT_DECL,
    AST_SCHEMA_DECL,
    AST_SECTION_DECL,
    AST_FIELD_DECL,
    AST_PROPERTY_DECL,
    
    AST_LITERAL,
    AST_RANGE_EXPR,
    AST_CONDITION,
    
    AST_TYPE,
    AST_ENUM_TYPE
} AstNodeType;

typedef enum {
    INT_LITERAL,
    FLOAT_LITERAL,
    BOOL_LITERAL,
    STRING_LITERAL,
    IDENTIFIER_LITERAL, /* for enum members or unquoted strings */
    LIT_UNKNOWN
} LiteralType;

typedef struct {
    LiteralType type;
    union {
        long long       int_val;
        double          float_val;
        int             bool_val;
        InternResult   *string_val;
    } value;
} ConstValue;

/* ----------------------- AST payload structs ----------------------- */

typedef struct {
    DynArray *imports; /* AstNode* (AST_IMPORT_DECL) */
    DynArray *schemas; /* AstNode* (AST_SCHEMA_DECL) */
} AstProgram;

typedef struct {
    InternResult *path; /* STRING_LIT */
} AstImportDecl;

typedef struct {
    InternResult *name;
    DynArray *items; /* AstNode* (AST_SECTION_DECL or AST_FIELD_DECL) */
} AstSchemaDecl;

typedef struct {
    InternResult *name;
    DynArray *items; /* AstNode* (AST_SECTION_DECL or AST_FIELD_DECL) */
} AstSectionDecl;

typedef struct {
    InternResult *name;
    AstNode *type;   /* AST_TYPE */
    DynArray *properties; /* AstNode* (AST_PROPERTY_DECL) */
} AstFieldDecl;

typedef struct {
    InternResult *name;
    AstNode *value; /* AST_LITERAL, AST_RANGE_EXPR, or AST_CONDITION */
} AstPropertyDecl;

typedef struct {
    AstNode *min; /* AST_LITERAL */
    AstNode *max; /* AST_LITERAL */
} AstRangeExpr;

typedef struct {
    InternResult *left; /* IDENTIFIER */
    AstNode *right;      /* AST_LITERAL */
} AstCondition;

typedef enum {
    AST_TYPE_PRIMITIVE,
    AST_TYPE_ARRAY,
    AST_TYPE_ENUM
} AstTypeKind;

typedef struct {
    AstTypeKind kind;
    union {
        struct { InternResult *name; } primitive;
        struct { AstNode *elem; } array;
        struct { AstNode *enum_decl; } enum_type;
    } u;
} AstType;

typedef struct {
    DynArray *members; /* InternResult* */
} AstEnumType;

/* ----------------------- AstNode ----------------------- */
struct AstNode {
    AstNodeType node_type;
    Span span;
    
    union {
        AstProgram program;
        AstImportDecl import_decl;
        AstSchemaDecl schema_decl;
        AstSectionDecl section_decl;
        AstFieldDecl field_decl;
        AstPropertyDecl property_decl;
        
        ConstValue literal;
        AstRangeExpr range_expr;
        AstCondition condition;
        
        AstType ast_type;
        AstEnumType enum_type;
    } data;
};

/* ----------------------- Helpers & prototypes ----------------------- */

AstNode *ast_create_node(AstNodeType type, Arena *arena);
void print_ast(AstNode *node, int depth, DenseInterner *keywords, DenseInterner *identifiers, DenseInterner *strings);
