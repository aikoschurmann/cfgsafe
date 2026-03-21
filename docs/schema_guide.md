# Schema Definition Guide

The `cfgsafe` schema language is a declarative DSL used to define hierarchical, strictly-typed configuration models. This guide provides an exhaustive reference for the syntax, semantics, and mapping rules.

---

## 1. Syntax Fundamentals

*   **Identifiers**: Field and schema names must start with a letter and contain only alphanumeric characters or underscores.
*   **Separators**: Fields are defined as `name: type`. Property blocks are required for any field defining constraints or defaults.
*   **Comments**: Use `//` for single-line or `/* */` for multi-line comments.
*   **Imports**: Must appear at the top of the file using `import "header.h"`.

---

## 2. The Type System

`cfgsafe` maps schema types to native C99 types.

### Primitives
| Keyword | C99 Type | Description |
| :--- | :--- | :--- |
| `int` | `int64_t` | 64-bit signed integer. |
| `float` | `double` | Double-precision floating point. |
| `bool` | `bool` | Boolean (maps `true`/`false` or `1`/`0` from INI). |
| `string` | `const char*` | UTF-8 string, interned in the internal memory pool. |
| `path` | `const char*` | Same as `string`, but supports the `exists` constraint. |
| `ipv4` | `cfg_ipv4_t` | A struct containing 4 `uint8_t` octets. |

### Complex Types

#### Enumerations (`enum`)
Restricts a field to a specific set of identifiers.
```scala
level: enum(DEBUG, INFO, ERROR) { default: INFO }
```
*   **C Access**: `cfg.level == MySchema_level_DEBUG`. (Members are prefixed to avoid collisions).

#### Arrays (`T[]`)
Any type can be converted to an array.
```scala
tags: string[]
```
*   **C Struct**:
    ```c
    struct { T *data; size_t count; } tags;
    ```
*   **INI Format**: Comma-separated list: `tags = val1, val2, val3`.

#### Schema Composition
You can use a defined `schema` as a type for a field in another schema.
```scala
schema DB { host: string }
schema App {
    database: DB {}
}
```

---

## 3. Strict INI Mapping Rules

`cfgsafe` enforces a **strictly explicit** mapping between the schema and the `.ini` file.

### Rule 1: No Global Keys
Every configuration key **must** reside inside a section. Keys at the very top of an INI file (global scope) are ignored.

### Rule 2: Root Scoping
Fields defined directly in a `schema` block must be placed in a section named exactly like that schema.

**Schema:**
```scala
schema MyConfig {
    version: int
}
```
**Required INI:**
```ini
[MyConfig]
version = 1
```

### Rule 3: Hierarchical Prefixing
Sections and embedded schemas must be prefixed by their parent path using dot-notation.

**Schema:**
```scala
schema App {
    section network {
        port: int
    }
}
```
**Required INI:**
```ini
[App.network]
port = 8080
```

---

## 4. Property Reference

Properties are applied inside `{}` after the type.

| Property | Applies To | Description |
| :--- | :--- | :--- |
| `default` | All | Fallback value if missing from INI and ENV. |
| `env` | All | Environment variable name for overrides. |
| `required` | All | Fails load if value is not provided by any source. |
| `secret` | All | If `true`, redacts value in `_print()` output. |
| `range` | `int`, `float`| Inclusive bounds: `range: 1..100`. |
| `min_length`| `string`, `any[]`| Minimum characters or array element count. |
| `max_length`| `string`, `any[]`| Maximum characters or array element count. |
| `pattern` | `string` | Regex match (e.g. `"^[a-z]+$"`). |
| `exists` | `path` | Validates file presence on the filesystem. |
| `hook` | All | Name of custom C function for validation. |

---

## 5. Precedence & Resolution

When `_load()` is called, the value for each field is determined in this order:

1.  **Environment Variable**: Highest priority. Only used if `env` property is set.
2.  **INI File**: Read from the explicitly scoped section.
3.  **Default**: Fallback from the schema definition.

---

## 6. Custom Validation Hooks

Hooks allow you to run C code during validation.
**Signature**: `bool hook_name(const void *val, cfg_error_t *err);`

| Target Type | Accessing `val` in C |
| :--- | :--- |
| `string`, `path` | `const char *s = (const char *)val;` (Direct value) |
| `int` | `int64_t i = *(int64_t *)val;` (By address) |
| `float` | `double d = *(double *)val;` (By address) |
| `any[]` | `struct { T *data; size_t count; } *a = (void *)val;` |

---

## 7. Regex Support

The built-in engine supports standard tokens: `^`, `$`, `.`, `[char-class]`, `[range]`, `[^negated]`, `+`, `*`, `?`.
Example: `pattern: "^[A-F0-9]{8}$"`
