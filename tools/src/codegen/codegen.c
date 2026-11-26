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
    (void)ctx;
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

    fputs(
        "typedef struct {\n"
        "    void *cfg;\n"
        "    cfg_pool_node_t *pool;\n"
        "    cfg_error_t *err;\n"
        "    size_t line;\n"
        "    cfg_status_t status;\n"
        "} cfg_common_context_t;\n\n"
        "/* Records the first error only; later errors are usually consequences of it. */\n"
        "static void cfg_fail(cfg_common_context_t *ctx, cfg_status_t status, const char *msg, const char *field) {\n"
        "    if (ctx->status != CFG_SUCCESS) return;\n"
        "    ctx->status = status;\n"
        "    cfg_set_error(ctx->err, msg, field, ctx->line);\n"
        "}\n\n"
        "static void cfg_fail_value(cfg_common_context_t *ctx, const char *kind, const char *field, const char *val, const char *origin) {\n"
        "    char msg[512];\n"
        "    if (origin) snprintf(msg, sizeof(msg), \"invalid %s value '%.200s' (from %s)\", kind, val, origin);\n"
        "    else snprintf(msg, sizeof(msg), \"invalid %s value '%.200s'\", kind, val);\n"
        "    cfg_fail(ctx, CFG_ERR_VALIDATION, msg, field);\n"
        "}\n\n", f);

    if (tracker->uses_enum) {
        fputs(
            "static void cfg_fail_enum(cfg_common_context_t *ctx, const char *field, const char *val, const char *allowed, const char *origin) {\n"
            "    char msg[512];\n"
            "    if (origin) snprintf(msg, sizeof(msg), \"invalid value '%.100s' (from %s), expected one of: %.300s\", val, origin, allowed);\n"
            "    else snprintf(msg, sizeof(msg), \"invalid value '%.100s', expected one of: %.300s\", val, allowed);\n"
            "    cfg_fail(ctx, CFG_ERR_VALIDATION, msg, field);\n"
            "}\n\n", f);
    }
    if (tracker->uses_int) {
        fputs(
            "static bool cfg_parse_int(const char *s, int64_t *out) {\n"
            "    char *end;\n"
            "    long long v;\n"
            "    errno = 0;\n"
            "    v = strtoll(s, &end, 10);\n"
            "    if (end == s || errno == ERANGE) return false;\n"
            "    while (isspace((unsigned char)*end)) end++;\n"
            "    if (*end != '\\0') return false;\n"
            "    *out = (int64_t)v;\n"
            "    return true;\n"
            "}\n\n", f);
    }
    if (tracker->uses_float) {
        fputs(
            "static bool cfg_parse_float(const char *s, double *out) {\n"
            "    char *end;\n"
            "    double v;\n"
            "    errno = 0;\n"
            "    v = strtod(s, &end);\n"
            "    if (end == s || errno == ERANGE) return false;\n"
            "    while (isspace((unsigned char)*end)) end++;\n"
            "    if (*end != '\\0') return false;\n"
            "    if (v != v || v - v != 0) return false; /* reject nan and inf */\n"
            "    *out = v;\n"
            "    return true;\n"
            "}\n\n", f);
    }
    if (tracker->uses_bool) {
        fputs(
            "static bool cfg_parse_bool(const char *s, bool *out) {\n"
            "    if (strcmp(s, \"true\") == 0 || strcmp(s, \"1\") == 0) { *out = true; return true; }\n"
            "    if (strcmp(s, \"false\") == 0 || strcmp(s, \"0\") == 0) { *out = false; return true; }\n"
            "    return false;\n"
            "}\n\n", f);
    }

    fprintf(f, "static char* cfg_next_token(char **str, const char *delim) {\n");
    fprintf(f, "    char *s = *str;\n");
    fprintf(f, "    if (!s || *s == '\\0') return NULL;\n");
    fprintf(f, "    s += strspn(s, delim);\n");
    fprintf(f, "    if (*s == '\\0') { *str = s; return NULL; }\n");
    fprintf(f, "    char *token = s;\n");
    fprintf(f, "    s += strcspn(s, delim);\n");
    fprintf(f, "    if (*s != '\\0') { *s = '\\0'; *str = s + 1; }\n");
    fprintf(f, "    else { *str = s; }\n");
    fprintf(f, "    return token;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static const char* cfg_intern_string(cfg_common_context_t *ctx, const char *s) {\n");
    fprintf(f, "    if (!s) return NULL;\n");
    fprintf(f, "    char *copy = (char*)cfg_pool_alloc(&ctx->pool, strlen(s) + 1);\n");
    fprintf(f, "    if (copy) strcpy(copy, s);\n");
    fprintf(f, "    return copy;\n");
    fprintf(f, "}\n\n");

    if (tracker->uses_ipv4) {
        fputs(
            "static bool cfg_parse_ipv4(const char *s, cfg_ipv4_t *out) {\n"
            "    uint8_t octets[4];\n"
            "    for (int i = 0; i < 4; i++) {\n"
            "        int v = 0, digits = 0;\n"
            "        while (*s >= '0' && *s <= '9') {\n"
            "            v = v * 10 + (*s++ - '0');\n"
            "            if (++digits > 3) return false;\n"
            "        }\n"
            "        if (digits == 0 || v > 255) return false;\n"
            "        octets[i] = (uint8_t)v;\n"
            "        if (i < 3 && *s++ != '.') return false;\n"
            "    }\n"
            "    if (*s != '\\0') return false;\n"
            "    memcpy(out->octets, octets, sizeof(octets));\n"
            "    return true;\n"
            "}\n\n", f);
    }


    if (tracker->uses_array) {
        fputs(
            "/* Returns the next comma-separated item, trimmed and with surrounding quotes removed. */\n"
            "static char* cfg_next_item(char **cursor) {\n"
            "    char *tok = cfg_next_token(cursor, \",\");\n"
            "    if (!tok) return NULL;\n"
            "    while (isspace((unsigned char)*tok)) tok++;\n"
            "    char *end = tok + strlen(tok);\n"
            "    while (end > tok && isspace((unsigned char)end[-1])) *--end = '\\0';\n"
            "    if (*tok == '\"' && end - tok >= 2 && end[-1] == '\"') { end[-1] = '\\0'; tok++; }\n"
            "    return tok;\n"
            "}\n\n"
            "typedef bool (*cfg_elem_parser)(cfg_common_context_t *ctx, const char *s, void *out);\n\n"
            "static void cfg_parse_array(cfg_common_context_t *ctx, const char *val, void **data, size_t *count,\n"
            "                            size_t elem_size, cfg_elem_parser parse, const char *kind,\n"
            "                            const char *field, const char *origin) {\n"
            "    size_t len = strlen(val) + 1, n = 0;\n"
            "    char *buf = (char*)malloc(len), *cursor, *item;\n"
            "    *data = NULL;\n"
            "    *count = 0;\n"
            "    if (!buf) { cfg_fail(ctx, CFG_ERR_VALIDATION, \"out of memory\", field); return; }\n"
            "    memcpy(buf, val, len);\n"
            "    cursor = buf;\n"
            "    while (cfg_next_item(&cursor)) n++;\n"
            "    if (n > 0) {\n"
            "        *data = cfg_pool_alloc(&ctx->pool, n * elem_size);\n"
            "        if (!*data) { free(buf); cfg_fail(ctx, CFG_ERR_VALIDATION, \"out of memory\", field); return; }\n"
            "        memcpy(buf, val, len);\n"
            "        cursor = buf;\n"
            "        for (size_t i = 0; (item = cfg_next_item(&cursor)) != NULL; i++) {\n"
            "            if (!parse(ctx, item, (char*)*data + i * elem_size)) {\n"
            "                cfg_fail_value(ctx, kind, field, item, origin);\n"
            "                break;\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "    *count = n;\n"
            "    free(buf);\n"
            "}\n\n", f);
    }
    if (tracker->uses_array_int)
        fputs("static bool cfg_elem_int(cfg_common_context_t *ctx, const char *s, void *out) { (void)ctx; return cfg_parse_int(s, (int64_t*)out); }\n", f);
    if (tracker->uses_array_float)
        fputs("static bool cfg_elem_float(cfg_common_context_t *ctx, const char *s, void *out) { (void)ctx; return cfg_parse_float(s, (double*)out); }\n", f);
    if (tracker->uses_array_bool)
        fputs("static bool cfg_elem_bool(cfg_common_context_t *ctx, const char *s, void *out) { (void)ctx; return cfg_parse_bool(s, (bool*)out); }\n", f);
    if (tracker->uses_array_ipv4)
        fputs("static bool cfg_elem_ipv4(cfg_common_context_t *ctx, const char *s, void *out) { (void)ctx; return cfg_parse_ipv4(s, (cfg_ipv4_t*)out); }\n", f);
    if (tracker->uses_array_string)
        fputs("static bool cfg_elem_string(cfg_common_context_t *ctx, const char *s, void *out) { const char *c = cfg_intern_string(ctx, s); *(const char**)out = c; return c != NULL; }\n", f);
    if (tracker->uses_array) fputs("\n", f);

    fprintf(f, "typedef void (*cfg_ini_cb)(cfg_common_context_t *ctx, const char *sec, const char *key, const char *val);\n");
    fprintf(f, "static cfg_status_t cfg_parse_ini(const char *filename, cfg_ini_cb cb, cfg_common_context_t *ctx) {\n");
    fprintf(f, "    FILE *f = fopen(filename, \"r\"); if (!f) { cfg_fail(ctx, CFG_ERR_OPEN_FILE, \"failed to open file\", filename); return ctx->status; }\n");
    fprintf(f, "    char line[4096], section[256] = \"\";\n");
    fprintf(f, "    while (fgets(line, sizeof(line), f)) {\n");
    fprintf(f, "        ctx->line++; char *p = line; while(isspace(*p)) p++;\n");
    fprintf(f, "        if (*p == '\\0' || *p == ';' || *p == '#') continue;\n");
    fprintf(f, "        if (*p == '[') {\n");
    fprintf(f, "            bool is_array = (*(p+1) == '[');\n");
    fprintf(f, "            if (is_array) p++;\n");
    fprintf(f, "            char *end = is_array ? strstr(p, \"]]\") : strchr(p, ']');\n");
    fprintf(f, "            if (end) {\n");
    fprintf(f, "                size_t len = end - (p + 1);\n");
    fprintf(f, "                if (len >= sizeof(section)) len = sizeof(section) - 1;\n");
    fprintf(f, "                strncpy(section, p + 1, len);\n");
    fprintf(f, "                section[len] = '\\0';\n");
    fprintf(f, "                if (is_array) cb(ctx, section, NULL, NULL);\n");
    fprintf(f, "            } else { cfg_fail(ctx, CFG_ERR_SYNTAX, \"missing closing bracket for section\", section); }\n");
    fprintf(f, "        } else {\n");
    fprintf(f, "            char *eq = strchr(p, '=');\n");
    fprintf(f, "            if (eq) {\n");
    fprintf(f, "                *eq = '\\0'; char *key = p, *val = eq + 1;\n");
    fprintf(f, "                char *k_end = key + strlen(key) - 1; while(k_end > key && isspace(*k_end)) *k_end-- = '\\0';\n");
    fprintf(f, "                while(isspace(*val)) val++;\n");
    fprintf(f, "                \n");
    fprintf(f, "                // Handle quoted values and inline comments\n");
    fprintf(f, "                bool in_quotes = false;\n");
    fprintf(f, "                char *ptr = val;\n");
    fprintf(f, "                char *comment = NULL;\n");
    fprintf(f, "                while (*ptr) {\n");
    fprintf(f, "                    if (*ptr == '\\\"') in_quotes = !in_quotes;\n");
    fprintf(f, "                    else if ((*ptr == ';' || *ptr == '#') && !in_quotes) {\n");
    fprintf(f, "                        comment = ptr;\n");
    fprintf(f, "                        break;\n");
    fprintf(f, "                    }\n");
    fprintf(f, "                    ptr++;\n");
    fprintf(f, "                }\n");
    fprintf(f, "                if (comment) *comment = '\\0';\n");
    fprintf(f, "                \n");
    fprintf(f, "                char *v_end = val + strlen(val) - 1;\n");
    fprintf(f, "                while(v_end >= val && isspace(*v_end)) *v_end-- = '\\0';\n");
    fprintf(f, "                \n");
    fprintf(f, "                if (*val == '\\\"' && v_end >= val && *v_end == '\\\"') {\n");
    fprintf(f, "                    int quote_count = 0;\n");
    fprintf(f, "                    for (char *q = val; *q; q++) if (*q == '\\\"') quote_count++;\n");
    fprintf(f, "                    if (quote_count == 2) {\n");
    fprintf(f, "                        val++;\n");
    fprintf(f, "                        *v_end = '\\0';\n");
    fprintf(f, "                    }\n");
    fprintf(f, "                }\n");
    fprintf(f, "                cb(ctx, section, key, val);\n");
    fprintf(f, "            } else {\n");
    fprintf(f, "                cfg_fail(ctx, CFG_ERR_SYNTAX, \"missing assignment operator\", p);\n");
    fprintf(f, "            }\n");
    fprintf(f, "        }\n");
    fprintf(f, "    }\n");
    fprintf(f, "    fclose(f);\n");
    fprintf(f, "    return ctx->status;\n");
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

    fprintf(f, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
    
    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "#include <string.h>\n");
    fprintf(f, "#include <stdbool.h>\n");
    if (tracker.uses_int) fprintf(f, "#include <stdint.h>\n");
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

    fprintf(f, "static inline void cfg_set_error(cfg_error_t *err, const char *msg, const char *field, size_t line) {\n");
    fprintf(f, "    if (!err) return;\n");
    fprintf(f, "    if (msg) { strncpy(err->message, msg, sizeof(err->message) - 1); err->message[sizeof(err->message) - 1] = '\\0'; }\n");
    fprintf(f, "    if (field) { strncpy(err->field, field, sizeof(err->field) - 1); err->field[sizeof(err->field) - 1] = '\\0'; }\n");
    fprintf(f, "    err->line = line;\n");
    fprintf(f, "}\n\n");

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
        fprintf(f, "cfg_status_t %s_load(%s_t *cfg, const char *filename, int argc, const char **argv, cfg_error_t *err);\n", schema_name, schema_name);
        fprintf(f, "void %s_parse_cli(%s_t *cfg, int argc, const char **argv);\n", schema_name, schema_name);
        fprintf(f, "void %s_print(const %s_t *cfg, FILE *f);\n", schema_name, schema_name);
        fprintf(f, "void %s_free(%s_t *cfg);\n", schema_name, schema_name);
        emit_validation_prototypes(f, schema, schema_name);
    }

    fprintf(f, "\n#ifdef __cplusplus\n}\n#endif\n");
    fprintf(f, "\n#endif /* %s */\n\n", guard);

    fprintf(f, "#ifdef CONFIG_IMPLEMENTATION\n\n");
    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "#include <stdlib.h>\n");
    fprintf(f, "#include <string.h>\n");
    fprintf(f, "#include <ctype.h>\n");
    fprintf(f, "#include <errno.h>\n");

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

        fprintf(f, "static void %s_ini_handler(cfg_common_context_t *ctx, const char *sec, const char *key, const char *val) {\n", schema_name);
        fprintf(f, "    char sec_copy[256];\n");
        fprintf(f, "    strncpy(sec_copy, sec, sizeof(sec_copy) - 1);\n");
        fprintf(f, "    sec_copy[sizeof(sec_copy) - 1] = '\\0';\n");
        fprintf(f, "    char *parts[32]; int num_parts = 0;\n");
        fprintf(f, "    char *s = sec_copy;\n");
        fprintf(f, "    char *token = cfg_next_token(&s, \".\");\n");
        fprintf(f, "    while(token && num_parts < 32) { parts[num_parts++] = token; token = cfg_next_token(&s, \".\"); }\n\n");

        fprintf(f, "    if (num_parts == 0 || strcmp(parts[0], \"%s\") != 0) return;\n", schema_name);
        fprintf(f, "    for (int i = 0; i < num_parts - 1; i++) parts[i] = parts[i+1];\n");
        fprintf(f, "    num_parts--;\n\n");

        fprintf(f, "    %s_ini_handler_recursive(ctx, key, val, parts, num_parts, 0);\n", schema_name);
        fprintf(f, "}\n\n");

        fprintf(f, "cfg_status_t %s_load(%s_t *cfg, const char *filename, int argc, const char **argv, cfg_error_t *err) {\n", schema_name, schema_name);
        fprintf(f, "    if (!cfg) return CFG_ERR_VALIDATION;\n");
        fprintf(f, "    memset(cfg, 0, sizeof(%s_t));\n", schema_name);
        fprintf(f, "    cfg_common_context_t ctx = { cfg, NULL, err, 0, CFG_SUCCESS };\n");
        fprintf(f, "    if (err) memset(err, 0, sizeof(cfg_error_t));\n");
        
        emit_default_initialization_recursive(ctx, f, schema, "cfg->", "&ctx");

        fprintf(f, "    if (filename && cfg_parse_ini(filename, %s_ini_handler, &ctx) != CFG_SUCCESS) {\n", schema_name);
        fprintf(f, "        cfg_pool_free(ctx.pool);\n");
        fprintf(f, "        memset(cfg, 0, sizeof(%s_t));\n", schema_name);
        fprintf(f, "        return ctx.status;\n");
        fprintf(f, "    }\n");
        fprintf(f, "    ctx.line = 0;\n");

        emit_env_overrides_recursive(ctx, &tracker, f, schema, "cfg->", "&ctx");

        fprintf(f, "    %s_parse_cli_ctx(&ctx, argc, argv);\n", schema_name);
        fprintf(f, "    cfg->internal_pool = ctx.pool;\n");
        fprintf(f, "    if (ctx.status != CFG_SUCCESS) { %s_free(cfg); return ctx.status; }\n", schema_name);

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
