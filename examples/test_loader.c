#define CONFIG_IMPLEMENTATION
#include "config.h"
#include <stdio.h>

int main() {
    ApiGateway_t cfg;
    
    printf("--- Loading Example Configuration ---\n");
    cfg_status_t status = ApiGateway_load(&cfg, "config.ini");
    
    if (status == CFG_SUCCESS) {
        printf("Status: SUCCESS\n");
        printf("Service Name: %s\n", cfg.service_name);
        printf("Bind Address: %d.%d.%d.%d\n", 
            cfg.bind_address.octets[0], cfg.bind_address.octets[1],
            cfg.bind_address.octets[2], cfg.bind_address.octets[3]);
        printf("Listen Port: %lld\n", cfg.listen_port);
        
        printf("\n[Database]\n");
        printf("  Host: %s\n", cfg.database.host);
        printf("  Port: %lld\n", cfg.database.port);
        
        printf("\n[Auth]\n");
        printf("  Enabled: %s\n", cfg.auth.enabled ? "true" : "false");
        printf("  Token TTL: %lld\n", cfg.auth.token_ttl);
        
        printf("\n[Logging]\n");
        printf("  File: %s\n", cfg.logging.file_path);
        
        printf("\n[Caching]\n");
        printf("  Enabled: %s\n", cfg.caching.enabled ? "true" : "false");
        printf("  Nodes count: %zu\n", cfg.caching.nodes.count);
        for (size_t i = 0; i < cfg.caching.nodes.count; i++) {
            printf("    - %s\n", cfg.caching.nodes.data[i]);
        }

        ApiGateway_free(&cfg);
    } else {
        printf("Status: FAILURE (code %d)\n", status);
        return 1;
    }

    return 0;
}
