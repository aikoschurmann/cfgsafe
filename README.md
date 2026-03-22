# cfgsafe — C99 Configuration Engine

![C99 Strict](https://img.shields.io/badge/Language-C99-blue.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)
![Zero Dependencies](https://img.shields.io/badge/Dependencies-0-success.svg)
![Single Header](https://img.shields.io/badge/Format-Single%20Header-orange.svg)

**cfgsafe** is a high-performance configuration engine for C99. It transforms a declarative schema into a strongly-typed, memory-safe single-header C library.

Stop writing fragile string-to-int parsing logic. Stop silencing invalid values. `cfgsafe` handles parsing, deep validation, and memory management for you, ensuring your configuration is 100% correct before your application logic even starts.

---

## Features

* **Zero Dependencies**: Pure C99 implementation. No regex libraries, no JSON parsers, no external bloat.
* **AOT Strong Typing**: Your schema compiles into a native C `struct`. Access fields naturally like `cfg.db.port` with the correct types (`int64_t`, `double`, `bool`, etc.). No dynamic string lookups natively!
* **Layered Resolution**: Automatically merges **CLI Arguments**, **Environment Variables**, and **INI Files** safely with strict precedence.
* **Deep Validation**: Built-in support for numeric `range`s, string `min_length`, regex `pattern` matching, and system file `exists` checks.
* **Security Built-In**: Tag fields as `secret` to explicitly redact them from the auto-generated debug output.
* **Safe Memory Model**: Uses an internal memory pool. Deeply nested configurations containing strings and arrays are freed with a single `_free()` call.

---

## Quick Start

### 1. Write the Schema (`app.schema`)
Define the shape of your constraints and defaults:

```scala
import "validators.h" // Optional: custom C validation hooks

schema Database {
    host: string { 
        default: "localhost" 
        pattern: "^[a-z0-9.-]+$"
    }
    // Automatically binds to the DB_PORT env var
    port: int { 
        env: "DB_PORT"
        default: 5432 
        range: 1..65535 
    }
    password: string {
        secret: true // Redacted from prints
    }
}

schema Config {
    service_name: string { required: true }
    
    // Embed the defined Database schema
    db: Database {}
}
```

### 2. Add some Data (`config.ini`)
Section names in your INI file map directly to your schema structure:

```ini
[Config]
service_name = "production-api"

[Config.db]
host = "db.internal"
password = "super_secret_password"
```

### 3. Generate & Run
Run the generator on your schema:
```bash
cfg-gen app.schema -o config.h
```

Include it in your C app:
```c
#define CONFIG_IMPLEMENTATION
#include "config.h"

int main(int argc, const char **argv) {
    Config_t cfg;
    cfg_error_t err;

    // Parses the INI file, overrides with ENV variables, 
    // and finally applies any CLI arguments (e.g. --db.port 9000)!
    if (Config_load(&cfg, "config.ini", argc, argv, &err) == CFG_SUCCESS) {
        
        // Fully initialized, strongly-typed C runtime struct 
        printf("App: %s starting on DB port %d\n", 
               cfg.service_name, 
               (int)cfg.db.port);

        // Safely dump config to stdout (tag 'secret' hides the password)
        Config_print(&cfg, stdout);

        // One call frees all dynamically allocated sub-strings/arrays
        Config_free(&cfg);
    } else {
        fprintf(stderr, "Startup Failed! Field '%s': %s\n", err.field, err.message);
        return 1;
    }
    return 0;
}
```

---

## Documentation & Reference

Dive into the details of the DSL and C integrations:

* [**Schema Definition Guide**](docs/schema_guide.md) - Syntax, data types (like nested arrays or IP addresses), validation rules, and implicit CLI flags mapping.
* [**C API Reference**](docs/c_api_reference.md) - The generated module lifecycle, memory pooling mechanics, and creating custom C `hook` bindings.

---

## Installation & Build

Build the code generator binary from source:

```bash
git clone https://github.com/your-username/cfgsafe.git
cd cfgsafe/tools
make
```

The generator will be available at `tools/out/cfg-gen`. We highly recommend invoking it as a pre-build step in your `CMakeLists.txt` or `Makefile`.
