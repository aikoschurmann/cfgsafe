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

**Example**:
```c
bool validate_port(const void *val, cfg_error_t *err) {
    int64_t port = *(int64_t*)val;
    if (port == 80) {
        if (err) {
            strcpy(err->message, "Port 80 is reserved");
            strcpy(err->field, "port");
        }
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
- `CFG_ERR_VALIDATION`: A built-in constraint or custom hook failed.

### `cfg_error_t`
```c
typedef struct {
    char message[512]; // Error description
    char field[256];   // Dot-notated path to the failing field
    size_t line;       // INI line number (0 if not applicable)
} cfg_error_t;
```