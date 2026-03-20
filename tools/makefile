NAME    := cfg-gen
CC      := gcc
CFLAGS  := -Wall -Iinclude -g
SRC_DIR := src
OUT_DIR := out

SRCS    := $(shell find $(SRC_DIR) -name '*.c')
BIN     := $(OUT_DIR)/$(NAME)

TEST_DIR    := tests/integration
TEST_RUNNER := $(OUT_DIR)/integration_runner
TEST_SCHEMA := $(TEST_DIR)/integration.schema
TEST_HEADER := $(TEST_DIR)/generated_integration.h

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

clean:
	rm -rf $(OUT_DIR)
	rm -f $(TEST_HEADER)
