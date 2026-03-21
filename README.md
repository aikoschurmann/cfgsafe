# cfgsafe: Safe, Validated C Configuration

![C99 Strict](https://img.shields.io/badge/Language-C99-blue.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)
![Zero Dependencies](https://img.shields.io/badge/Dependencies-0-success.svg)
![Single Header](https://img.shields.io/badge/Format-Single%20Header-orange.svg)

**cfgsafe** is a high-performance configuration engine for C99+ that transforms a declarative schema into a strongly-typed, memory-safe C header library. It provides built-in validation, hierarchical grouping, and environment variable overrides without external dependencies.

Stop writing fragile string-to-int parsing logic. Stop silently ignoring invalid values. **cfgsafe** ensures your configuration is correct before your application even starts.

---

## Core Features

*   **Zero Dependencies**: Pure C99 implementation. No regex libraries, no JSON parsers, no bloat.
*   **Strong Typing**: Your schema becomes a native C `struct`. Access fields naturally like `cfg.db.port` with correct types (`int64_t`, `double`, `bool`, etc.).
*   **Strict Explicit Mapping**: All INI keys must be explicitly scoped within sections (e.g., `[MySchema]`). Global keys are ignored for safety.
*   **Deep Validation**: Built-in support for numeric `range`s, string/array `min_length`, regex `pattern` matching, and file `exists` checks.
*   **Custom Hooks**: Extend validation with your own C functions using the `hook` property.
*   **Strict Precedence**: Environment Variables > INI File > Default Values.
*   **Security First**: Mark fields as `secret` to ensure they are redacted automatically in logs and debug output.
*   **Zero-Allocation Runtime**: Uses an internal memory pool for string interning and array allocation, making cleanup as simple as a single `_free()` call.

---

## At a Glance

### 1. Define your Schema (`app.schema`)

```scala
import "validators.h" // Include custom C validation hooks

schema Database {
    host: string { 
        default: "localhost" 
        pattern: "^[a-z0-9.-]+$"
    }
    port: int { 
        default: 5432 
        range: 1..65535 
    }
}

schema Config {
    service_name: string { required: true }
    db: Database {} // Recursive composition

    section caching {
        enabled: bool { default: false }
        nodes: string[] { 
            required_if: enabled == true 
            hook: "validate_nodes"
        }
    }
}
```

### 2. Provide the Data (`config.ini`)

All keys **must** be inside a section named after the root schema or its sub-paths.

```ini
[Config]
service_name = "production-api"

[Config.db]
host = "db.internal"
port = 5432

[Config.caching]
enabled = true
nodes = 10.0.0.1, 10.0.0.2
```

### 3. Generate and Use

```bash
cfg-gen app.schema -o config.h
```

```c
#define CONFIG_IMPLEMENTATION
#include "config.h"

int main() {
    Config_t cfg;
    if (Config_load(&cfg, "config.ini", NULL) == CFG_SUCCESS) {
        printf("App: %s\n", cfg.service_name);
        Config_free(&cfg);
    }
    return 0;
}
```

---

## Documentation

*   [**Schema Definition Guide**](docs/schema_guide.md) - Syntax, data types, and validation rules.
*   [**C API Reference**](docs/c_api_reference.md) - Integration, memory management, and error handling.

## Installation

```bash
cd tools && make
# Binary at tools/out/cfg-gen
```
