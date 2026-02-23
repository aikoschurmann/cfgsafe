#include <stdio.h>
#include <stdlib.h>
#include "preprocessor.h"

int main(int argc, char const *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <schema_file.c> [extra compiler flags...]\n", argv[0]);
        return 1;
    }

    size_t data_size = 0;
    
    // Preprocessing Phase
    char *schema_data = preprocess_schema(argc, argv, &data_size);
    if (schema_data == NULL) {
        return 1;
    }

    printf("Successfully captured %zu bytes at pointer %p\n", data_size, (void*)schema_data);
    printf("Data:\n%s\n", schema_data);


    // Clean up memory
    free(schema_data);
    return 0;
}