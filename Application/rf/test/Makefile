# Makefile for RF config module host-based testing
#
# Usage:
#   make        - build test binary
#   make run    - build and run tests
#   make clean  - remove build artifacts
#
# Pattern follows Application/libiec104/test (host gcc + mocks).

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g
BUILD_DIR := build

# Directories
SRC_DIR := ..
APP_DIR := ../..
LIBS_DIR := ../../libs
GSM_DIR := ../../gsm
BSP_DIR := ../../bsp
CSLOG_DIR := ../../cslog
IEC104_DIR := ../../libiec104

# Include paths (rf_config.h -> rf_types.h -> modem_types.h -> gsm/bsp/xprintf chain)
INCLUDES := -I$(SRC_DIR) -I$(APP_DIR) -I$(LIBS_DIR) -I$(GSM_DIR) -I$(BSP_DIR) \
            -I$(CSLOG_DIR) -I$(IEC104_DIR)

# Sources
RF_CONFIG_SRC := $(SRC_DIR)/rf_config.c
MOCK_NVRAM_SRC := mock_nvram.c
TEST_SRC := test_rf_config.c

# Objects
RF_CONFIG_OBJ := $(BUILD_DIR)/rf_config.o
MOCK_NVRAM_OBJ := $(BUILD_DIR)/mock_nvram.o
TEST_OBJ := $(BUILD_DIR)/test_rf_config.o

TARGET := $(BUILD_DIR)/test_rf_config

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(RF_CONFIG_OBJ) $(MOCK_NVRAM_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(RF_CONFIG_OBJ): $(RF_CONFIG_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(MOCK_NVRAM_OBJ): $(MOCK_NVRAM_SRC) mock_nvram.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TEST_OBJ): $(TEST_SRC) mock_nvram.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
