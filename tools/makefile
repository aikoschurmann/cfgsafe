NAME    := cfg-gen
CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -std=c99 -Iinclude -g
SRC_DIR := src
OUT_DIR := out

SRCS    := $(shell find $(SRC_DIR) -name '*.c')
BIN     := $(OUT_DIR)/$(NAME)

TEST_DIR    := tests/integration
TEST_RUNNER := $(OUT_DIR)/integration_runner
TEST_SCHEMA := $(TEST_DIR)/integration.schema
TEST_HEADER := $(TEST_DIR)/generated_integration.h
INVALID_DIR := tests/invalid_schemas

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRCS)
	@mkdir -p $(OUT_DIR)
	$(CC) $(CFLAGS) $(SRCS) -o $(BIN)

test: $(BIN)
	@echo "--- Generating config header ---"
	./$(BIN) -o $(TEST_HEADER) $(TEST_SCHEMA)
	@echo "--- Compiling test runner ---"
	$(CC) $(CFLAGS) -Iinclude -I$(TEST_DIR) $(TEST_DIR)/integration_runner.c -o $(TEST_RUNNER)
	@echo "--- Running integration tests ---"
	cd $(TEST_DIR) && ../../$(TEST_RUNNER)
	@echo "--- Checking that invalid schemas are rejected ---"
	@fail=0; for s in $(INVALID_DIR)/*.schema; do \
		if ./$(BIN) -o $(OUT_DIR)/invalid.h $$s >/dev/null 2>&1; then echo "FAILED: accepted $$s"; fail=1; \
		else echo "PASSED: rejected $$s"; fi; \
	done; rm -f $(OUT_DIR)/invalid.h; exit $$fail

clean:
	rm -rf $(OUT_DIR)
	rm -f $(TEST_HEADER)
