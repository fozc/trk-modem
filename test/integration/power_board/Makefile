# Makefile for power_board_decode module host-based testing
#
# Usage:
#   make        - build test binary
#   make run    - build and run tests
#   make clean  - remove build artifacts
#
# Host GCC build for power-board frame decoding.

include ../../make/common.mk

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g
BUILD_DIR := build

SRC := ../../../Application/power_board/power_board_decode.c
TEST_SRC := test_power_board_decode.c

SRC_OBJ := $(BUILD_DIR)/power_board_decode.o
TEST_OBJ := $(BUILD_DIR)/test_power_board_decode.o

TARGET := $(BUILD_DIR)/test_power_board_decode

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR):
	$(MKDIR_P) $(BUILD_DIR)

$(SRC_OBJ): $(SRC) ../../../Application/power_board/power_board_decode.h ../../../Application/power_board/power_board.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I../../../Application/power_board -c $< -o $@

$(TEST_OBJ): $(TEST_SRC) ../../../Application/power_board/power_board_decode.h ../../../Application/power_board/power_board.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I../../../Application/power_board -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	$(RM_RF) $(BUILD_DIR)
