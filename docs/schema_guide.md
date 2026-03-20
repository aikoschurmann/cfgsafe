# Schema Definition Guide

The `cfgsafe` schema language is a declarative format used to define the shape, types, defaults, and validation rules of your configuration. This file is parsed by the `cfg-gen` tool to produce a single-file C header.

## Structure Overview

A schema file typically consists of `schema` blocks, which represent top-level configuration structures.

```scala
schema ServerConfig {
    // fields and sections go here
}
```

## Data Types

`cfgsafe` provides a strict set of built-in data types that map directly to native C types.

| Schema Type | C Equivalent | Description |
| :--- | :--- | :--- |
| `int` | `int64_t` | A signed 64-bit integer. |
| `float` | `double` | A double-precision floating-point number. |
| `bool` | `bool` | A boolean value (`true` or `false`). |
| `string` | `const char*` | A dynamically allocated, interned text string. |
| `path` | `const char*` | A file system path string. |
| `ipv4` | `cfg_ipv4_t` | An IPv4 address struct (`uint8_t octets[4]`). |

### Enumerations (`enum`)

Enums map to native C enums. They restrict a field to a predefined set of string identifiers.

```scala
schema Config {
    log_level: enum(debug, info, warn, error) {
        default: info
    }
}
```

### Arrays (`[]`)

Any base type can be converted into an array by appending `[]`. Arrays in the generated C code are represented as a pointer and a length field.

```scala
schema Config {
    ip_allowlist: ipv4[]
}
```
In C, this generates:
```c
char** ip_allowlist;
sonst cize_t ip_allowlist_len;
```

## Nesting and Grouping

### Sections

You can group fields logically using the `section` keyword. This maps to nested structures in C and maps directly to `[section_name]` blocks in INI files.

```scala
schema Config {
    section database {
        host: string
        port: int
    }
}
```

### Embedded Schemas

Schemas can be reused by embedding them as field types.

```scala
schema DatabaseConfig {
    port: int
}

schema MainConfig {
    primary_db: DatabaseConfig {}
    replica_db: DatabaseConfig {}
}
```

## Properties and Constraints

Fields can define behavior and constraints within a `{}` block.

### Default Values (`default`)
Provides a fallback value if the key is missing from the configuration source and environment.
```scala
port: int { default: 8080 }
```

### Environment Overrides (`env`)
Binds a field to an environment variable. If the environment variable is present, it overrides all other values.
```scala
secret_key: string { env: "APP_SECRET_KEY" }
```

### Validation Constraints

Validation rules are evaluated automatically by the generated `_load` function.

* **`range`**: Enforces a numeric boundary for `int` or `float` types.
  ```scala
  port: int { range: 1024..65535 }
  ```
* **`min_length` / `max_length`**: Constraints for `string`, `path`, or array types.
  ```scala
  username: string { min_length: 3, max_length: 32 }
  ```
* **`pattern`**: A regular expression applied to `string` fields.
  ```scala
  uuid: string { pattern: "^[0-9a-f]{8}-([0-9a-f]{4}-){3}[0-9a-f]{12}$" }
  ```
* **`exists`**: Checks if a file or directory exists at parsing time (applies to `path`).
  ```scala
  cert_file: path { exists: true }
  ```
* **`required_if`**: Evaluates cross-field dependencies.
  ```scala
  use_tls: bool { default: false }
  cert: path { required_if: use_tls == true }
  ```
