# C API Reference

The `cfgsafe` generator (`cfg-gen`) produces a single-file C header library that contains both your data structures and the implementation code needed to parse, validate, and free the configuration.

## STB-Style Single Header

The generated code follows the STB single-header pattern. You must `#include` the file in all source files accessorizing the types, but you must define `CONFIG_IMPLEMENTATION` in **exactly one** C file to emit the actual function definitions.

```c
// config_impl.c
#define CONFIG_IMPLEMENTATION
#include "config.h"
```

## Generated Types structures

For every `schema Name` declared in your `.schema` file, `cfgsafe` generates a corresponding `Name_t` struct.

```c
typedef struct {
    const char* service_name;
    bool enable_tls;
    
    struct {
        int64_t port;
    } database;
    
    const char** nodes;
    size_t nodes_len;
} ApiGateway_t;
```

## Core Functions

Each schema generates a standard set of procedures for lifecycle management.

### `[SchemaName]_load`

```c
cfg_status_t [SchemaName]_load([SchemaName]_t* cfg, const char* filepath, cfg_error_t* err);
```

**Parameters:**
* `cfg`: Pointer to the uninitialized structurally-typed configuration object.
* `filepath`: Path to the INI file to parse (optional, can be NULL to load only defaults and environment).
* `err`: Pointer to an error struct to populate in case of failure.

**Returns:** A `cfg_status_t` indicating the result of the load operation.

### `[SchemaName]_print`

```c
void [SchemaName]_print(const [SchemaName]_t* cfg, FILE* f);
```

**Parameters:**
* `cfg`: Pointer to a loaded configuration object.
* `f`: File stream to print to (e.g., `stdout`, `stderr`, or a log file). If `NULL`, defaults to `stdout`.

Recursively prints the configuration structure. Fields marked as `secret: true` in the schema will be redacted as `********`.

### `[SchemaName]_free`

```c
void [SchemaName]_free([SchemaName]_t* cfg);
```

**Parameters:**
* `cfg`: Pointer to a previously loaded configuration object.

Frees any heap-allocated memory associated with strings, paths, and arrays within the structure. Does not free the `cfg` pointer itself (allowing stack-allocated structs).

## Status and Error Handling

`cfgsafe` will never assert or crash your application on malformed data. Instead, it returns a status code and populates a detailed error object.

### `cfg_status_t` Enum

| Status Code | Value | Description |
| :--- | :--- | :--- |
| `CFG_SUCCESS` | `0` | Configuration was parsed and validated successfully. |
| `CFG_ERR_OPEN_FILE` | `1` | The provided configuration file could not be opened. |
| `CFG_ERR_SYNTAX` | `2` | The INI file has a syntax error. |
| `CFG_ERR_VALIDATION` | `3` | A value failed validation (e.g. out of range, missing required field). |
| `CFG_ERR_MEMORY` | `4` | An allocation failure occurred (OOM). |

### `cfg_error_t` Struct

When `_load` returns anything other than `CFG_SUCCESS`, the provided `cfg_error_t*` object is populated with detailed diagnostics.

```c
typedef struct {
    char message[256]; // Human-readable error description
    char field[128];   // The specific schema field that triggered the error
    size_t line;       // The line number in the INI file (if applicable)
} cfg_error_t;
```

**Example Error Handling:**
```c
cfg_error_t err;
cfg_status_t result = ApiGateway_load(&cfg, "config.ini", &err);

if (result == CFG_ERR_VALIDATION) {
    fprintf(stderr, "Validation failed for field '%s': %s\n", err.field, err.message);
    // e.g., Validation failed for field 'listen_port': Value 10000 exceeds maximum range 9000
}
```
