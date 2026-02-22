#ifndef CFGSAFE_SCHEMA_H
#define CFGSAFE_SCHEMA_H

#ifndef CFGSAFE_CLEAN_OUTPUT
    // Normal Compilation
    #include <stddef.h>
    #include <stdbool.h>

    /* Standard signature for custom validator hooks */
    typedef int (*__cfgsafe_validate_fn)(const void *value);

    struct __cfgsafe_str_opts { 
        const char *default_value; 
        size_t max_len; 
        bool required; 
        __cfgsafe_validate_fn validate; 
    };

    struct __cfgsafe_int_opts { 
        int default_value; 
        int min; int max; 
        bool required; 
        __cfgsafe_validate_fn validate; 
    };

    struct __cfgsafe_bool_opts { 
        bool default_value; 
        bool required; 
        __cfgsafe_validate_fn validate; 
    };

    struct __cfgsafe_float_opts { 
        float default_value; 
        float min; float max; 
        bool required; 
        __cfgsafe_validate_fn validate; 
    };

    struct __cfgsafe_path_opts { 
        const char *default_value; 
        bool must_exist; 
        bool required; 
        __cfgsafe_validate_fn validate; 
    };

    struct __cfgsafe_enum_opts { 
        const char *default_value; 
        const char *valid_values; 
        bool required; 
        __cfgsafe_validate_fn validate; 
    };

    struct __cfgsafe_array_opts { 
        const char *default_value; 
        size_t min_elems; 
        size_t max_elems; 
        __cfgsafe_validate_fn validate; 
    };

    #define CFGSAFE_STRING(name, ...) struct __cfgsafe_str_opts name##_opts = { __VA_ARGS__ };
    #define CFGSAFE_INT(name, ...)    struct __cfgsafe_int_opts name##_opts = { __VA_ARGS__ };
    #define CFGSAFE_BOOL(name, ...)   struct __cfgsafe_bool_opts name##_opts = { __VA_ARGS__ };
    #define CFGSAFE_FLOAT(name, ...)  struct __cfgsafe_float_opts name##_opts = { __VA_ARGS__ };
    #define CFGSAFE_PATH(name, ...)   struct __cfgsafe_path_opts name##_opts = { __VA_ARGS__ };
    #define CFGSAFE_ENUM(name, ...)   struct __cfgsafe_enum_opts name##_opts = { __VA_ARGS__ };
    #define CFGSAFE_ARRAY(name, ...)  struct __cfgsafe_array_opts name##_opts = { __VA_ARGS__ };

    // verify if string
    #define CFGSAFE_HEADER(path) \
        static const char *__cfg_header_check_##__LINE__ __attribute__((unused)) = (path)

    #define CFGSAFE_SECTION(name, ...) __VA_ARGS__
    #define CFGSAFE_SCHEMA(name, ...)  __VA_ARGS__

#else
    // Generation

    /* Tells the generator to insert a #include into the generated .c file */
    #define CFGSAFE_HEADER(path) HEADER_INCLUDE path

    #define CFGSAFE_STRING(name, ...) STRING name = { __VA_ARGS__ }
    #define CFGSAFE_INT(name, ...)    INT name = { __VA_ARGS__ }
    #define CFGSAFE_BOOL(name, ...)   BOOL name = { __VA_ARGS__ }
    #define CFGSAFE_FLOAT(name, ...)  FLOAT name = { __VA_ARGS__ }
    #define CFGSAFE_PATH(name, ...)   PATH name = { __VA_ARGS__ }
    #define CFGSAFE_ENUM(name, ...)   ENUM name = { __VA_ARGS__ }
    #define CFGSAFE_ARRAY(name, ...)  ARRAY name = { __VA_ARGS__ }

    #define CFGSAFE_SECTION(name, ...) \
        SECTION_START name; \
        __VA_ARGS__ \
        SECTION_END name

    #define CFGSAFE_SCHEMA(name, ...) \
        SCHEMA_START name; \
        __VA_ARGS__ \
        SCHEMA_END name
#endif

#endif /* CFGSAFE_SCHEMA_H */