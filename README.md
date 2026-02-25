# cfgsafe — Safe, validated C configuration

### This is only an idea for now
**cfgsafe** is a small C library + code generator that turns a programmer‑defined schema into a validated, typed `struct` your program can use with zero runtime failure paths. The generator reads the schema from file, produces `*.h`/`*.c` with defaults + validation, and your program simply calls a generated `cfg_load` function at startup.

---

## Quick start (example)

### `config.schema` (what you write)

```scala
import "validators.h"

schema ServerConfig {
    // Environment Variable Override & Defaults
    port: int {
        default: 8080
        env: "SERVER_PORT" 
        range: 1024..65535
    }

    // Advanced Network Types
    bind_address: ipv4 {
        default: "0.0.0.0"
    }

    // Security & Validation (Secret fields are masked in logs)
    api_key: string {
        required: true
        env: "API_KEY"
        secret: true
        pattern: "^[A-F0-9]{32}$"
        max_length: 32
    }

    // Cross-field Validation
    enable_tls: bool { default: false }
    cert_path: path {
        required_if: enable_tls == true
        exists: true
    }

    // Developer Experience (Deprecation warnings)
    v1_compatibility_mode: bool {
        default: false
        deprecated: true
    }

    section database {
        host: string { default: "localhost" }
        user: string { required: true }
        
        backup_nodes: string[] {
            min_length: 1
        }
    }
}
```

### Run generator

```bash
cfgsafe-gen config.schema
# generates server_config.h + server_config.c
```

### `main.c` (runtime)

```c
#include "server_config.h"
#include <stdio.h>

int main(void) {
    ServerConfig cfg;
    char err[256];

    if (cfg_load(&cfg, "server.conf", err, sizeof(err)) != 0) {
        fprintf(stderr, "Config error: %s\n", err);
        return 1;
    }

    printf("Server listening on %s:%d\n", cfg.bind_address, cfg.port);
}
```

---

## Example of generated code

The generator abstracts complex logic into helper functions to keep the API clean and the implementation readable.

**`server_config.h`** (Excerpt)

```c
typedef struct {
    char* host;
    char* user;
    struct { char** items; size_t count; } backup_nodes;
} DatabaseSection;

typedef struct {
    int port;
    char* bind_address;
    char* api_key;
    bool enable_tls;
    char* cert_path;
    bool v1_compatibility_mode;
    DatabaseSection database;
} ServerConfig;

int cfg_load(ServerConfig *cfg, const char *path, char *err, size_t err_len);
```

**`server_config.c`** (Excerpt)

```c
int cfg_load(ServerConfig *cfg, const char *path, char *err, size_t err_len) {
    set_defaults(cfg);

    // 1. Load from environment first
    cfg_internal_env_int("SERVER_PORT", &cfg->port);
    cfg_internal_env_str("API_KEY", &cfg->api_key);

    // 2. Parse the config file...
    if (parse_file(cfg, path, err, err_len) != 0) return -1;

    // 3. Validation (Abstracted into internal helpers)
    if (!cfg_validate_range_int(cfg->port, 1024, 65535)) {
        snprintf(err, err_len, "port %d is out of range (1024..65535)", cfg->port);
        return -1;
    }

    if (!cfg_validate_pattern(cfg->api_key, "^[A-F0-9]{32}$")) {
        snprintf(err, err_len, "api_key: invalid format (expected 32-char hex)");
        return -1;
    }

    // 4. Cross-field logic
    if (cfg->enable_tls && (cfg->cert_path == NULL)) {
        snprintf(err, err_len, "cert_path is required when enable_tls is enabled");
        return -1;
    }

    return 0;
}
```
