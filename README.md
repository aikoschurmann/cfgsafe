# cfgsafe — Safe, validated C configuration

### This is only an idea for now
**cfgsafe** is a small C library + code generator that turns a programmer‑defined schema into a validated, typed `struct` your program can use with zero runtime failure paths. The generator reads schema file you write in a schema file, produces `*.h`/`*.c` with defaults + validation + parsing glue, and your program simply calls a generated `load` function at startup.

---

## Quick start (example)

### `config.schema` (what you write)

```scala
import "validators.h" // to include this file in the generated one so port_check etc work 

schema AppConfig {
    port: int {
        default: 8080
        range: 1..65535
        validate: validators.port_check
    }

    threshold: float {
        default: 0.5
        range: 0.0..1.0
    }
    
    log_level: enum(debug, info, warn, error) { 
        default: info
    }
    
    cert_path: path { 
        required: true
        exists: true 
    }
    
    section database {
        user: string { required: true }
        
        backup_nodes: [string] {
            min_length: 1
        }
    }
}
```

### Run generator

```bash
cfgsafe-gen config.schema
# generates app_config.h + app_config.c
```

### `main.c` (runtime)

```c
#include "app_config.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    AppConfig cfg;
    char err[256];

    // app.conf will be parsed to set values
    if (cfg_load(&cfg, "app.conf", err, sizeof(err)) != 0) {
        fprintf(stderr, "Config error: %s\n", err);
        return 1;
    }

    printf("Server running on %s:%d\n", cfg.host, cfg.port);
    if (cfg.debug) printf("Debug mode enabled\n");
}
```

---

## Example of generated code

Here is the kind of output `cfgsafe-gen` will produce (shortened for clarity):

**`app_config.h`**

```c
#pragma once
#include <stdbool.h>
#include <stddef.h>

// generated because config.schema imports validators.h for custom hooks
#include "validators.h"

// Enums are generated as native C types for fast switching
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

// Arrays include a 'count' so validators know the exact size
typedef struct {
    char** items;
    size_t count;
} StringArray;

typedef struct {
    char* user;
    StringArray backup_nodes;
} DatabaseSection;

typedef struct {
    int port;
    float threshold;
    LogLevel log_level;
    char* cert_path;
    DatabaseSection database;
} AppConfig;

// The load function returns non-zero on any validation failure
int cfg_load(AppConfig *cfg, const char *path, char *err, size_t err_len);
```

**`app_config.c`**

```c
#include "app_config.h"
#include <stdio.h>
#include <string.h>

static void set_defaults(AppConfig *cfg) {
    cfg->port = 8080;
    cfg->threshold = 0.5f;
    cfg->log_level = LOG_LEVEL_INFO;
    cfg->database.user = NULL; 
}

int cfg_load(AppConfig *cfg, const char *path, char *err, size_t err_len) {
    set_defaults(cfg);

    parse_init(cfg, path, err, err_len);

    // Then automatically generated validation code based on the schema:

    // 1.  Range Checks (min: 1, max: 65535)
    if (cfg->port < 1 || cfg->port > 65535) {
        snprintf(err, err_len, "port out of range: %d (1..65535)", cfg->port);
        return -1;
    }

    // 2. Custom Validator Hooks
    int success = port_check(cfg->port);
    if (!success) {
        snprintf(err, err_len, "custom validation failed for port: %d", cfg->port);
        return -1;
    }

    // 3. Float Range Checks (0.0..1.0)
    if (cfg->threshold < 0.0f || cfg->threshold > 1.0f) {
        snprintf(err, err_len, "threshold out of range: %f (0.0..1.0)", cfg->threshold);
        return -1;
    }

    // 4. Required Field Checks
    if (!cfg->database.user) {
        snprintf(err, err_len, "missing required field: database.user");
        return -1;
    }

    // 5. Array Length Verification (min_length: 1)
    if (cfg->database.backup_nodes.count < 1) {
        snprintf(err, err_len, "database.backup_nodes must contain at least 1 node");
        return -1;
    }

    return 0; // Success: AppConfig is now guaranteed to be valid
}
```

This generated code is plain C, easy to read and inspect.