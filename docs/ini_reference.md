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

## Value Types

### Primitives
- **`int`**: Standard integers (e.g., `42`, `-10`).
- **`float`**: Floating point numbers (e.g., `3.14`, `0.5`).
- **`bool`**: Accepts `true`/`false` or `1`/`0`.
- **`string` / `path`**: UTF-8 text. Can be quoted (`"value"`) or unquoted. Quotes are recommended for values containing spaces.
- **`ipv4`**: Standard dot-decimal notation (e.g., `127.0.0.1`).

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
