#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "datastructures/arena.h"
#include "datastructures/dense_interner.h"
#include "datastructures/hash_map.h"

typedef enum {
    TYPE_PRIMITIVE,
    TYPE_ARRAY,
    TYPE_ENUM,
    TYPE_SCHEMA,
    TYPE_SECTION,
    TYPE_UNKNOWN
} TypeKind;

typedef enum {
    PRIM_INT,
    PRIM_FLOAT,
    PRIM_BOOL,
    PRIM_STRING,
    PRIM_IPV4,
    PRIM_PATH,
    PRIM_VOID
} PrimitiveKind;

typedef struct Type Type;

struct Type {
    TypeKind kind;
    union {
        PrimitiveKind primitive;

        struct {
            Type *element_type;
        } array;

        struct {
            InternResult *name;
            DynArray *members;
        } enum_type;

        struct {
            InternResult *name;
        } schema;

        struct {
            InternResult *name;
        } section;
    } data;
};

typedef struct {
    Arena *arena;
    HashMap *primitive_map; /* InternResult* -> Type* */
    
    Type *t_int;
    Type *t_float;
    Type *t_bool;
    Type *t_string;
    Type *t_ipv4;
    Type *t_path;
    Type *t_void;
} TypeStore;

TypeStore* typestore_create(Arena *arena, DenseInterner *keywords);
Type* typestore_get_primitive(TypeStore *ts, InternResult *name);

Type* type_create_array(TypeStore *ts, Type *element_type);
Type* type_create_enum(TypeStore *ts, InternResult *name, DynArray *members);
Type* type_create_schema(TypeStore *ts, InternResult *name);
Type* type_create_section(TypeStore *ts, InternResult *name);

bool types_are_equal(Type *a, Type *b);
const char* type_to_string(Type *type);
