# cfgsafe — Type-Safe C99 Configuration

![C99 Strict](https://img.shields.io/badge/Language-C99-blue.svg)
![Zero Dependencies](https://img.shields.io/badge/Dependencies-0-success.svg)
![Single Header](https://img.shields.io/badge/Format-Single%20Header-orange.svg)

**cfgsafe** is a schema-driven configuration engine for C99. It transforms a declarative schema into a strongly-typed, memory-safe single-header C library.

Stop writing fragile string-to-int parsing logic. `cfgsafe` handles parsing, deep validation, and memory management for you, ensuring your configuration is 100% correct before your application logic even starts.

---

## Features

* **Zero Dependencies**: Pure C99 implementation.
* **AOT Strong Typing**: Your schema compiles into a native C `struct` (e.g., `cfg.db.port`).
* **Layered Resolution**: Automatically merges **CLI Arguments**, **Environment Variables**, and **INI Files** with strict precedence.
* **Deep Validation**: Built-in `range`s, `min_length`, regex `pattern`s, and file `exists` checks.
* **Security Built-In**: Redact `secret` fields from debug output automatically.
* **Safe Memory Model**: Deeply nested strings and arrays are managed by an internal pool and freed with one `_free()` call.

---

## Quick Start

### 1. Define your Schema (`app.schema`)
```scala
schema Database {
    host: string { default: "localhost" }
    port: int    { default: 5432, range: 1..65535, env: "DB_PORT" }
    password: string { secret: true }
}

schema Config {
    service_name: string { required: true }
    db: Database {}
}
```

### 2. Generate Header
```bash
cfg-gen app.schema -o config.h
```

### 3. Use in C
```c
#define CONFIG_IMPLEMENTATION
#include "config.h"

int main(int argc, const char **argv) {
    Config_t cfg;
    cfg_error_t err;

    if (Config_load(&cfg, "config.ini", argc, argv, &err) == CFG_SUCCESS) {
        printf("App: %s on port %d\n", cfg.service_name, (int)cfg.db.port);
        Config_free(&cfg);
    } else {
        fprintf(stderr, "Error in %s: %s\n", err.field, err.message);
        return 1;
    }
}
```

---

## Configuration Format (INI)

`cfgsafe` uses standard INI files with dot-notation for hierarchy.

```ini
[Config]
service_name = "My Application"

[Config.db]
host = "localhost"
port = 5432
```

See the [**INI Reference**](docs/ini_reference.md) for full details on section naming and value types.

---

## CLI Usage

The `cfg-gen` tool provides a simple interface:

```text
Usage: cfg-gen [options] <input.schema>

Options:
  -o, --output <file>    Set output header filename (default: config.h)
  -a, --ast              Print the Abstract Syntax Tree instead of generating code
  -v, --version          Show version information
  -h, --help             Show this help message
```

---

## Documentation

* [**Schema Definition Guide**](docs/schema_guide.md) - DSL syntax, types, and properties.
* [**INI Configuration Reference**](docs/ini_reference.md) - Mapping schema to INI files.
* [**C API Reference**](docs/c_api_reference.md) - Lifecycle, hooks, and memory management.

---

## Installation

Build the generator from source:

```bash
cd tools && make
```
The binary will be at `tools/out/cfg-gen`.
