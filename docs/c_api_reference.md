# C API Reference

The `cfgsafe` generator produces a standalone C99 header-only library. It uses a single-header pattern: define `CONFIG_IMPLEMENTATION` in exactly one source file to generate the logic.

- [Data Structures](#data-structures)
- [Lifecycle Functions](#lifecycle-functions)
- [Validation Hooks](#validation-hooks)
- [Memory Management](#memory-management)
- [Error Handling](#error-handling)
- [Thread Safety](#thread-safety)

---

## Data Structures

### Generated Structs
For a schema named `Server`, a `Server_t` struct is generated.
- **Primitives**: Mapped to standard types (`int64_t`, `double`, `bool`, `const char*`).
- **Sections**: Represented as nested structs.
- **Embedded Schemas**: Represented as nested instances of that schema's type.

### Primitive Types Reference
| Schema Type | C Type | Note |
| :--- | :--- | :--- |
| `int` | `int64_t` | |
| `float` | `double` | |
| `bool` | `bool` | Requires `<stdbool.h>` |
| `string` | `const char*` | UTF-8, memory-managed |
| `path` | `const char*` | Same as string |
| `ipv4` | `cfg_ipv4_t` | |

### `cfg_ipv4_t`
```c
typedef struct {
    uint8_t octets[4];
} cfg_ipv4_t;
```

### Enumerations
The enum is prefixed with the schema and field name to avoid collisions:

```c
// Schema: schema Logger { level: enum(DEBUG, INFO) }
typedef enum {
    Logger_level_DEBUG,
    Logger_level_INFO
} Logger_level_t;
```

**Usage:**
```c
if (config.level == Logger_level_DEBUG) {
    // ...
}
```

### Arrays
Arrays are generated as anonymous structs within the parent schema struct:
```c
struct { 
    T *data; 
    size_t count; 
} field_name;
```

**Usage:**
```c
// Iterate over array
for (size_t i = 0; i < config.node_ips.count; i++) {
    printf("Node %zu: %s\n", i, config.node_ips.data[i]);
}
```

---

## Lifecycle Functions

Every schema `Name` generates the following API:

### `Name_load`
The primary entry point. It follows a strict precedence: **CLI > Environment > INI > Defaults**.

```c
cfg_status_t Name_load(
    Name_t *cfg, 
    const char *filename, 
    int argc, 
    const char **argv, 
    cfg_error_t *err
);
```
- `filename`: Path to `.ini` file. If `NULL`, file parsing is skipped.
- `argc/argv`: Passed directly from `main`. Use `0, NULL` to skip CLI parsing.
- **Returns**: `CFG_SUCCESS` on success, or an error code.

### `Name_load_report`
Same as `Name_load`, but reports **every** error, not just the first, by calling `on_error` once per error. `err` (if not `NULL`) still receives the first error, and the return value is the same as `Name_load` would give.
```c
typedef void (*cfg_error_cb)(const cfg_error_t *error, void *user);

cfg_status_t Name_load_report(Name_t *cfg, const char *filename, int argc, const char **argv,
                              cfg_error_t *err, cfg_error_cb on_error, void *user);
```
Parse errors from the INI file, environment and command line are all reported together. Validation (constraints and hooks) runs only if parsing succeeded, so a bad value isn't reported a second time as, say, out of range.

```c
static void print_error(const cfg_error_t *e, void *user) {
    (void)user;
    fprintf(stderr, "config.ini:%zu: %s: %s\n", e->line, e->field, e->message);
}

if (Config_load_report(&cfg, "config.ini", argc, argv, NULL, print_error, NULL) != CFG_SUCCESS) return 1;
```

### `Name_free`
Releases all memory held by the config instance.
```c
void Name_free(Name_t *cfg);
```
- Frees interned strings, array data, and the internal memory pool.
- **Note**: Does not free the `cfg` pointer itself.

### `Name_print`
Prints the current configuration to a stream.
```c
void Name_print(const Name_t *cfg, FILE *f);
```
- Fields marked `secret: true` are redacted as `********`.

### `Name_validate`
Manually triggers the validation logic. Useful if you modify fields programmatically.
```c
bool Name_validate(const Name_t *cfg, cfg_error_t *err);
```

### `Name_parse_cli`
Internal helper exposed for modularity. Parses CLI arguments into the struct without running validation or loading other sources.
Values that fail to parse are skipped silently here, since this function cannot report errors; `Name_load` parses the same arguments and does report them.
```c
void Name_parse_cli(Name_t *cfg, int argc, const char **argv);
```

---

## Validation Hooks

Hooks allow custom C logic to validate fields.

**Signature**:
```c
bool hook_name(const void *val, cfg_error_t *err);
```

- **Strings/Paths**: `val` is `const char*`.
- **Primitives**: `val` is a pointer to the type (e.g., `int64_t*`).
- **Arrays**: `val` is a pointer to the array struct.
- **`err`**: an empty error to describe the problem in. Setting `message` is enough: the field's full path, its INI line and `kind = CFG_KIND_HOOK` are filled in for you. If you leave `message` empty, it becomes `rejected by hook '<name>'`.

**Example**:
```c
bool validate_port(const void *val, cfg_error_t *err) {
    int64_t port = *(int64_t*)val;
    if (port == 80) {
        snprintf(err->message, sizeof(err->message), "port 80 is reserved");
        return false;
    }
    return true;
}
```

---

## Memory Management

`cfgsafe` uses an internal **Arena Allocator** (Memory Pool) to manage configuration data.

1. **Interning**: All strings and paths are interned. Duplicate strings across different fields share the same memory location.
2. **Aggregation**: Array data and strings are allocated within the schema's pool.
3. **Teardown**: A single call to `_free()` releases every allocation made during the lifecycle of that config instance.

**Rule**: Never manually `free()` strings or arrays accessed from the config struct.

---

## Error Handling

### `cfg_status_t`
- `CFG_SUCCESS`: No errors.
- `CFG_ERR_OPEN_FILE`: INI file could not be read.
- `CFG_ERR_SYNTAX`: Syntax error in the INI file.
- `CFG_ERR_VALIDATION`: A value could not be parsed as its field's type, or a built-in constraint or custom hook failed.

`Name_load` reports only the first error; use `Name_load_report` to get all of them.

`field` is relative to the struct being loaded or validated, without the schema name: `level1.level2.bind_addr`, `sources[1].target`. For an unknown command-line option it is the option name. `line` is set for parse errors and, when loading with `Name_load`, for validation errors too: it is the line that last assigned the failing field. It is 0 when the value came from a default, the environment or the command line, when the field is missing, and when calling `Name_validate` directly.

If a hook returns `false` without filling in `err`, the field name and the message `rejected by hook '<name>'` are filled in for it.

### `cfg_error_t`
```c
typedef struct {
    char message[512]; // Error description
    char field[256];   // Path to the failing field, e.g. "db.port" or "servers[1].host"
    size_t line;       // INI line that set the failing value (0 if not from the INI file)
    cfg_error_kind_t kind; // What went wrong (see below)
} cfg_error_t;
```

### `cfg_error_kind_t`
`kind` classifies the error without parsing `message`:

| Kind | Meaning | Status |
|---|---|---|
| `CFG_KIND_NONE` | No error | `CFG_SUCCESS` |
| `CFG_KIND_OPEN_FILE` | The INI file could not be opened | `CFG_ERR_OPEN_FILE` |
| `CFG_KIND_SYNTAX` | Malformed INI line | `CFG_ERR_SYNTAX` |
| `CFG_KIND_INVALID_VALUE` | Value not valid for the field's type or enum | `CFG_ERR_VALIDATION` |
| `CFG_KIND_UNKNOWN_KEY` | Unknown INI key or section, or unknown `--Schema.` option | `CFG_ERR_VALIDATION` |
| `CFG_KIND_MISSING` | Required field not provided, or option given without a value | `CFG_ERR_VALIDATION` |
| `CFG_KIND_CONSTRAINT` | `range`, `min`/`max`, length, `pattern` or `exists` failed | `CFG_ERR_VALIDATION` |
| `CFG_KIND_HOOK` | A validation hook returned `false` | `CFG_ERR_VALIDATION` |
| `CFG_KIND_OUT_OF_MEMORY` | An allocation failed | `CFG_ERR_VALIDATION` |
