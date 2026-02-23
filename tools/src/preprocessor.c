#include "preprocessor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* preprocess_schema(int argc, const char *argv[], size_t *out_size) {
    const char *file_path = argv[1];
    char command[4096];

    // Build the command
    strcpy(command, "gcc -E -P -DCFGSAFE_CLEAN_OUTPUT");
    for (int i = 2; i < argc; i++) {
        strcat(command, " ");
        strcat(command, argv[i]);
    }
    strcat(command, " \"");
    strcat(command, file_path);
    strcat(command, "\"");

    // Open the pipe
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        perror("Error: Failed to execute preprocessor");
        return NULL;
    }

    // Prepare dynamic buffer
    size_t capacity = 4096;
    size_t length = 0;
    char *schema_data = malloc(capacity);
    if (schema_data == NULL) {
        perror("Error: Failed to allocate memory");
        pclose(fp);
        return NULL;
    }

    // Read the stream
    while (1) {
        size_t bytes_read = fread(schema_data + length, 1, capacity - length - 1, fp);
        length += bytes_read;

        if (feof(fp)) break;

        if (length >= capacity - 1) {
            capacity *= 2;
            char *new_data = realloc(schema_data, capacity);
            if (new_data == NULL) {
                perror("Error: Memory reallocation failed");
                free(schema_data);
                pclose(fp);
                return NULL;
            }
            schema_data = new_data;
        }
    }

    // Check exit status
    int status = pclose(fp);
    if (status != 0) {
        fprintf(stderr, "Error: Preprocessor failed on '%s' (Exit status: %d)\n", file_path, status);
        fprintf(stderr, "Command executed: %s\n", command);
        free(schema_data);
        return NULL;
    }

    schema_data[length] = '\0';

    if (out_size != NULL) {
        *out_size = length;
    }

    return schema_data;
}