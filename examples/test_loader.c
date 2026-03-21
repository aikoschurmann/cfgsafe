#define CONFIG_IMPLEMENTATION
#include "config.h"
#include <stdio.h>
#include <string.h>

int main() {
    ApiGateway_t cfg;
    cfg_error_t err;
    
    printf("--- Loading Example Configuration ---\n");
    cfg_status_t status = ApiGateway_load(&cfg, "config.ini", &err);
    
    if (status == CFG_SUCCESS) {
        printf("Status: SUCCESS\n");
        ApiGateway_print(&cfg, stdout);
        ApiGateway_free(&cfg);
    } else {
        printf("Status: FAILURE (code %d)\n", (int)status);
        printf("Error: %s\n", err.message);
        printf("Field: %s\n", err.field);
        if (err.line > 0) printf("Line: %zu\n", err.line);
        return 1;
    }

    return 0;
}
