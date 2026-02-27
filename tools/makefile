NAME    := cfg-gen
CC      := gcc
CFLAGS  := -Wall -Iinclude -g
SRC_DIR := src
OUT_DIR := out

SRCS    := $(shell find $(SRC_DIR) -name '*.c')
BIN     := $(OUT_DIR)/$(NAME)

all: $(BIN)

$(BIN): $(SRCS)
	@mkdir -p $(OUT_DIR)
	$(CC) $(CFLAGS) $(SRCS) -o $(BIN)

clean:
	rm -rf $(OUT_DIR)