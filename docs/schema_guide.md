# cfgsafe Schema Definition Guide

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

### Comments

```scala
// Single-line comment

/* 
   Multi-line comment
   spanning several lines
*/

schema Config {
    port: int  // Inline comments are allowed
}
```

---

## Type System Reference

### Primitive Types

cfgsafe provides six primitive types that map directly to C:

#### `int` → `int64_t`
64-bit signed integer for any whole number:

```scala
schema Limits {
    max_connections: int { default: 100, range: 1..10000 }
    retry_count: int { default: 3, min: 0 }
    offset: int  // Can be negative
}
```

#### `float` → `double`
Double-precision floating point for decimals:

```scala
schema Metrics {
    threshold: float { default: 0.95, range: 0.0..1.0 }
    timeout_seconds: float { default: 30.5 }
    scale_factor: float
}
```

#### `bool` → `bool`
Boolean flags accepting true/false or 1/0:

```scala
schema Features {
    debug_mode: bool { default: false }
    enable_cache: bool { default: true }
    verbose: bool
}
```

#### `string` → `const char*`
UTF-8 text strings (automatically interned in memory pool):

```scala
schema Server {
    hostname: string { default: "localhost" }
    api_key: string { 
        required: true, 
        pattern: "^[A-Za-z0-9]{32}$",
        secret: true  // Won't print in logs
    }
    region: string { min_length: 2, max_length: 10 }
}
```

#### `path` → `const char*`
File system paths with optional existence validation:

```scala
schema Paths {
    config_dir: path { default: "/etc/myapp" }
    log_file: path { default: "/var/log/app.log" }
    cert_file: path { required: true, exists: true }  // Must exist
}
```

#### `ipv4` → `cfg_ipv4_t`
IPv4 addresses stored as four octets:

```scala
schema Network {
    listen_addr: ipv4 { default: "127.0.0.1" }
    gateway: ipv4 { default: "192.168.1.1" }
    dns_server: ipv4
}
```

### Enumerations

Define a field that must be one of a fixed set of values:

```scala
schema Logger {
    level: enum(DEBUG, INFO, WARNING, ERROR, CRITICAL) {
        default: INFO
    }
    
    format: enum(JSON, TEXT, XML) {
        default: TEXT
    }
}
```

### Arrays

Any type can be made into an array using `[]` syntax:

```scala
schema Cluster {
    node_ips: string[] {
        min_length: 1,  // At least one node required
        max_length: 10
    }
    
    allowed_ports: int[]
    backup_servers: ipv4[]
    tags: string[]
}
```

### Schema Composition

You can use one schema as a field type in another schema, creating reusable configuration blocks:

```scala
// Define reusable schemas
schema DatabaseConfig {
    host: string { default: "localhost" }
    port: int { default: 5432 }
    username: string { required: true }
    password: string { required: true, secret: true }
    database: string { required: true }
    max_connections: int { default: 10, range: 1..100 }
}

schema RedisConfig {
    host: string { default: "localhost" }
    port: int { default: 6379 }
    db_index: int { default: 0 }
}

// Compose them into a larger configuration
schema Application {
    app_name: string { default: "MyApp" }
    
    // Embedded schema instances
    postgres: DatabaseConfig {}
    cache: RedisConfig {}
    
    section api {
        enabled: bool { default: true }
        rate_limit: int { default: 1000 }
    }
}
```

### Complete Realistic Example

Here's a full production-ready configuration schema:

**Schema (app.cfgsafe):**
```scala
schema DatabaseConfig {
    host: string { default: "localhost" }
    port: int { default: 5432, range: 1..65535 }
    username: string { required: true }
    password: string { required: true, secret: true }
    database: string { required: true }
    max_connections: int { default: 10, range: 1..200 }
    ssl_mode: enum(DISABLE, REQUIRE, VERIFY_CA, VERIFY_FULL) {
        default: REQUIRE
    }
}

schema WebConfig {
    name: string { default: "MyWebApp" }
    version: string { default: "1.0.0" }
    environment: enum(DEVELOPMENT, STAGING, PRODUCTION) {
        default: DEVELOPMENT
    }
    
    primary_db: DatabaseConfig {}
    
    section server {
        bind_address: ipv4 { default: "127.0.0.1" }
        port: int { default: 8080, range: 1024..65535 }
        workers: int { default: 4, range: 1..64 }
        request_timeout: float { default: 30.0, range: 1.0..300.0 }
    }
    
    section logging {
        level: enum(DEBUG, INFO, WARNING, ERROR) { default: INFO }
        file_path: path { required: true }
        max_file_size_mb: int { default: 100, range: 1..1000 }
        rotation_count: int { default: 5, range: 1..50 }
    }
    
    section security {
        allowed_origins: string[] {
            min_length: 1
        }
        api_keys: string[] {
            required: true,
            secret: true
        }
        rate_limit_per_minute: int { default: 60, range: 1..10000 }
    }
}
```

---

## Property System

Properties define constraints, defaults, and metadata for fields. They appear in `{}` after the type declaration.

### Universal Properties

These properties work on **all** field types:

#### `default`

Provides a fallback value when no value is specified in INI, ENV, or CLI:

```scala
schema Defaults {
    port: int { default: 8080 }
    debug: bool { default: false }
    name: string { default: "MyApp" }
    timeout: float { default: 30.5 }
    level: enum(LOW, MEDIUM, HIGH) { default: MEDIUM }
}
```

**Behavior:**
- If field is missing from all sources (INI, ENV, CLI), default is used
- If field appears in any source, that value is used instead

#### `env`

Allows environment variable override for this specific field:

```scala
schema Config {
    api_key: string {
        env: "API_KEY",
        secret: true
    }
    
    database_url: string {
        env: "DATABASE_URL",
        required: true
    }
    
    port: int {
        default: 8080,
        env: "PORT"
    }
}
```

**Precedence (highest to lowest):**
1. CLI argument: `--Config.port=9000`
2. Environment variable: `PORT=9000`
3. INI file: `port = 9000`
4. Default: `default: 8080`

**Usage:**
```bash
export API_KEY="secret_key_here"
export DATABASE_URL="postgresql://localhost/db"
export PORT=3000
./myapp
```

#### `required`

Validation fails if this field is not provided by any source:

```scala
schema Mandatory {
    database_url: string { required: true }
    api_token: string { required: true }
    
    // OK to have both required and default
    // (means "use default if missing, but validate it exists")
    admin_email: string {
        required: true,
        default: "admin@example.com",
        pattern: "^[^@]+@[^@]+\\.[^@]+$"
    }
}
```

**What counts as "provided":**
- Field has a value in INI file, OR
- Field has a value from environment variable, OR
- Field has a value from CLI argument, OR
- Field has a `default` property

**For strings/paths/arrays:**
- Empty string `""` counts as provided
- Empty array counts as provided
- Use `min_length: 1` if you need non-empty

#### `secret`

Redacts field value in `_print()` output (shows `********` instead):

```scala
schema Secrets {
    api_key: string { secret: true }
    password: string { secret: true }
    public_field: string  // Will be printed normally
}
```

**Print output:**
```
--- Secrets Configuration ---
api_key = ********
password = ********
public_field = "visible_value"
--------------------------
```

**Note:** This only affects the `_print()` function. The actual value is still accessible in your code as `config.api_key`.

### Numeric Properties (`int`, `float`)

#### `range`

Inclusive minimum and maximum bounds:

```scala
schema Ranges {
    port: int { range: 1024..65535 }
    percentage: float { range: 0.0..100.0 }
    workers: int { range: 1..64, default: 4 }
    temperature: float { range: -273.15..1000.0 }
}
```

**Validation:**
- `port = 80` fails (too low)
- `port = 8080` passes
- `port = 70000` fails (too high)

#### `min` and `max`

Alternative to `range` when you only need one bound:

```scala
schema Bounds {
    retry_count: int { min: 0, max: 10 }
    timeout: float { min: 0.1 }  // No maximum
    priority: int { max: 100 }   // No minimum (can be negative)
}
```

### String Properties (`string`, `path`)

#### `min_length` and `max_length`

Character count constraints:

```scala
schema StringLengths {
    username: string {
        min_length: 3,
        max_length: 20,
        required: true
    }
    
    country_code: string {
        min_length: 2,
        max_length: 2  // Exactly 2 characters
    }
    
    description: string {
        max_length: 500
    }
}
```

**Validation:**
- `username = "ab"` fails (too short)
- `username = "alice"` passes
- `username = "this_is_a_very_long_username_that_exceeds_limit"` fails (too long)

#### `pattern`

Regular expression matching (cfgsafe's built-in regex engine):

```scala
schema Patterns {
    email: string {
        required: true,
        pattern: "^[^@]+@[^@]+\\.[^@]+$"
    }
    
    hex_color: string {
        pattern: "^#[0-9A-Fa-f]{6}$"
    }
    
    version: string {
        pattern: "^[0-9]+\\.[0-9]+\\.[0-9]+$"  // Semantic version
    }
    
    alphanumeric: string {
        pattern: "^[A-Za-z0-9]+$"
    }
}
```

**Supported regex features:**
- `^` — anchor to start
- `$` — anchor to end
- `.` — any character
- `[abc]` — character class (a, b, or c)
- `[a-z]` — character range
- `[^abc]` — negated class (not a, b, or c)
- `+` — one or more
- `*` — zero or more
- `?` — zero or one

**Examples:**
- `email = "user@example.com"` passes
- `email = "invalid"` fails
- `hex_color = "#FF5733"` passes
- `hex_color = "FF5733"` fails (missing #)

### Path-Specific Properties

#### `exists`

Validates that the file or directory exists on the filesystem:

```scala
schema Paths {
    config_file: path {
        required: true,
        exists: true  // Must exist when validation runs
    }
    
    cert_file: path {
        exists: true
    }
    
    log_dir: path {
        default: "/var/log/myapp",
        exists: true
    }
    
    output_file: path {
        required: true
        // No exists: true, because we're creating it
    }
}
```

**Validation:**
- Checks using `access()` on Unix, `_access()` on Windows
- Validation fails if file/directory doesn't exist
- Useful for certificates, config includes, data directories

### Array Properties

#### `min_length` and `max_length`

Element count constraints (not character length):

```scala
schema Arrays {
    nodes: string[] {
        min_length: 1,     // At least one node
        max_length: 10,    // Maximum 10 nodes
        required: true
    }
    
    backup_servers: ipv4[] {
        min_length: 2      // Require at least 2 backups
    }
    
    tags: string[] {
        max_length: 20     // No more than 20 tags
    }
}
```

**Validation:**
- `nodes = ` (empty) fails (min_length: 1)
- `nodes = host1` passes
- `nodes = h1, h2, ..., h11` fails (max_length: 10)

### Custom Validation Hooks

For complex validation logic that can't be expressed with built-in properties, you can reference custom C functions:

```scala
import "validators.h"

schema Custom {
    url: string {
        required: true,
        hook: "validate_url"
    }
    
    credit_card: string {
        required: true,
        pattern: "^[0-9]{16}$",
        hook: "validate_luhn_checksum"
    }
    
    ports: int[] {
        hook: "validate_port_range_no_conflicts"
    }
}
```

**Hook function signature:**
```c
bool validate_url(const void *val, cfg_error_t *err);
bool validate_luhn_checksum(const void *val, cfg_error_t *err);
bool validate_port_range_no_conflicts(const void *val, cfg_error_t *err);
```

**Hook implementation (validators.h):**
```c
#include <stdbool.h>
#include "config.h"

// For string/path: receive pointer directly
bool validate_url(const void *val, cfg_error_t *err) {
    const char *url = (const char *)val;
    if (!url) return false;
    
    if (strncmp(url, "http://", 7) != 0 && 
        strncmp(url, "https://", 8) != 0) {
        cfg_set_error(err, "URL must start with http:// or https://", "url", 0);
        return false;
    }
    return true;
}

// For int/float/bool: receive pointer to value
bool validate_positive_even(const void *val, cfg_error_t *err) {
    int64_t n = *(int64_t *)val;
    if (n <= 0 || n % 2 != 0) {
        cfg_set_error(err, "must be positive and even", "number", 0);
        return false;
    }
    return true;
}

// For arrays: receive pointer to struct { T *data; size_t count; }
bool validate_port_range_no_conflicts(const void *val, cfg_error_t *err) {
    struct { int64_t *data; size_t count; } *arr = (void *)val;
    
    // Check for duplicates
    for (size_t i = 0; i < arr->count; i++) {
        for (size_t j = i + 1; j < arr->count; j++) {
            if (arr->data[i] == arr->data[j]) {
                cfg_set_error(err, "duplicate port numbers not allowed", "ports", 0);
                return false;
            }
        }
    }
    return true;
}
```

### Conditional Requirements

The `required_if` property makes a field required only when a condition is met:

```scala
schema Conditional {
    mode: enum(SIMPLE, ADVANCED) { default: SIMPLE }
    
    // Only required if mode is ADVANCED
    advanced_setting: string {
        required_if: mode == ADVANCED
    }
    
    ssl_enabled: bool { default: false }
    
    // Only required if SSL is enabled
    cert_path: path {
        required_if: ssl_enabled == true,
        exists: true
    }
    
    key_path: path {
        required_if: ssl_enabled == true,
        exists: true
    }
}
```

**Validation examples:**
```ini
# This passes (simple mode, no advanced_setting needed)
[Conditional]
mode = SIMPLE
ssl_enabled = false

# This fails (advanced mode but advanced_setting missing)
[Conditional]
mode = ADVANCED

# This passes (advanced mode with required field)
[Conditional]
mode = ADVANCED
advanced_setting = "some_value"

# This fails (SSL enabled but cert missing)
[Conditional]
ssl_enabled = true

# This passes
[Conditional]
ssl_enabled = true
cert_path = "/etc/ssl/cert.pem"
key_path = "/etc/ssl/key.pem"
```

---

## Configuration Loading and Precedence

### The Load Sequence

When you call `SchemaName_load()`, values are resolved in this strict order (highest priority first):

1. **CLI Arguments** (highest priority)
   - Format: `--SchemaName.field.path=value`
   - Boolean flags: `--SchemaName.debug` (sets to true)
   - Arrays: `--SchemaName.nodes=ip1,ip2,ip3`

2. **Environment Variables**
   - Only if `env: "VAR_NAME"` is explicitly defined
   - Overrides INI file
   - Overridden by CLI

3. **INI File**
   - Read from `[SectionName]` sections
   - Overrides defaults
   - Overridden by ENV and CLI

4. **Schema Defaults** (lowest priority)
   - From `default:` property
   - Used only if no other source provides value

### CLI Argument Syntax

Every field in your schema automatically gets a CLI flag based on its path:

**Schema:**
```scala
schema App {
    port: int { default: 8080 }
    debug: bool { default: false }
    
    section database {
        host: string { default: "localhost" }
        timeout: int { default: 30 }
    }
    
    nodes: string[]
}
```

**Generated CLI flags:**
```bash
# Basic fields
--App.port=9000
--App.port 9000        # Space-separated also works

# Boolean flags (just presence sets to true)
--App.debug

# Nested sections
--App.database.host=db.example.com
--App.database.timeout=60

# Arrays (comma-separated)
--App.nodes=node1,node2,node3
--App.nodes=10.0.0.1,10.0.0.2
```

**Complete example:**
```bash
./myapp \
  --App.port=9000 \
  --App.debug \
  --App.database.host=prod-db.internal \
  --App.database.timeout=120 \
  --App.nodes=10.0.0.1,10.0.0.2,10.0.0.3
```

### Precedence Example

**Schema:**
```scala
schema Server {
    port: int { 
        default: 8080,
        env: "PORT"
    }
}
```

**INI file:**
```ini
[Server]
port = 9000
```

**Environment:**
```bash
export PORT=7000
```

**CLI:**
```bash
./app --Server.port=3000
```

**Result:** `config.port = 3000` (CLI wins)

**Without CLI:**
```bash
./app
```
**Result:** `config.port = 7000` (ENV wins)

**Without CLI or ENV:**
```bash
unset PORT
./app
```
**Result:** `config.port = 9000` (INI wins)

**With nothing:**
```bash
unset PORT
./app  # No INI file
```
**Result:** `config.port = 8080` (default wins)

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