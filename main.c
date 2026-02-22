#include "include/cfgsafe/schema.h"

int validate(const void *val);

CFGSAFE_HEADER("test.h");
CFGSAFE_SCHEMA(AppConfig,
    CFGSAFE_INT(port, .default_value = 8080, .min = 1, .max = 65535, .validate = validate);
    CFGSAFE_FLOAT(threshold, .default_value = 0.5f, .min = 0.0f, .max = 1.0f);
    CFGSAFE_ENUM(log_level, .default_value = "info", .valid_values = "debug,info,warn,error");
    CFGSAFE_PATH(cert_path, .must_exist = true, .required = true);
    CFGSAFE_SECTION(database,
        CFGSAFE_STRING(user, .required = true);
        CFGSAFE_ARRAY(backup_nodes, .min_elems = 1);
    )
);
