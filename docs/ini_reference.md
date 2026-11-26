# INI Configuration Reference

This guide explains how `cfgsafe` maps your schema definitions to the INI configuration file format.

## Section Naming Rules

`cfgsafe` uses INI sections with dot-notation to represent the hierarchical structure of your schema. The section name must match your schema structure **exactly**.

### Root Schema Fields
Fields at the root of a schema map to a section named after the schema.

**Schema:**
```scala
schema Server {
    port: int
    host: string
}
```

**INI:**
```ini
[Server]
port = 8080
host = "0.0.0.0"
```

### Sections (Nested Groups)
Use dot-notation: `[SchemaName.section_name]`.

**Schema:**
```scala
schema App {
    version: string
    
    section database {
        url: string
        timeout: int
    }
}
```

**INI:**
```ini
[App]
version = "1.0.0"

[App.database]
url = "postgresql://localhost/db"
timeout = 30
```

### Embedded Schemas
Use dot-notation: `[ParentSchema.field_name]`.

**Schema:**
```scala
schema Database {
    host: string
}

schema Service {
    primary_db: Database {}
    backup_db: Database {}
}
```

**INI:**
```ini
[Service.primary_db]
host = "db1.example.com"

[Service.backup_db]
host = "db2.example.com"
```

### Deeply Nested Structures
Keep using dot-notation for any depth: `[Config.network.ssl]`.

---

## Comments

Inline comments are supported using `;` or `#`. Comments can appear on their own lines or after values.

```ini
# This is a comment
[Server]
port = 8080 ; This is an inline comment
host = "127.0.0.1" # This is also an inline comment
```

---

## Value Types

### Primitives
- **`int`**: Standard integers (e.g., `42`, `-10`).
- **`float`**: Floating point numbers (e.g., `3.14`, `0.5`).
- **`bool`**: Accepts `true`/`false` or `1`/`0`.
- **`string` / `path`**: UTF-8 text. Can be quoted (`"value"`) or unquoted. Quotes are recommended for values containing spaces.
- **`ipv4`**: Standard dot-decimal notation (e.g., `127.0.0.1`).

Values are parsed strictly: a value that is not a complete, valid literal of the field's type (for example `80abc` for an `int`, `yes` for a `bool`, or `1.2.3.4.5` for an `ipv4`) makes loading fail with `CFG_ERR_VALIDATION`, naming the field and line. The same rules apply to values from environment variables and the command line.

Keys that do not correspond to a schema field, inside any section that belongs to the schema (for example a misspelled `prot = 80` under `[Config.db]`), also fail the load with `CFG_ERR_VALIDATION`. Sections belonging to other schemas, and keys before the first section header, are ignored. To accept unknown keys (and unknown `--Schema.` command-line options), define `CFG_ALLOW_UNKNOWN_KEYS` before including the implementation:

```c
#define CFG_ALLOW_UNKNOWN_KEYS
#define CONFIG_IMPLEMENTATION
#include "config.h"
```

Setting the same key twice in a section, including through a repeated section header, fails with `CFG_ERR_SYNTAX` (`duplicate key 'port' (already set on line 3)`). Entries of an array of structs (`[[...]]`) each have their own keys, and environment and command-line values may override INI values. Define `CFG_ALLOW_DUPLICATE_KEYS` to let the last value win instead.

### Enumerations
Use the literal name defined in the schema (case-sensitive).

**Schema:**
```scala
schema Log {
    level: enum(DEBUG, INFO, ERROR)
}
```

**INI:**
```ini
[Log]
level = INFO
```

### Arrays
Arrays are represented as comma-separated values.

**Schema:**
```scala
schema Cluster {
    nodes: string[]
    ports: int[]
}
```

**INI:**
```ini
[Cluster]
nodes = host1, host2, host3
ports = 80, 443, 8080
```

Elements can be quoted, and commas inside a quoted element do not split it:

```ini
labels = "eu-west, primary", backup   ; 2 elements: "eu-west, primary" and "backup"
```

If the whole value is a single quoted string, the quotes are removed before splitting (`"a, b"` is two elements). To write a one-element list whose element contains a comma, add a trailing comma: `labels = "eu-west, primary",`. Empty elements are skipped.

---

## Complete Example

**Schema:**
```scala
schema DatabaseConfig {
    host: string { default: "localhost" }
    port: int { default: 5432 }
    ssl_mode: enum(DISABLE, REQUIRE, VERIFY_FULL) { default: REQUIRE }
}

schema WebConfig {
    primary_db: DatabaseConfig {}
    
    section server {
        bind_address: ipv4 { default: "127.0.0.1" }
        port: int { default: 8080 }
    }
}
```

**INI:**
```ini
[WebConfig.primary_db]
host = "db-prod-01.internal.example.com"
port = 5432
ssl_mode = VERIFY_FULL

[WebConfig.server]
bind_address = 0.0.0.0
port = 443
```

---

## Precedence
Values in the INI file are the base configuration. They can be overridden by:
1. **CLI Arguments**: `--Schema.path.field=value`
2. **Environment Variables**: If the field has an `env` property.

See the [Schema Guide](schema_guide.md) for more details.
