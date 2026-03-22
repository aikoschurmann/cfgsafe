# C API Reference

The `cfgsafe` generator (`cfg-gen`) produces a single-file C header library containing strictly-typed data structures and lifecycle functions.

---

## Integration (STB-Style)

The generated header follows the **STB single-header pattern**. 
*   `#include "my_config.h"` in any file to access types.
*   `#define CONFIG_IMPLEMENTATION` in **one** C source file before including to generate the implementation.

```c
#define CONFIG_IMPLEMENTATION
#include "my_config.h"
```

---

## Life-Cycle Functions

For every `schema Name` defined, `cfgsafe` generates four primary functions:

### 1. `Name_load`
The main entry point. Initializes the config, parses the INI, applies environment overrides, parses CLI arguments natively, and finally runs validation.

```c
cfg_status_t Name_load(Name_t *cfg, const char *filename, int argc, const char **argv, cfg_error_t *err);
```
*   `cfg`: Pointer to the struct to populate.
*   `filename`: Path to the `.ini` file. Pass `NULL` to skip file parsing and use only defaults/ENV/CLI.
*   `argc`, `argv`: The standard main arguments. Pass `0, NULL` to disable CLI parsing.
*   **Memory**: All strings, arrays, and paths are allocated internally. Calling `Name_free` wipes them all safely.

### 2. `Name_parse_cli`
A modular helper called internally by `_load`, but exposed strictly to parse `argc`/`argv` mapping directly to the nested structs (e.g. `--db.port 5432`).

```c
void Name_parse_cli(Name_t *cfg, int argc, const char **argv);
```

### 3. `Name_print`
Recursively debug-prints the configuration to a file stream.

```c
void Name_print(const Name_t *cfg, FILE *f);
```
*   **Redaction**: Automatically replaces fields marked `secret: true` with `********`.

### 3. `Name_free`
Frees **all** memory allocated during `_load()` (interned strings and array data).

```c
void Name_free(Name_t *cfg);
```
*   Does not free the `cfg` pointer itself (allowing stack or custom heap allocation of the struct).

### 4. `Name_validate`
Internal validation pass. Can be called manually if you modify the struct fields at runtime.

```c
bool Name_validate(const Name_t *cfg, cfg_error_t *err);
```

---

## Custom Validation Hooks

Hooks are C functions triggered during the validation phase of `_load()`.

**Schema Syntax:**
```scala
field: string { hook: "my_validator" }
```

**C Implementation:**
```c
bool my_validator(const void *val, cfg_error_t *err) {
    // Strings are passed directly as values
    const char *str = (const char*)val;
    
    // Primitives and arrays are passed by address
    // int64_t i = *(int64_t*)val;
    
    if (/* fail condition */) {
        if (err) strcpy(err->message, "Custom failure reason");
        return false;
    }
    return true;
}
```

---

## Error Handling

### `cfg_status_t`
| Status | Meaning |
| :--- | :--- |
| `CFG_SUCCESS` | Load succeeded. |
| `CFG_ERR_OPEN_FILE` | INI file not found or inaccessible. |
| `CFG_ERR_SYNTAX` | Malformed INI syntax. |
| `CFG_ERR_VALIDATION` | A constraint or hook failed. |

### `cfg_error_t`
Contains diagnostics when an error occurs.
```c
typedef struct {
    char message[512]; // Description of the failure
    char field[256];   // The dot-notated field path (e.g. "App.db.port")
    size_t line;       // INI line number (if applicable)
} cfg_error_t;
```
