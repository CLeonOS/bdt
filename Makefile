CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
LDLIBS ?=
BUILD_DIR ?= build

SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))
EXE := $(BUILD_DIR)/bdt

ifeq ($(OS),Windows_NT)
EXE := $(BUILD_DIR)/bdt.exe
else
LDLIBS += -ldl
endif

.PHONY: all clean

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c src/bdt.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
