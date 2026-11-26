/* The README's "Use in C" example, built and run by `make test`. */
#define CONFIG_IMPLEMENTATION
#include "config.h"

int main(int argc, const char **argv) {
    Config_t cfg;
    cfg_error_t err;

    if (Config_load(&cfg, "config.ini", argc, argv, &err) == CFG_SUCCESS) {
        printf("App: %s on port %d\n", cfg.service_name, (int)cfg.db.port);
        Config_free(&cfg);
    } else {
        fprintf(stderr, "Error in %s: %s\n", err.field, err.message);
        return 1;
    }
}
