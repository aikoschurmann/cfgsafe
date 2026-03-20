# cfgsafe — Safe, validated C configuration

**cfgsafe** is a small C library and code generator that turns a programmer‑defined schema into a validated, typed `struct` your program can use with zero runtime failure paths. The generator reads the schema from a file, produces a single-header library (STB-style) with defaults and validation, and your program simply calls a generated `*_load` function at startup.

---

## Features

* **Strict Typing & Enums:** Generates native C types (`int64_t`, `double`, `bool`, arrays, and `enum`s).
* **Deep Nesting:** Compose configurations using `section` blocks or by embedding other `schema`s.
* **Rich Validation:** Built-in support for numeric `range`s, string `min_length`/`max_length`, regex `pattern` matching, and file `exists` checks.
* **Conditional Logic:** Cross-field validation using `required_if`.
* **Environment Overrides:** Seamlessly override specific file configuration keys with environment variables.
* **Single-Header Output:** Generates a single `.h` file containing both the definitions and the implementation.

---

## Quick start

### 1. `config.schema` (what you write)

Define your configuration structure, validation rules, and defaults.

```scala
schema DatabaseConfig {
    driver: enum(postgres, mysql, sqlite) {
        default: postgres
    }

    host: string {
        default: "localhost"
        env: "DB_HOST"
    }

    port: int {
        default: 5432
        range: 1..65535
    }
}

schema ApiGateway {
    service_name: string {
        min_length: 3
        pattern: "^[a-z0-9-]+$"
        default: "edge-gateway"
    }

    bind_address: ipv4 {
        default: "0.0.0.0"
        env: "BIND_ADDR"
    }

    enable_tls: bool { 
        default: false 
    }

    cert_path: path {
        required_if: enable_tls == true
        exists: true
    }

    // Embed another schema
    database: DatabaseConfig {}

    section caching {
        enabled: bool { default: false }
        
        // Array type with constraints
        nodes: string[] {
            min_length: 1
            required_if: enabled == true
        }
    }
}
```

### 2. Run the generator

Compile your schema into a C header file.

```bash
cfg-gen config.schema
# Generates config.h
```

### 3. `main.c` (runtime)

Include the generated header. Define `CONFIG_IMPLEMENTATION` in exactly *one* C file to compile the implementation logic.

```c
#define CONFIG_IMPLEMENTATION
#include "config.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    ApiGateway_t cfg;
    cfg_error_t err;
    
    printf("--- Loading Configuration ---\n");
    
    // Load from INI file, apply env overrides, and run all validations
    cfg_status_t status = ApiGateway_load(&cfg, "config.ini", &err);
    
    if (status == CFG_SUCCESS) {
        printf("Service Name: %s\n", cfg.service_name);
        printf("Database Host: %s\n", cfg.database.host);
        
        // Free dynamically allocated memory (strings, arrays)
        ApiGateway_free(&cfg);
    } else {
        // Detailed error reporting
        printf("Config Status: FAILURE (code %d)\n", (int)status);
        printf("Error: %s\n", err.message);
        printf("Field: %s\n", err.field);
        if (err.line > 0) printf("Line: %zu\n", err.line);
        return 1;
    }

    return 0;
}
```

---

## Error Handling

`cfgsafe` provides granular error reporting through the `cfg_error_t` struct, which populates a human-readable `message`, the specific `field` that failed validation, and the `line` number (if a syntax error occurred in the INI file).

Possible `cfg_status_t` return codes:
* `CFG_SUCCESS` (0): Configuration loaded and validated successfully.
* `CFG_ERR_OPEN_FILE`: The specified INI file could not be read.
* `CFG_ERR_SYNTAX`: The INI file contained malformed syntax.
* `CFG_ERR_VALIDATION`: The configuration failed a schema constraint (e.g., out of bounds, missing required field, pattern mismatch).