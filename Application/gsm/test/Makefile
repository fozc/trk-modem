# Makefile for ring_buff module host-based testing
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

SRC := ../ring_buff.c
TEST_SRC := test_ring_buff.c

SRC_OBJ := $(BUILD_DIR)/ring_buff.o
TEST_OBJ := $(BUILD_DIR)/test_ring_buff.o

TARGET := $(BUILD_DIR)/test_ring_buff

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SRC_OBJ): $(SRC) ../ring_buff.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_OBJ): $(TEST_SRC) ../ring_buff.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I.. -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
