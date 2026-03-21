# cfgsafe: Safe, Validated C Configuration

![C99 Strict](https://img.shields.io/badge/Language-C99-blue.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)
![Zero Dependencies](https://img.shields.io/badge/Dependencies-0-success.svg)
![Single Header](https://img.shields.io/badge/Format-Single%20Header-orange.svg)

**cfgsafe** is a fast, lightweight C library and code generator that translates a programmer‑defined schema into a strongly-typed C `struct`, complete with a zero-allocation INI parser and rigorous validation constraints.

You define the shape of your configuration in a specialized `.schema` file. `cfgsafe` handles parsing, defaults, environment variable overrides, constraint checking (regex, lengths, ranges), and memory cleanup. It generates a **single-file header library** (STB-style) you drop directly into any C or C++ project.

Stop writing boilerplate string-to-int parsing code. Stop silently ignoring invalid config values. Stop crashing on null pointers from unvalidated environment variables.

---

## Why cfgsafe?

* **Zero Dependencies**: Pure C99 implementation. No external regex engines or complex build steps required.
* **Type Safety**: Forget `void*` dictionaries. Your schema is compiled directly into standard C `struct`s, allowing you to access configuration fields naturally (e.g., `cfg.listen_port`, `cfg.database.host`) with the correct native C types (`int64_t`, `double`, `bool`, arrays, and `enum`s) automatically inferred.
* **Deep Validation**: First-class support for numeric `range`s, string `min_length`/`max_length`, regex `pattern` matching, and file `exists` checks.
* **Conditional Logic**: Complex cross-field validation out of the box (e.g., `required_if: enable_tls == true`).
* **Single-Header Output**: Generates one `.h` file containing both the data structures and the implementation logic.
* **Environment Variables**: Seamlessly override file configuration keys with environment variables. Great for deploying in Docker or modern cloud environments.

---

## Quick Start

### 1. Define the Schema (`config.schema`)

Define your configuration structure, validation rules, and defaults in a clean, readable syntax:

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

    // Embed the Database schema from above
    database: DatabaseConfig {}

    // Group fields natively
    section caching {
        enabled: bool { default: false }
        
        // Array types
        nodes: string[] {
            min_length: 1
            required_if: enabled == true
        }

        // Deeply nested section
        section redis {
            pool_size: int { default: 10 }
        }
    }
}
```

### 2. Provide the Data (`config.ini`)

The runtime parser consumes standard INI file syntax mapping logically to the structures you defined. For nested sections or embedded schemas, `cfgsafe` uses dot-notation `[parent.child]` for headers.

```ini
service_name = edge-gateway
bind_address = 127.0.0.1
enable_tls = false

[database]
driver = postgres
host = db.local
port = 5432

[caching]
enabled = true
nodes = cache-1.local,cache-2.local

[caching.redis]
pool_size = 20
```

### 3. Run the Generator

Compile your schema into a C header file using the `cfgsafe` generator:

```bash
cfg-gen config.schema
# Outputs -> config.h
```

### 4. Runtime Integration (`main.c`)

Include the generated header. Define `CONFIG_IMPLEMENTATION` in exactly *one* C file to compile the implementation logic. Load an INI file (e.g. `config.ini`).

```c
#define CONFIG_IMPLEMENTATION
#include "config.h"
#include <stdio.h>

int main(void) {
    ApiGateway_t cfg;
    cfg_error_t err;
    
    printf("--- Loading Configuration ---\n");
    
    // Load from INI file, apply env overrides, and run all validations
    cfg_status_t status = ApiGateway_load(&cfg, "config.ini", &err);
    
    if (status == CFG_SUCCESS) {
        printf("Service Name: %s\n", cfg.service_name);
        printf("Database Host: %s\n", cfg.database.host);
        printf("Database Port: %d\n", (int)cfg.database.port);
        
        // Free dynamically allocated memory (strings, arrays)
        ApiGateway_free(&cfg);
    } else {
        // Detailed error reporting tells you exactly what failed
        fprintf(stderr, "Config Status: FAILURE (code %d)\n", (int)status);
        fprintf(stderr, "Error: %s\n", err.message);
        fprintf(stderr, "Field: %s\n", err.field);
        if (err.line > 0) fprintf(stderr, "Line: %zu\n", err.line);
        return 1;
    }

    return 0;
}
```

---

## Examples

To see a fully working example including error recovery, environment override testing, and complex data models, check out the `examples/` directory in our repository:

1. `examples/config.schema`: The comprehensive test schema
2. `examples/config.ini`: The mock configuration data
3. `examples/test_loader.c`: An implementation demonstrating safe extraction

---

## Documentation

Comprehensive documentation has been split into detailed guides. Please refer to them for advanced usage:

1. [Schema Definition Guide](docs/schema_guide.md) - Learn how to define schemas, built-in types, nesting, and validation rules.
2. [C API Reference](docs/c_api_reference.md) - Learn about memory management, error handling structure, and C integration.

---

## Installation & Building

The code generator (`cfg-gen`) runs locally to read your schema and emit the C header.

```bash
git clone https://github.com/your-username/cfgsafe.git
cd cfgsafe/tools
make
```

Ensure the output binary is in your system `PATH`. Use the executable to generate headers alongside your standard build process (e.g. inside your `Makefile` or `CMakeLists.txt`).

---

## Contributing

Contributions are always welcome. Please submit pull requests or open issues for bugs, feature requests, or documentation improvements.

