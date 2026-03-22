#define CONFIG_IMPLEMENTATION
#include "generated_integration.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    TestSuite_t cfg;
    cfg_error_t err;
    int tests_passed = 0;
    int total_tests = 0;

    // Create temporary file for exists check
    FILE *tmp = fopen("/tmp/integration_log.txt", "w");
    if (tmp) fclose(tmp);

    printf("--- Extensive Integration Testing ---\n\n");

    // Test 1: Perfect Load
    total_tests++;
    printf("Test %d: Perfect Load with 4-level nesting...\n", total_tests);
    if (TestSuite_load(&cfg, "integration_perfect.ini", argc, (const char**)argv, &err) == CFG_SUCCESS) {
        if (strcmp(cfg.project_name, "My-Great-Project") == 0 &&
            cfg.version == 42 &&
            cfg.level1.level2.level3.level4.depth == 10 &&
            cfg.level1.level2.level3.level4.tags.count == 3) {
            printf("PASSED: Correct data loaded from deep hierarchy.\n");
            tests_passed++;
        } else {
            printf("FAILED: Data mismatch in perfect load.\n");
        }
        TestSuite_free(&cfg);
    } else {
        printf("FAILED: Failed to load perfect INI. Error: %s\n", err.message);
    }

    // Test 2: Syntax Error Handling
    total_tests++;
    printf("\nTest %d: Syntax Error Handling (Missing ] and =)...\n", total_tests);
    if (TestSuite_load(&cfg, "integration_broken_syntax.ini", argc, (const char**)argv, &err) == CFG_ERR_SYNTAX) {
        printf("PASSED: Caught syntax errors. Msg: %s, Line: %zu\n", err.message, err.line);
        tests_passed++;
    } else {
        printf("FAILED: Did not catch syntax errors correctly.\n");
        TestSuite_free(&cfg);
    }

    // Test 3: Validation Failures
    total_tests++;
    printf("\nTest %d: Validation Failures (Range, Pattern, RequiredIf)...\n", total_tests);
    if (TestSuite_load(&cfg, "integration_broken_validation.ini", argc, (const char**)argv, &err) == CFG_ERR_VALIDATION) {
        printf("PASSED: Caught validation errors. Field: %s, Msg: %s\n", err.field, err.message);
        tests_passed++;
    } else {
        printf("FAILED: Did not catch validation errors correctly.\n");
        TestSuite_free(&cfg);
    }

    // Test 4: Environment Overrides
    total_tests++;
    printf("\nTest %d: Environment Overrides...\n", total_tests);
    setenv("BIND_ADDR", "10.0.0.5", 1);
    if (TestSuite_load(&cfg, "integration_perfect.ini", argc, (const char**)argv, &err) == CFG_SUCCESS) {
        if (cfg.level1.level2.bind_addr.octets[0] == 10 && cfg.level1.level2.bind_addr.octets[3] == 5) {
            printf("PASSED: Correctly overrode bind_addr via env.\n");
            tests_passed++;
        } else {
            printf("FAILED: Env override failed. Got %d.%d.%d.%d\n", 
                cfg.level1.level2.bind_addr.octets[0], cfg.level1.level2.bind_addr.octets[1],
                cfg.level1.level2.bind_addr.octets[2], cfg.level1.level2.bind_addr.octets[3]);
        }
        TestSuite_free(&cfg);
    } else {
        printf("FAILED: Failed to load perfect INI with env override. Error: %s\n", err.message);
    }

    // Test 5: Defaults Verification (Partial INI)
    total_tests++;
    printf("\nTest %d: Defaults Verification (Partial INI)...\n", total_tests);
    if (TestSuite_load(&cfg, "integration_defaults.ini", argc, (const char**)argv, &err) == CFG_SUCCESS) {
        if (strcmp(cfg.project_name, "New-Project") == 0 &&
            cfg.version == 1 &&
            cfg.level1.level2.level3.level4.depth == 4) {
            printf("PASSED: Defaults correctly applied from schema.\n");
            tests_passed++;
        } else {
            printf("FAILED: Defaults mismatch. Got: %s, %lld, %lld\n", 
                cfg.project_name, (long long)cfg.version, (long long)cfg.level1.level2.level3.level4.depth);
        }
        TestSuite_free(&cfg);
    } else {
        printf("FAILED: Failed to load defaults INI. Error: %s, Field: %s\n", err.message, err.field);
    }

    // Test 6: Array Stress (Whitespace trimming)
    total_tests++;
    printf("\nTest %d: Array Stress (Whitespace trimming)...\n", total_tests);
    if (TestSuite_load(&cfg, "integration_array_stress.ini", argc, (const char**)argv, &err) == CFG_SUCCESS) {
        if (cfg.level1.level2.level3.level4.tags.count == 3 &&
            strcmp(cfg.level1.level2.level3.level4.tags.data[0], "tag1") == 0 &&
            strcmp(cfg.level1.level2.level3.level4.tags.data[1], "tag2") == 0 &&
            strcmp(cfg.level1.level2.level3.level4.tags.data[2], "tag3") == 0) {
            printf("PASSED: Array tags correctly trimmed and loaded.\n");
            tests_passed++;
        } else {
            printf("FAILED: Array mismatch. Count: %zu, [0]: '%s'\n", 
                cfg.level1.level2.level3.level4.tags.count, cfg.level1.level2.level3.level4.tags.data[0]);
        }
        TestSuite_free(&cfg);
    } else {
        printf("FAILED: Failed to load array stress INI. Error: %s\n", err.message);
    }

    // Test 7: Type Mismatch (String where Int expected)
    total_tests++;
    printf("\nTest %d: Type Mismatch (String for Int)...\n", total_tests);
    if (TestSuite_load(&cfg, "integration_type_mismatch.ini", argc, (const char**)argv, &err) == CFG_ERR_VALIDATION) {
        printf("PASSED: Caught validation error for mismatched type. Field: %s\n", err.field);
        tests_passed++;
    } else {
        printf("FAILED: Should have failed validation due to out-of-range (0).\n");
        TestSuite_free(&cfg);
    }

    // Test 8: Missing File
    total_tests++;
    printf("\nTest %d: Missing File Handling...\n", total_tests);
    if (TestSuite_load(&cfg, "non_existent.ini", argc, (const char**)argv, &err) == CFG_ERR_OPEN_FILE) {
        printf("PASSED: Gracefully handled missing file. Error: %s\n", err.message);
        tests_passed++;
    } else {
        printf("FAILED: Should have returned CFG_ERR_OPEN_FILE.\n");
    }

    // Test 9: Edge Case Stress (Multi-schema, Booleans, Escapes)
    total_tests++;
    printf("\nTest %d: Edge Case Stress (Booleans, Cross-schema)...\n", total_tests);
    EdgeCaseTest_t edge;
    if (EdgeCaseTest_load(&edge, "integration_edgecases.ini", argc, (const char**)argv, &err) == CFG_SUCCESS) {
        if (edge.flag_true == true &&
            edge.flag_1 == true &&
            edge.flag_false == false &&
            edge.flag_0 == false &&
            edge.sub.nested_id == 555 &&
            edge.sub.active == false &&
            edge.int_array.count == 2 &&
            strcmp(edge.weird.key_with_dash, "some-value") == 0) {
            printf("PASSED: Handled diverse booleans, cross-schema, and nested fields.\n");
            tests_passed++;
        } else {
            printf("FAILED: Data mismatch in edge case test.\n");
            printf("  flags: %d %d %d %d\n", edge.flag_true, edge.flag_1, edge.flag_false, edge.flag_0);
            printf("  sub: %lld %d\n", edge.sub.nested_id, edge.sub.active);
            printf("  int_array count: %zu\n", edge.int_array.count);
            printf("  dash: %s\n", edge.weird.key_with_dash);
        }
        EdgeCaseTest_free(&edge);
    } else {
        printf("FAILED: Failed to load edge case INI. Error: %s, Field: %s\n", err.message, err.field);
    }

    // Test 10: Secret Redaction
    total_tests++;
    printf("\nTest %d: Secret Redaction...\n", total_tests);
    if (TestSuite_load(&cfg, "integration_perfect.ini", argc, (const char**)argv, &err) == CFG_SUCCESS) {
        FILE *mem = tmpfile();
        TestSuite_print(&cfg, mem);
        rewind(mem);
        char buf[4096];
        size_t n = fread(buf, 1, sizeof(buf)-1, mem);
        buf[n] = '\0';
        fclose(mem);
        
        if (strstr(buf, "api_key = ********") != NULL) {
            printf("PASSED: Secret field redacted in print output.\n");
            tests_passed++;
        } else {
            printf("FAILED: Secret field NOT redacted.\n");
            printf("Output was:\n%s\n", buf);
        }
        TestSuite_free(&cfg);
    } else {
        printf("FAILED: Load failed for redaction test.\n");
    }

    // Test 11: Custom Hook Success
    total_tests++;
    printf("\nTest %d: Custom Hook Success...\n", total_tests);
    if (EdgeCaseTest_load(&edge, "integration_edgecases.ini", argc, (const char**)argv, &err) == CFG_SUCCESS) {
        printf("PASSED: Hook accepted valid value.\n");
        tests_passed++;
        EdgeCaseTest_free(&edge);
    } else {
        printf("FAILED: Hook rejected valid value. Error: %s\n", err.message);
    }

    // Test 12: Custom Hook Failure
    total_tests++;
    printf("\nTest %d: Custom Hook Failure...\n", total_tests);
    if (EdgeCaseTest_load(&edge, "integration_hook_fail.ini", argc, (const char**)argv, &err) == CFG_ERR_VALIDATION) {
        printf("PASSED: Hook correctly rejected forbidden value.\n");
        tests_passed++;
    } else {
        printf("FAILED: Hook failed to reject forbidden value.\n");
        EdgeCaseTest_free(&edge);
    }

    // Test 13: Array Hook Success
    total_tests++;
    printf("\nTest %d: Array Hook Success (3 elements)...\n", total_tests);
    if (TestSuite_load(&cfg, "integration_perfect.ini", argc, (const char**)argv, &err) == CFG_SUCCESS) {
        printf("PASSED: Array hook accepted 3 elements.\n");
        tests_passed++;
        TestSuite_free(&cfg);
    } else {
        printf("FAILED: Array hook rejected 3 elements. Error: %s\n", err.message);
    }

    // Test 14: Array Hook Failure
    total_tests++;
    printf("\nTest %d: Array Hook Failure (not 3 elements)...\n", total_tests);
    if (TestSuite_load(&cfg, "integration_array_hook_fail.ini", argc, (const char**)argv, &err) == CFG_ERR_VALIDATION) {
        printf("PASSED: Array hook correctly rejected 2 elements.\n");
        tests_passed++;
    } else {
        printf("FAILED: Array hook failed to reject 2 elements.\n");
        TestSuite_free(&cfg);
    }

    // Test 15: Nested Hook Failure
    total_tests++;
    printf("\nTest %d: Nested Hook Failure (Recursive validation)...\n", total_tests);
    if (EdgeCaseTest_load(&edge, "integration_nested_hook_fail.ini", argc, (const char**)argv, &err) == CFG_ERR_VALIDATION) {
        printf("PASSED: Nested hook correctly rejected value.\n");
        tests_passed++;
    } else {
        printf("FAILED: Nested hook failed to reject value.\n");
        EdgeCaseTest_free(&edge);
    }

    // Test 16: Int Array Length Validation
    total_tests++;
    printf("\nTest %d: Int Array Length Validation...\n", total_tests);
    if (EdgeCaseTest_load(&edge, "integration_int_array_fail.ini", argc, (const char**)argv, &err) == CFG_ERR_VALIDATION) {
        if (strcmp(err.field, "int_array") == 0) {
            printf("PASSED: Correctly caught too short int array.\n");
            tests_passed++;
        } else {
            printf("FAILED: Caught validation error but on wrong field: %s\n", err.field);
        }
    } else {
        printf("FAILED: Should have failed validation due to int array length.\n");
        EdgeCaseTest_free(&edge);
    }

    // Test 17: CLI Overrides (The new feature!)
    total_tests++;
    printf("\nTest %d: CLI Overrides (CLI > ENV > INI)...\n", total_tests);
    const char *mock_argv[] = { "program", "--TestSuite.project_name", "CLI-Override", "--TestSuite.version", "99" };
    if (TestSuite_load(&cfg, "integration_perfect.ini", 5, mock_argv, &err) == CFG_SUCCESS) {
        if (strcmp(cfg.project_name, "CLI-Override") == 0 && cfg.version == 99) {
            printf("PASSED: CLI correctly overrode INI values.\n");
            tests_passed++;
        } else {
            printf("FAILED: CLI override failed. Got: %s, %lld\n", cfg.project_name, (long long)cfg.version);
        }
        TestSuite_free(&cfg);
    } else {
        printf("FAILED: Failed to load with CLI override. Error: %s\n", err.message);
    }

    // Test 18: CLI Boolean Toggle & Short Flags
    total_tests++;
    printf("\nTest %d: CLI Boolean Toggle & Short Flags...\n", total_tests);
    const char *mock_argv_2[] = { "program", "--TestSuite.level1.level2.level3.debug_mode", "-v", "77" };
    if (TestSuite_load(&cfg, "integration_perfect.ini", 4, mock_argv_2, &err) == CFG_SUCCESS) {
        if (cfg.level1.level2.level3.debug_mode == true && cfg.version == 77) {
            printf("PASSED: Boolean toggle and short flag (-v) verified.\n");
            tests_passed++;
        } else {
            printf("FAILED: CLI verification failed. Debug: %d, Version: %lld\n", 
                cfg.level1.level2.level3.debug_mode, (long long)cfg.version);
        }
        TestSuite_free(&cfg);
    } else {
        printf("FAILED: CLI test failed. Error: %s\n", err.message);
    }

    // Test 19: CLI Assignment Syntax (--flag=value)
    total_tests++;
    printf("\nTest %d: CLI Assignment Syntax (--flag=value)...\n", total_tests);
    const char *mock_argv_3[] = { "program", "--TestSuite.project_name=assignment-syntax" };
    if (TestSuite_load(&cfg, "integration_perfect.ini", 2, mock_argv_3, &err) == CFG_SUCCESS) {
        if (strcmp(cfg.project_name, "assignment-syntax") == 0) {
            printf("PASSED: Assignment syntax verified.\n");
            tests_passed++;
        } else {
            printf("FAILED: Assignment syntax failed. Got: %s\n", cfg.project_name);
        }
        TestSuite_free(&cfg);
    } else {
        printf("FAILED: CLI assignment syntax test failed. Error: %s\n", err.message);
    }

    // Test 20: CLI Enum and Array Flags
    total_tests++;
    printf("\nTest %d: CLI Enum and Array Flags...\n", total_tests);
    const char *mock_argv_4[] = { "program", "--TestSuite.global_level", "WARN", "--TestSuite.level1.level2.level3.level4.tags", "cli1,cli2,cli3" };
    if (TestSuite_load(&cfg, "integration_perfect.ini", 5, mock_argv_4, &err) == CFG_SUCCESS) {
        if (cfg.global_level == TestSuite_global_level_WARN &&
            cfg.level1.level2.level3.level4.tags.count == 3 &&
            strcmp(cfg.level1.level2.level3.level4.tags.data[2], "cli3") == 0) {
            printf("PASSED: Enum and Array CLI parsing verified.\n");
            tests_passed++;
        } else {
            printf("FAILED: Data mismatch. Enum: %d, Count: %zu\n", 
                (int)cfg.global_level, cfg.level1.level2.level3.level4.tags.count);
        }
        TestSuite_free(&cfg);
    } else {
        printf("FAILED: CLI Enum/Array test failed. Error: %s, Field: %s\n", err.message, err.field);
    }

    // Test 21: CLI Precedence (CLI > ENV)
    total_tests++;
    printf("\nTest %d: CLI Precedence (CLI > ENV)...\n", total_tests);
    setenv("BIND_ADDR", "1.1.1.1", 1);
    const char *mock_argv_5[] = { "program", "--TestSuite.level1.level2.bind_addr", "8.8.8.8" };
    if (TestSuite_load(&cfg, "integration_perfect.ini", 3, mock_argv_5, &err) == CFG_SUCCESS) {
        if (cfg.level1.level2.bind_addr.octets[0] == 8) {
            printf("PASSED: CLI overrode ENV correctly.\n");
            tests_passed++;
        } else {
            printf("FAILED: CLI precedence failure. Got %d.%d.%d.%d\n", 
                cfg.level1.level2.bind_addr.octets[0], cfg.level1.level2.bind_addr.octets[1],
                cfg.level1.level2.bind_addr.octets[2], cfg.level1.level2.bind_addr.octets[3]);
        }
        TestSuite_free(&cfg);
    } else {
        printf("FAILED: CLI precedence test failed. Error: %s\n", err.message);
    }

    printf("\n--- Test Results Summary ---\n");
    printf("%d / %d tests passed.\n", tests_passed, total_tests);

    remove("/tmp/integration_log.txt");

    return (tests_passed == total_tests) ? 0 : 1;
}
