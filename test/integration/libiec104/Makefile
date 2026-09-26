# Host tests for the libiec104 protocol core.
#
#   make          - build
#   make run      - build and run
#   make clean    - remove build artifacts

include ../../make/common.mk

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Wshadow -O1 -g
BUILD   := build

LIB_DIR := ../../../Application/libiec104
APP_DIR := ../../../Application

INCLUDES := -I. -I$(LIB_DIR) -I$(APP_DIR) -I$(APP_DIR)/libs -I$(APP_DIR)/bsp \
            -I$(APP_DIR)/cslog -I$(APP_DIR)/rf -I$(APP_DIR)/gsm \
            -I$(APP_DIR)/libscp -I$(APP_DIR)/power_board -I$(APP_DIR)/bms

# The library sources build clean under the flags above; keep it that way.
LIB_CFLAGS := $(CFLAGS)

TEST_SRCS := test_iec104_protocol.c mock_platform.c
LIB_SRCS  := $(LIB_DIR)/iec104.c \
             $(LIB_DIR)/iec104_config.c \
             $(LIB_DIR)/iec104_util.c \
             $(LIB_DIR)/cp56time2a.c

TEST_OBJS := $(addprefix $(BUILD)/,$(TEST_SRCS:.c=.o))
LIB_OBJS  := $(addprefix $(BUILD)/,$(notdir $(LIB_SRCS:.c=.o)))

TARGET := $(BUILD)/test_iec104_protocol

.PHONY: all run clean

all: $(TARGET)

$(BUILD):
	@$(MKDIR_P) $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/%.o: $(LIB_DIR)/%.c | $(BUILD)
	$(CC) $(LIB_CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(TEST_OBJS) $(LIB_OBJS)
	$(CC) $(TEST_OBJS) $(LIB_OBJS) -o $@

run: $(TARGET)
	@./$(TARGET)

clean:
	@$(RM_RF) $(BUILD)
