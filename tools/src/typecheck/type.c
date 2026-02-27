#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "typecheck/type.h"
#include "datastructures/utils.h"

TypeStore* typestore_create(Arena *arena, DenseInterner *keywords) {
    TypeStore *ts = arena_alloc(arena, sizeof(TypeStore));
    ts->arena = arena;
    
    ts->primitive_map = hashmap_create(16, arena_allocator_create(arena));

    struct {
        const char *name;
        PrimitiveKind kind;
        Type **target;
    } primitives[] = {
        {"int",    PRIM_INT,    &ts->t_int},
        {"float",  PRIM_FLOAT,  &ts->t_float},
        {"bool",   PRIM_BOOL,   &ts->t_bool},
        {"string", PRIM_STRING, &ts->t_string},
        {"ipv4",   PRIM_IPV4,   &ts->t_ipv4},
        {"path",   PRIM_PATH,   &ts->t_path},
        {"void",   PRIM_VOID,   &ts->t_void},
    };

    for (int i = 0; i < 7; i++) {
        Slice slice = {(char*)primitives[i].name, (uint32_t)strlen(primitives[i].name)};
        InternResult *interned = intern(keywords, &slice, NULL);
        if (interned) {
            Type *type = arena_alloc(arena, sizeof(Type));
            type->kind = TYPE_PRIMITIVE;
            type->data.primitive = primitives[i].kind;
            *primitives[i].target = type;
            hashmap_put(ts->primitive_map, interned, type, ptr_hash, ptr_cmp);
        }
    }

    return ts;
}

Type* typestore_get_primitive(TypeStore *ts, InternResult *name) {
    return (Type*)hashmap_get(ts->primitive_map, name, ptr_hash, ptr_cmp);
}

Type* type_create_array(TypeStore *ts, Type *element_type) {
    Type *type = arena_alloc(ts->arena, sizeof(Type));
    type->kind = TYPE_ARRAY;
    type->data.array.element_type = element_type;
    return type;
}

Type* type_create_enum(TypeStore *ts, InternResult *name, DynArray *members) {
    Type *type = arena_alloc(ts->arena, sizeof(Type));
    type->kind = TYPE_ENUM;
    type->data.enum_type.name = name;
    type->data.enum_type.members = members;
    return type;
}

Type* type_create_schema(TypeStore *ts, InternResult *name) {
    Type *type = arena_alloc(ts->arena, sizeof(Type));
    type->kind = TYPE_SCHEMA;
    type->data.schema.name = name;
    return type;
}

Type* type_create_section(TypeStore *ts, InternResult *name) {
    Type *type = arena_alloc(ts->arena, sizeof(Type));
    type->kind = TYPE_SECTION;
    type->data.section.name = name;
    return type;
}

bool types_are_equal(Type *a, Type *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case TYPE_PRIMITIVE:
            return a->data.primitive == b->data.primitive;
        case TYPE_ARRAY:
            return types_are_equal(a->data.array.element_type, b->data.array.element_type);
        case TYPE_ENUM:
            return a->data.enum_type.name == b->data.enum_type.name;
        case TYPE_SCHEMA:
            return a->data.schema.name == b->data.schema.name;
        case TYPE_SECTION:
            return a->data.section.name == b->data.section.name;
        case TYPE_UNKNOWN:
            return true;
    }
    return false;
}

const char* type_to_string(Type *type) {
    if (!type) return "unknown";
    switch (type->kind) {
        case TYPE_PRIMITIVE:
            switch (type->data.primitive) {
                case PRIM_INT:    return "int";
                case PRIM_FLOAT:  return "float";
                case PRIM_BOOL:   return "bool";
                case PRIM_STRING: return "string";
                case PRIM_IPV4:   return "ipv4";
                case PRIM_PATH:   return "path";
                case PRIM_VOID:   return "void";
            }
        case TYPE_ARRAY: {
            static char buf[256];
            snprintf(buf, sizeof(buf), "%s[]", type_to_string(type->data.array.element_type));
            return buf;
        }
        case TYPE_ENUM:    return "enum";
        case TYPE_SCHEMA:  return "schema";
        case TYPE_SECTION: return "section";
        case TYPE_UNKNOWN: return "unknown";
    }
    return "unknown";
}
