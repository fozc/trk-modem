# Makefile for web-server RF JSON host-based testing
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
RF_DIR := ../../rf
RF_TEST_DIR := ../../rf/test
APP_DIR := ../..
LIBS_DIR := ../../libs
GSM_DIR := ../../gsm
BSP_DIR := ../../bsp
CSLOG_DIR := ../../cslog
IEC104_DIR := ../../libiec104
MODBUS_DIR := ../../libmodbusrtu

# Include paths
INCLUDES := -I$(SRC_DIR) -I$(RF_DIR) -I$(APP_DIR) -I$(LIBS_DIR) -I$(GSM_DIR) \
            -I$(BSP_DIR) -I$(CSLOG_DIR) -I$(IEC104_DIR) -I$(MODBUS_DIR) \
            -I$(RF_TEST_DIR)

# Sources
RF_JSON_SRC := $(SRC_DIR)/rf_json.c
RF_CONFIG_SRC := $(RF_DIR)/rf_config.c
RF_DISCOVERY_SRC := $(RF_DIR)/rf_discovery.c
XPRINTF_SRC := $(LIBS_DIR)/xprintf.c
MOCK_NVRAM_SRC := $(RF_TEST_DIR)/mock_nvram.c
TEST_SRC := test_rf_json.c

# Objects
RF_JSON_OBJ := $(BUILD_DIR)/rf_json.o
RF_CONFIG_OBJ := $(BUILD_DIR)/rf_config.o
RF_DISCOVERY_OBJ := $(BUILD_DIR)/rf_discovery.o
XPRINTF_OBJ := $(BUILD_DIR)/xprintf.o
MOCK_NVRAM_OBJ := $(BUILD_DIR)/mock_nvram.o
TEST_OBJ := $(BUILD_DIR)/test_rf_json.o

TARGET := $(BUILD_DIR)/test_rf_json

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(RF_JSON_OBJ) $(RF_CONFIG_OBJ) $(RF_DISCOVERY_OBJ) $(XPRINTF_OBJ) $(MOCK_NVRAM_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(RF_JSON_OBJ): $(RF_JSON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(RF_CONFIG_OBJ): $(RF_CONFIG_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(RF_DISCOVERY_OBJ): $(RF_DISCOVERY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(XPRINTF_OBJ): $(XPRINTF_SRC) | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -g $(INCLUDES) -c $< -o $@

$(MOCK_NVRAM_OBJ): $(MOCK_NVRAM_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TEST_OBJ): $(TEST_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
