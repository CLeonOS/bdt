CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
BUILD_DIR ?= build

SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))
EXE := $(BUILD_DIR)/bdt

ifeq ($(OS),Windows_NT)
EXE := $(BUILD_DIR)/bdt.exe
endif

.PHONY: all clean

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(OBJ) -o $@

$(BUILD_DIR)/%.o: src/%.c src/bdt.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
