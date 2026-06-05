# Makefile for IEC104 Event Queue Host-Based Testing
# 
# Usage:
#   make simple        - Build simple structure validation test
#   make comprehensive - Build comprehensive functional tests
#   make all           - Build all tests
#   make run-simple    - Build and run simple test
#   make run-comp      - Build and run comprehensive test
#   make clean         - Remove build artifacts

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -g
BUILD_DIR := build

# Directories
SRC_DIR := ..
LIBS_DIR := ../../libs
BSP_DIR := ../../bsp

# Include paths
INCLUDES := -I$(SRC_DIR) -I. -I$(LIBS_DIR) -I$(BSP_DIR)

# Source files
MOCK_FLASH_SRC := mock_flash.c
MOCK_BSP_SRC := mock_bsp.c
IEC104_QUEUE_SRC := $(SRC_DIR)/iec104_event_queue.c
CRC_SRC := $(LIBS_DIR)/crc32.c

# Object files
MOCK_FLASH_OBJ := $(BUILD_DIR)/mock_flash.o
MOCK_BSP_OBJ := $(BUILD_DIR)/mock_bsp.o
IEC104_QUEUE_OBJ := $(BUILD_DIR)/iec104_event_queue.o
CRC_OBJ := $(BUILD_DIR)/crc32.o

# Simple test
SIMPLE_SRC := iec104_test_simple.c
SIMPLE_OBJ := $(BUILD_DIR)/iec104_test_simple.o
SIMPLE_TARGET := $(BUILD_DIR)/iec104_test_simple

# Comprehensive test
COMP_SRC := iec104_test_comprehensive.c
COMP_OBJ := $(BUILD_DIR)/iec104_test_comprehensive.o
COMP_TARGET := $(BUILD_DIR)/iec104_test_comprehensive
COMP_DEPS := $(MOCK_FLASH_OBJ) $(MOCK_BSP_OBJ) $(IEC104_QUEUE_OBJ) $(CRC_OBJ)

.PHONY: all simple comprehensive run-simple run-comp clean help

all: simple comprehensive

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Simple structure validation test
simple: $(SIMPLE_TARGET) 

$(SIMPLE_TARGET): $(BUILD_DIR) $(SIMPLE_OBJ)
	@echo "Linking $(SIMPLE_TARGET)..."
	@$(CC) $(SIMPLE_OBJ) -o $(SIMPLE_TARGET)
	@echo "Build complete!"

$(SIMPLE_OBJ): $(SIMPLE_SRC)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Comprehensive functional test
comprehensive: $(COMP_TARGET)

$(COMP_TARGET): $(BUILD_DIR) $(COMP_OBJ) $(COMP_DEPS)
	@echo "Linking $(COMP_TARGET)..."
	@$(CC) $(COMP_OBJ) $(COMP_DEPS) -o $(COMP_TARGET)
	@echo "Build complete!"

$(COMP_OBJ): $(COMP_SRC)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Mock objects
$(MOCK_FLASH_OBJ): $(MOCK_FLASH_SRC)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(MOCK_BSP_OBJ): $(MOCK_BSP_SRC)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# IEC104 queue source
$(IEC104_QUEUE_OBJ): $(IEC104_QUEUE_SRC)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) -DTEST_BUILD -c $< -o $@

# CRC source
$(CRC_OBJ): $(CRC_SRC)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Run tests
run-simple: simple
	@echo ""
	@echo "Running simple structure test..."
	@echo "================================"
	@$(SIMPLE_TARGET)

run-comp: comprehensive
	@echo ""
	@echo "Running comprehensive test suite..."
	@echo "==================================="
	@$(COMP_TARGET)

# Clean build artifacts
clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete."

# Help
help:
	@echo "IEC104 Event Queue Test Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make simple        - Build simple structure test"
	@echo "  make comprehensive - Build comprehensive functional tests"
	@echo "  make all           - Build all tests"
	@echo "  make run-simple    - Build and run simple test"
	@echo "  make run-comp      - Build and run comprehensive test"
	@echo "  make clean         - Remove build artifacts"
	@echo "  make help          - Show this help"
