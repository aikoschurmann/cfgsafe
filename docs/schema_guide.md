# cfgsafe Schema Definition Guide

- [Language Syntax](#language-syntax)
- [Type System Reference](#type-system-reference)
- [Configuration File Format (INI)](#configuration-file-format-ini)
- [Property System](#property-system)
- [Configuration Loading and Precedence](#configuration-loading-and-precedence)
- [Complete Property Reference Table](#complete-property-reference-table)

---

## Language Syntax

### Basic Structure

A cfgsafe file contains schema definitions, optional imports, and field declarations:

```scala
// Optional imports appear at the top
import "custom_validators.h"

// Define schemas with nested structure
schema MyConfig {
    field_name: type { properties }
    
    section nested_group {
        another_field: type
    }
}
```

### Identifiers and Naming

- **Schema names**: Uppercase (e.g., `AppConfig`)
- **Field/Section names**: snake_case (e.g., `max_connections`)
- **Valid characters**: Letters, digits, underscores

### Comments

```scala
// Single-line comment
/* Multi-line 
   comment */
```

---

## Type System Reference

### Primitive Types

| Type | C Mapping | Description |
|------|-----------|-------------|
| `int` | `int64_t` | 64-bit signed integer |
| `float` | `double` | Double-precision float |
| `bool` | `bool` | Boolean (true/false, 1/0) |
| `string` | `const char*` | Interned UTF-8 string |
| `path` | `const char*` | File system path |
| `ipv4` | `cfg_ipv4_t` | IPv4 address (4 octets) |

### Enumerations

Fixed set of named values:

```scala
schema Logger {
    level: enum(DEBUG, INFO, WARN, ERROR) { default: INFO }
}
```

### Arrays

Any type can be made into an array using `[]`:

```scala
schema Cluster {
    node_ips: string[]
    ports: int[]
}
```

**C Mapping:** `struct { T *data; size_t count; }`

### Schema Composition

Embed one schema within another:

```scala
schema DB { host: string }
schema App { database: DB {} }
```

---

## Configuration File Format (INI)

### Section Naming Rules

cfgsafe uses dot-notation for hierarchy:

- **Root fields**: `[SchemaName]`
- **Nested sections**: `[SchemaName.section_name]`
- **Embedded schemas**: `[ParentSchema.field_name]`

**Example:**
```ini
[App]
name = "MyApp"

[App.database]
host = "localhost"
```

---

## Property System

### Universal Properties

- `default`: Fallback value (e.g., `default: 8080`)
- `env`: Environment variable override (e.g., `env: "PORT"`)
- `required`: Fails if no value provided (e.g., `required: true`)
- `secret`: Redacts value in `_print()` output (e.g., `secret: true`)

### Validation Properties

- **Numeric**: `range: 1..10`, `min: 0`, `max: 100`
- **String/Array**: `min_length: 1`, `max_length: 255`
- **String/Path**: `pattern: "^[a-z]+$"` (Regex)
- **Path**: `exists: true` (File existence check)
- **Conditional**: `required_if: other_field == true`

### Custom Validation Hooks

Reference C functions for complex logic:

```scala
schema Custom {
    port: int { hook: "validate_port" }
}
```

**Signature:** `bool validate_port(const void *val, cfg_error_t *err);`

---

## Configuration Loading and Precedence

### Precedence Order (Highest to Lowest)

1. **CLI Arguments**: `--SchemaName.field=value`
2. **Environment Variables**: Defined via `env:` property
3. **INI File**: Values from `[Section]`
4. **Defaults**: Defined via `default:` property

### CLI Argument Syntax

- **Basic**: `--SchemaName.field=value`
- **Boolean**: `--SchemaName.flag` (sets to true)
- **Arrays**: `--SchemaName.list=val1,val2,val3`

---

## Complete Property Reference Table

| Property | Types | Description | Example |
|----------|-------|-------------|---------|
| `default` | All | Fallback value | `default: 8080` |
| `env` | All | Environment variable name | `env: "DATABASE_URL"` |
| `required` | All | Must be provided | `required: true` |
| `secret` | All | Redact in print output | `secret: true` |
| `range` | `int`, `float` | Min and max (inclusive) | `range: 1024..65535` |
| `min` | `int`, `float` | Minimum value | `min: 0` |
| `max` | `int`, `float` | Maximum value | `max: 100` |
| `min_length` | `string`, `path`, `array` | Minimum length/count | `min_length: 3` |
| `max_length` | `string`, `path`, `array` | Maximum length/count | `max_length: 255` |
| `pattern` | `string`, `path` | Regular expression | `pattern: "^[A-Z]+$"` |
| `exists` | `path` | File must exist | `exists: true` |
| `hook` | All | Custom validation function | `hook: "validate_url"` |
| `required_if` | All | Conditional requirement | `required_if: mode == ADVANCED` |