#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "codegen/codegen.h"
#include "datastructures/utils.h"
#include "codegen/codegen_common.h"

CodegenContext* codegen_context_create(
    Arena *arena,
    AstNode *program,
    TypeStore *store,
    DenseInterner *identifiers,
    DenseInterner *keywords,
    const char *filename
) {
    CodegenContext *ctx = (CodegenContext*)arena_alloc(arena, sizeof(CodegenContext));
    ctx->arena = arena;
    ctx->program = program;
    ctx->store = store;
    ctx->identifiers = identifiers;
    ctx->keywords = keywords;
    ctx->filename = filename;
    return ctx;
}

static void emit_boilerplate_runtime(CodegenContext *ctx, FILE *f, UsageTracker *tracker) {
    fprintf(f, "typedef struct cfg_pool_node {\n");
    fprintf(f, "    void *ptr;\n");
    fprintf(f, "    struct cfg_pool_node *next;\n");
    fprintf(f, "} cfg_pool_node_t;\n\n");
    
    fprintf(f, "static void* cfg_pool_alloc(cfg_pool_node_t **head, size_t size) {\n");
    fprintf(f, "    void *ptr = malloc(size);\n");
    fprintf(f, "    if (!ptr) return NULL;\n");
    fprintf(f, "    cfg_pool_node_t *node = (cfg_pool_node_t*)malloc(sizeof(cfg_pool_node_t));\n");
    fprintf(f, "    if (!node) { free(ptr); return NULL; }\n");
    fprintf(f, "    node->ptr = ptr;\n");
    fprintf(f, "    node->next = *head;\n");
    fprintf(f, "    *head = node;\n");
    fprintf(f, "    return ptr;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static void cfg_pool_free(cfg_pool_node_t *head) {\n");
    fprintf(f, "    while (head) {\n");
    fprintf(f, "        cfg_pool_node_t *next = head->next;\n");
    fprintf(f, "        free(head->ptr);\n");
    fprintf(f, "        free(head);\n");
    fprintf(f, "        head = next;\n");
    fprintf(f, "    }\n");
    fprintf(f, "}\n\n");

    fprintf(f, "typedef struct {\n    void *cfg;\n    cfg_pool_node_t *pool;\n} cfg_common_context_t;\n\n");

    fprintf(f, "static const char* cfg_intern_string(cfg_common_context_t *ctx, const char *s) {\n");
    fprintf(f, "    if (!s) return NULL;\n");
    fprintf(f, "    char *copy = (char*)cfg_pool_alloc(&ctx->pool, strlen(s) + 1);\n");
    fprintf(f, "    if (copy) strcpy(copy, s);\n");
    fprintf(f, "    return copy;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static void cfg_parse_array(cfg_common_context_t *ctx, const char *val, void **data, size_t *count) {\n");
    fprintf(f, "    char *val_copy = (char*)malloc(strlen(val) + 1);\n");
    fprintf(f, "    if (!val_copy) return;\n");
    fprintf(f, "    strcpy(val_copy, val);\n");
    fprintf(f, "    char *s = val_copy;\n");
    fprintf(f, "    char *token = strtok(s, \",\");\n");
    fprintf(f, "    size_t n = 0;\n");
    fprintf(f, "    while (token) { n++; token = strtok(NULL, \",\"); }\n");
    fprintf(f, "    free(val_copy);\n");
    fprintf(f, "    *count = n;\n");
    fprintf(f, "    if (n == 0) { *data = NULL; return; }\n");
    fprintf(f, "    *data = cfg_pool_alloc(&ctx->pool, n * sizeof(const char*));\n");
    fprintf(f, "    val_copy = (char*)malloc(strlen(val) + 1);\n");
    fprintf(f, "    if (!val_copy) return;\n");
    fprintf(f, "    strcpy(val_copy, val);\n");
    fprintf(f, "    s = val_copy;\n");
    fprintf(f, "    token = strtok(s, \",\");\n");
    fprintf(f, "    for (size_t i = 0; i < n; i++) {\n");
    fprintf(f, "        while(isspace(*token)) token++;\n");
    fprintf(f, "        char *end = token + strlen(token) - 1;\n");
    fprintf(f, "        while(end > token && isspace(*end)) *end-- = '\\0';\n");
    fprintf(f, "        ((const char**)*data)[i] = cfg_intern_string(ctx, token);\n");
    fprintf(f, "        token = strtok(NULL, \",\");\n");
    fprintf(f, "    }\n");
    fprintf(f, "    free(val_copy);\n");
    fprintf(f, "}\n\n");

    if (tracker->uses_ipv4) {
        fprintf(f, "static bool cfg_parse_ipv4(const char *s, cfg_ipv4_t *out) {\n");
        fprintf(f, "    int octets[4];\n");
        fprintf(f, "    if (sscanf(s, \"%%d.%%d.%%d.%%d\", &octets[0], &octets[1], &octets[2], &octets[3]) != 4) return false;\n");
        fprintf(f, "    for (int i = 0; i < 4; i++) { if (octets[i] < 0 || octets[i] > 255) return false; out->octets[i] = (uint8_t)octets[i]; }\n");
        fprintf(f, "    return true;\n");
        fprintf(f, "}\n\n");
    }

    fprintf(f, "static void cfg_set_error(cfg_error_t *err, const char *msg, const char *field, size_t line) {\n");
    fprintf(f, "    if (!err) return;\n");
    fprintf(f, "    if (msg) { strncpy(err->message, msg, sizeof(err->message) - 1); err->message[sizeof(err->message) - 1] = '\\0'; }\n");
    fprintf(f, "    if (field) { strncpy(err->field, field, sizeof(err->field) - 1); err->field[sizeof(err->field) - 1] = '\\0'; }\n");
    fprintf(f, "    err->line = line;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "typedef void (*cfg_ini_cb)(void *user, const char *sec, const char *key, const char *val);\n");
    fprintf(f, "static cfg_status_t cfg_parse_ini(const char *filename, cfg_ini_cb cb, void *user, cfg_error_t *err) {\n");
    fprintf(f, "    FILE *f = fopen(filename, \"r\"); if (!f) { cfg_set_error(err, \"failed to open file\", filename, 0); return CFG_ERR_OPEN_FILE; }\n");
    fprintf(f, "    char line[4096], section[256] = \"\";\n");
    fprintf(f, "    size_t line_num = 0; bool success = true;\n");
    fprintf(f, "    while (fgets(line, sizeof(line), f)) {\n");
    fprintf(f, "        line_num++; char *p = line; while(isspace(*p)) p++;\n");
    fprintf(f, "        if (*p == '\\0' || *p == ';' || *p == '#') continue;\n");
    fprintf(f, "        if (*p == '[') {\n");
    fprintf(f, "            char *end = strchr(p, ']');\n");
    fprintf(f, "            if (end) {\n");
    fprintf(f, "                size_t len = end - (p + 1);\n");
    fprintf(f, "                if (len >= sizeof(section)) len = sizeof(section) - 1;\n");
    fprintf(f, "                strncpy(section, p + 1, len);\n");
    fprintf(f, "                section[len] = '\\0';\n");
    fprintf(f, "            } else { cfg_set_error(err, \"missing closing bracket for section\", section, line_num); success = false; }\n");
    fprintf(f, "        } else {\n");
    fprintf(f, "            char *eq = strchr(p, '=');\n");
    fprintf(f, "            if (eq) {\n");
    fprintf(f, "                *eq = '\\0'; char *key = p, *val = eq + 1;\n");
    fprintf(f, "                char *k_end = key + strlen(key) - 1; while(k_end > key && isspace(*k_end)) *k_end-- = '\\0';\n");
    fprintf(f, "                while(isspace(*val)) val++;\n");
    fprintf(f, "                char *v_end = val + strlen(val) - 1; while(v_end > val && isspace(*v_end)) *v_end-- = '\\0';\n");
    fprintf(f, "                cb(user, section, key, val);\n");
    fprintf(f, "            } else {\n");
    fprintf(f, "                cfg_set_error(err, \"missing assignment operator\", p, line_num); success = false;\n");
    fprintf(f, "            }\n");
    fprintf(f, "        }\n");
    fprintf(f, "    }\n");
    fprintf(f, "    fclose(f);\n");
    fprintf(f, "    return success ? CFG_SUCCESS : CFG_ERR_SYNTAX;\n");
    fprintf(f, "}\n\n");
}

bool codegen_generate_header(CodegenContext *ctx, const char *output_filename) {
    UsageTracker tracker = {0};
    tracker.regex_patterns = allocator_alloc(arena_allocator_create(ctx->arena), sizeof(DynArray));
    dynarray_init(tracker.regex_patterns, sizeof(char*), arena_allocator_create(ctx->arena));
    
    track_usage(ctx->program, &tracker);

    FILE *f = fopen(output_filename, "w");
    if (!f) return false;

    char guard[256];
    const char* last_slash = strrchr(output_filename, '/');
    const char* base_name = last_slash ? last_slash + 1 : output_filename;
    strncpy(guard, base_name, sizeof(guard));
    for (int i = 0; guard[i]; i++) {
        if (guard[i] == '.') guard[i] = '_';
        else guard[i] = toupper(guard[i]);
    }

    fprintf(f, "/* Generated by cfg-gen for %s */\n", ctx->filename);
    fprintf(f, "#ifndef %s\n", guard);
    fprintf(f, "#define %s\n\n", guard);
    
    fprintf(f, "#include <stdio.h>\n");
    if (tracker.uses_int) fprintf(f, "#include <stdint.h>\n");
    if (tracker.uses_bool) fprintf(f, "#include <stdbool.h>\n");
    if (tracker.uses_size_t) fprintf(f, "#include <stddef.h>\n");
    
    fprintf(f, "\ntypedef enum {\n");
    fprintf(f, "    CFG_SUCCESS = 0,\n");
    fprintf(f, "    CFG_ERR_OPEN_FILE,\n");
    fprintf(f, "    CFG_ERR_SYNTAX,\n");
    fprintf(f, "    CFG_ERR_VALIDATION,\n");
    fprintf(f, "} cfg_status_t;\n\n");

    fprintf(f, "typedef struct {\n");
    fprintf(f, "    char message[512];\n");
    fprintf(f, "    char field[256];\n");
    fprintf(f, "    size_t line;\n");
    fprintf(f, "} cfg_error_t;\n\n");

    AstProgram *prog = &ctx->program->data.program;
    if (prog->imports) {
        for (size_t i = 0; i < prog->imports->count; i++) {
            AstNode *imp = *(AstNode**)dynarray_get(prog->imports, i);
            fprintf(f, "#include \"%s\"\n", get_str(imp->data.import_decl.path));
        }
    }

    if (tracker.uses_int || tracker.uses_bool || tracker.uses_size_t || (prog->imports && prog->imports->count > 0)) fprintf(f, "\n");

    if (tracker.uses_ipv4) {
        if (!tracker.uses_int) fprintf(f, "#include <stdint.h>\n");
        fprintf(f, "typedef struct {\n");
        fprintf(f, "    uint8_t octets[4];\n");
        fprintf(f, "} cfg_ipv4_t;\n\n");
    }

    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema = *(AstNode**)dynarray_get(prog->schemas, i);
        const char* schema_name = get_str(schema->data.schema_decl.name);
        fprintf(f, "typedef struct %s_t %s_t;\n", schema_name, schema_name);
    }
    fprintf(f, "\n");

    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema = *(AstNode**)dynarray_get(prog->schemas, i);
        emit_section_or_schema(ctx, f, schema, get_str(schema->data.schema_decl.name));
    }

    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema = *(AstNode**)dynarray_get(prog->schemas, i);
        const char* schema_name = get_str(schema->data.schema_decl.name);
        fprintf(f, "cfg_status_t %s_load(%s_t *cfg, const char *filename, cfg_error_t *err);\n", schema_name, schema_name);
        fprintf(f, "void %s_print(const %s_t *cfg, FILE *f);\n", schema_name, schema_name);
        fprintf(f, "void %s_free(%s_t *cfg);\n", schema_name, schema_name);
        emit_validation_prototypes(f, schema, schema_name);
    }

    fprintf(f, "\n#endif /* %s */\n\n", guard);

    fprintf(f, "#ifdef CONFIG_IMPLEMENTATION\n\n");
    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "#include <stdlib.h>\n");
    fprintf(f, "#include <string.h>\n");
    fprintf(f, "#include <ctype.h>\n");
    
    if (tracker.uses_access) {
        fprintf(f, "\n#ifndef CFG_FILE_EXISTS\n");
        fprintf(f, "#ifdef _WIN32\n");
        fprintf(f, "#include <io.h>\n");
        fprintf(f, "#define CFG_FILE_EXISTS(path) (_access((path), 0) == 0)\n");
        fprintf(f, "#else\n");
        fprintf(f, "#include <unistd.h>\n");
        fprintf(f, "#define CFG_FILE_EXISTS(path) (access((path), F_OK) == 0)\n");
        fprintf(f, "#endif\n");
        fprintf(f, "#endif /* CFG_FILE_EXISTS */\n");
    }

    emit_boilerplate_runtime(ctx, f, &tracker);

    for (size_t i = 0; i < tracker.regex_patterns->count; i++) {
        emit_regex_validator(f, *(char**)dynarray_get(tracker.regex_patterns, i), (int)i);
    }

    for (size_t i = 0; i < prog->schemas->count; i++) {
        AstNode *schema = *(AstNode**)dynarray_get(prog->schemas, i);
        const char* schema_name = get_str(schema->data.schema_decl.name);
        
        emit_validation_function(&tracker, ctx, f, schema, schema_name);
        emit_print_function(ctx, f, schema, schema_name);
        emit_recursive_ini_handler(ctx, f, schema, schema_name);

        fprintf(f, "static void %s_ini_handler(void *user, const char *sec, const char *key, const char *val) {\n", schema_name);
        fprintf(f, "    cfg_common_context_t *ctx = (cfg_common_context_t*)user;\n");
        fprintf(f, "    char sec_copy[256];\n");
        fprintf(f, "    strncpy(sec_copy, sec, sizeof(sec_copy) - 1);\n");
        fprintf(f, "    sec_copy[sizeof(sec_copy) - 1] = '\\0';\n");
        fprintf(f, "    char *parts[32]; int num_parts = 0;\n");
        fprintf(f, "    char *token = strtok(sec_copy, \".\");\n");
        fprintf(f, "    while(token && num_parts < 32) { parts[num_parts++] = token; token = strtok(NULL, \".\"); }\n\n");

        fprintf(f, "    if (num_parts == 0 || strcmp(parts[0], \"%s\") != 0) return;\n", schema_name);
        fprintf(f, "    for (int i = 0; i < num_parts - 1; i++) parts[i] = parts[i+1];\n");
        fprintf(f, "    num_parts--;\n\n");

        fprintf(f, "    %s_ini_handler_recursive(ctx, key, val, parts, num_parts, 0);\n", schema_name);
        fprintf(f, "}\n\n");

        fprintf(f, "cfg_status_t %s_load(%s_t *cfg, const char *filename, cfg_error_t *err) {\n", schema_name, schema_name);
        fprintf(f, "    if (!cfg) return CFG_ERR_VALIDATION;\n");
        fprintf(f, "    memset(cfg, 0, sizeof(%s_t));\n", schema_name);
        fprintf(f, "    cfg_common_context_t ctx = { cfg, NULL };\n");
        fprintf(f, "    if (err) memset(err, 0, sizeof(cfg_error_t));\n");
        
        emit_default_initialization_recursive(ctx, f, schema, "cfg->", "&ctx");

        fprintf(f, "    if (filename) {\n");
        fprintf(f, "        cfg_status_t status = cfg_parse_ini(filename, %s_ini_handler, &ctx, err);\n", schema_name);
        fprintf(f, "        if (status != CFG_SUCCESS) { cfg_pool_free(ctx.pool); return status; }\n");
        fprintf(f, "    }\n");

        emit_env_overrides_recursive(ctx, &tracker, f, schema, "cfg->", "&ctx");

        fprintf(f, "    cfg->internal_pool = ctx.pool;\n");
        fprintf(f, "    if (!%s_validate(cfg, err)) { %s_free(cfg); return CFG_ERR_VALIDATION; }\n", schema_name, schema_name);
        fprintf(f, "    return CFG_SUCCESS;\n");
        fprintf(f, "}\n\n");

        fprintf(f, "void %s_free(%s_t *cfg) {\n", schema_name, schema_name);
        fprintf(f, "    if (!cfg) return;\n");
        fprintf(f, "    cfg_pool_free((cfg_pool_node_t*)cfg->internal_pool);\n");
        fprintf(f, "    memset(cfg, 0, sizeof(%s_t));\n", schema_name);
        fprintf(f, "}\n\n");
    }

    fprintf(f, "#endif /* CONFIG_IMPLEMENTATION */\n");

    fclose(f);
    return true;
}
