#ifndef VALIDATORS_H
#define VALIDATORS_H

#include <stdbool.h>
#include <string.h>


static bool validate_db_name(const void *val, cfg_error_t *err) {
    const char *name = (const char *)val;
    
    if (name && strcmp(name, "forbidden") == 0) {
        if (err) {
            strncpy(err->message, "database name 'forbidden' is not allowed", sizeof(err->message) - 1);
            strncpy(err->field, "database.host", sizeof(err->field) - 1);
        }
        return false;
    }
    return true;
}

#endif
