# Host tests for the nvram_sync persistence contract.
#
#   make          - build
#   make run      - build and run
#   make clean    - remove build artifacts

include ../../make/common.mk

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Wshadow -O1 -g
BUILD   := build

APP_DIR := ../../../Application

INCLUDES := -I. -I$(APP_DIR) -I$(APP_DIR)/libs -I$(APP_DIR)/bsp \
            -I$(APP_DIR)/cslog -I$(APP_DIR)/rf -I$(APP_DIR)/gsm \
            -I$(APP_DIR)/libiec104 -I$(APP_DIR)/libscp \
            -I$(APP_DIR)/power_board -I$(APP_DIR)/bms

# The target build carries no warning flags yet, so nvram.c may trip
# -Wunused-*. Warnings stay visible here but do not fail the test run.
# NO_CONSOLE_LOG compiles the real CSLOG macros out of nvram.c.
LIB_CFLAGS := $(CFLAGS) -Wno-error -Wno-unused-variable \
              -Wno-unused-function -Wno-sign-compare -DNO_CONSOLE_LOG

TEST_SRCS := test_nvram_sync.c mock_platform.c
LIB_SRCS  := $(APP_DIR)/nvram.c \
             $(APP_DIR)/libs/crc32.c \
             $(APP_DIR)/libiec104/iec104_util.c \
             $(APP_DIR)/gsm/utils.c \
             $(APP_DIR)/rf/rf_config.c

# Explicit object list: gsm/utils.c needs its own rule, otherwise the
# generic $(APP_DIR)/%.c pattern matches Application/utils.c for the
# basename "utils.o".
TEST_OBJS := $(addprefix $(BUILD)/,$(TEST_SRCS:.c=.o))
LIB_OBJS  := $(BUILD)/nvram.o $(BUILD)/crc32.o $(BUILD)/iec104_util.o \
             $(BUILD)/gsm_utils.o $(BUILD)/rf_config.o

TARGET := $(BUILD)/test_nvram_sync

.PHONY: all run clean

all: $(TARGET)

$(BUILD):
	@$(MKDIR_P) $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/%.o: $(APP_DIR)/%.c | $(BUILD)
	$(CC) $(LIB_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/%.o: $(APP_DIR)/libs/%.c | $(BUILD)
	$(CC) $(LIB_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/%.o: $(APP_DIR)/libiec104/%.c | $(BUILD)
	$(CC) $(LIB_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/%.o: $(APP_DIR)/rf/%.c | $(BUILD)
	$(CC) $(LIB_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/gsm_utils.o: $(APP_DIR)/gsm/utils.c | $(BUILD)
	$(CC) $(LIB_CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(TEST_OBJS) $(LIB_OBJS)
	$(CC) $(TEST_OBJS) $(LIB_OBJS) -o $@

run: $(TARGET)
	@./$(TARGET)

clean:
	@$(RM_RF) $(BUILD)
