#include <stdlib.h>
#include "typecheck/typecheck.h"
#include "typecheck/properties.h"

TypeCheckContext* typecheck_context_create(
    Arena *arena,
    AstNode *program,
    TypeStore *store,
    DenseInterner *identifiers,
    DenseInterner *keywords,
    const char *filename
) {
    TypeCheckContext *ctx = arena_alloc(arena, sizeof(TypeCheckContext));
    ctx->arena = arena;
    ctx->program = program;
    ctx->store = store;
    ctx->symbols = symbol_table_create(arena);
    ctx->identifiers = identifiers;
    ctx->keywords = keywords;
    ctx->filename = filename;
    dynarray_init(&ctx->errors, sizeof(TypeError*), arena_allocator_create(arena));
    
    ctx->resolving_stack = arena_alloc(arena, sizeof(DynArray));
    dynarray_init(ctx->resolving_stack, sizeof(InternResult*), arena_allocator_create(arena));
    
    return ctx;
}

void typecheck_program(TypeCheckContext *ctx) {
    AstProgram *prog = &ctx->program->data.program;

    // First pass: Resolve all schemas (and potentially enums)
    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema_node = *(AstNode**)dynarray_get(prog->schemas, i);
        AstSchemaDecl *schema = &schema_node->data.schema_decl;

        Symbol *sym = arena_alloc(ctx->arena, sizeof(Symbol));
        sym->kind = SYMBOL_SCHEMA;
        sym->name = schema->name;
        sym->type = type_create_schema(ctx->store, schema->name);
        sym->decl = schema_node;
        sym->resolved = false;

        if (!symbol_table_define(ctx->symbols, sym)) {
            report_type_error(ctx->arena, &ctx->errors, TE_REDECLARATION, schema_node->span, ctx->filename, schema->name);
        }
    }

    // Second pass: Typecheck inside each schema
    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema_node = *(AstNode**)dynarray_get(prog->schemas, i);
        typecheck_schema(ctx, schema_node);
    }
}

static void typecheck_scope_items(TypeCheckContext *ctx, DynArray *items) {
    // 1. First pass: Define all symbols in current scope
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            AstFieldDecl *field = &item->data.field_decl;
            Type *type = resolve_ast_type(ctx, field->type);
            Symbol *sym = arena_alloc(ctx->arena, sizeof(Symbol));
            sym->kind = SYMBOL_FIELD;
            sym->name = field->name;
            sym->type = type;
            sym->decl = item;
            sym->resolved = true;
            if (!symbol_table_define(ctx->symbols, sym)) {
                report_type_error(ctx->arena, &ctx->errors, TE_REDECLARATION, item->span, ctx->filename, field->name);
            }
        } else if (item->node_type == AST_SECTION_DECL) {
            AstSectionDecl *section = &item->data.section_decl;
            Symbol *sym = arena_alloc(ctx->arena, sizeof(Symbol));
            sym->kind = SYMBOL_SECTION;
            sym->name = section->name;
            sym->type = type_create_section(ctx->store, section->name);
            sym->decl = item;
            sym->resolved = true;
            if (!symbol_table_define(ctx->symbols, sym)) {
                report_type_error(ctx->arena, &ctx->errors, TE_REDECLARATION, item->span, ctx->filename, section->name);
            }
        }
    }

    // 2. Second pass: Typecheck properties and recurse into sections
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = *(AstNode**)dynarray_get(items, i);
        if (item->node_type == AST_FIELD_DECL) {
            AstFieldDecl *field = &item->data.field_decl;
            Symbol *sym = symbol_table_lookup_local(ctx->symbols, field->name);
            if (sym) {
                typecheck_field_properties(ctx->arena, &ctx->errors, ctx->filename, ctx->symbols, ctx->store, sym->type, field->properties);
            }
        } else if (item->node_type == AST_SECTION_DECL) {
            typecheck_section(ctx, item);
        }
    }
}

void typecheck_schema(TypeCheckContext *ctx, AstNode *node) {
    AstSchemaDecl *schema = &node->data.schema_decl;
    
    Symbol *sym = symbol_table_lookup(ctx->symbols, schema->name);
    if (sym && sym->resolved) return;

    // Cycle detection
    for (size_t i = 0; i < ctx->resolving_stack->count; i++) {
        if (*(InternResult**)dynarray_get(ctx->resolving_stack, i) == schema->name) {
            report_type_error(ctx->arena, &ctx->errors, TE_CIRCULAR_DEPENDENCY, node->span, ctx->filename, schema->name);
            return;
        }
    }
    dynarray_push_ptr(ctx->resolving_stack, schema->name);

    symbol_table_push_scope(ctx->symbols);
    typecheck_scope_items(ctx, schema->items);
    symbol_table_pop_scope(ctx->symbols);

    if (sym) sym->resolved = true;
    dynarray_pop(ctx->resolving_stack);
}

void typecheck_section(TypeCheckContext *ctx, AstNode *node) {
    AstSectionDecl *section = &node->data.section_decl;
    symbol_table_push_scope(ctx->symbols);
    typecheck_scope_items(ctx, section->items);
    symbol_table_pop_scope(ctx->symbols);
}

void typecheck_field(TypeCheckContext *ctx, AstNode *node) {
    (void)ctx; (void)node;
}

Type* resolve_ast_type(TypeCheckContext *ctx, AstNode *type_node) {
    AstType *ast_type = &type_node->data.ast_type;
    
    switch (ast_type->kind) {
        case AST_TYPE_PRIMITIVE: {
            Type *prim = typestore_get_primitive(ctx->store, ast_type->u.primitive.name);
            if (!prim) {
                // Check if it's a schema reference
                Symbol *sym = symbol_table_lookup(ctx->symbols, ast_type->u.primitive.name);
                if (sym && sym->kind == SYMBOL_SCHEMA) {
                    typecheck_schema(ctx, sym->decl);
                    return sym->type;
                }
                report_type_error(ctx->arena, &ctx->errors, TE_UNKNOWN_TYPE, type_node->span, ctx->filename, ast_type->u.primitive.name);
                return ctx->store->t_void; // Fallback
            }
            return prim;
        }
        case AST_TYPE_ARRAY: {
            Type *elem = resolve_ast_type(ctx, ast_type->u.array.elem);
            return type_create_array(ctx->store, elem);
        }
        case AST_TYPE_ENUM: {
            AstNode *enum_decl = ast_type->u.enum_type.enum_decl;
            AstEnumType *enum_type = &enum_decl->data.enum_type;
            return type_create_enum(ctx->store, NULL, enum_type->members);
        }
    }
    return ctx->store->t_void;
}
