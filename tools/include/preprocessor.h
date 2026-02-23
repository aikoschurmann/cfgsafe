#ifndef CFGSAFE_PREPROCESSOR_H
#define CFGSAFE_PREPROCESSOR_H

#include <stddef.h>

/**
 * Runs the preprocessor via a pipe and captures the generated tokens dynamically.
 * @param argc Original argument count
 * @param argv Original argument vector
 * @param out_size Pointer to store the exact byte size of the returned data
 * @return Allocated string containing the preprocessed data, or NULL on failure.
 * The caller is responsible for calling free() on the returned pointer.
 */
char* preprocess_schema(int argc, const char *argv[], size_t *out_size);

#endif /* CFGSAFE_PREPROCESSOR_H */