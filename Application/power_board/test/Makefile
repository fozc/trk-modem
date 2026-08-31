# Makefile for power_board_decode module host-based testing
#
# Usage:
#   make        - build test binary
#   make run    - build and run tests
#   make clean  - remove build artifacts
#
# Pattern follows Application/rf/test (host gcc).

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g
BUILD_DIR := build

SRC := ../power_board_decode.c
TEST_SRC := test_power_board_decode.c

SRC_OBJ := $(BUILD_DIR)/power_board_decode.o
TEST_OBJ := $(BUILD_DIR)/test_power_board_decode.o

TARGET := $(BUILD_DIR)/test_power_board_decode

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SRC_OBJ): $(SRC) ../power_board_decode.h ../power_board.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I.. -c $< -o $@

$(TEST_OBJ): $(TEST_SRC) ../power_board_decode.h ../power_board.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I.. -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
