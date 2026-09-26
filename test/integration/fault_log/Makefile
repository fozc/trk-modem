# Host tests for the fault_log feeder switch / dual-copy contract.
#
#   make          - build
#   make run      - build and run
#   make clean    - remove build artifacts

include ../../make/common.mk

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Wshadow -O1 -g \
           -DNO_CONSOLE_LOG -DNO_SHELL_LOG
BUILD   := build

APP_DIR := ../../../Application

INCLUDES := -I. -I$(APP_DIR) -I$(APP_DIR)/libs -I$(APP_DIR)/bsp \
            -I$(APP_DIR)/cslog -I$(APP_DIR)/rf -I$(APP_DIR)/gsm \
            -I$(APP_DIR)/libiec104 -I$(APP_DIR)/libscp \
            -I$(APP_DIR)/power_board -I$(APP_DIR)/bms

# The target build carries no warning flags yet, so fault_log.c may trip
# -Wunused-*. Warnings stay visible here but do not fail the test run.
# NO_CONSOLE_LOG / NO_SHELL_LOG compile the logging macros out.
LIB_CFLAGS := $(CFLAGS) -Wno-error -Wno-unused-parameter \
              -Wno-unused-variable -Wno-unused-but-set-variable \
              -DNO_CONSOLE_LOG -DNO_SHELL_LOG

TEST_SRCS := test_fault_log.c mock_platform.c
LIB_SRCS  := $(APP_DIR)/fault_log.c \
             $(APP_DIR)/libs/crc32.c \
             $(APP_DIR)/libiec104/cp56time2a.c

TEST_OBJS := $(addprefix $(BUILD)/,$(TEST_SRCS:.c=.o))
LIB_OBJS  := $(BUILD)/fault_log.o $(BUILD)/crc32.o \
             $(BUILD)/cp56time2a.o

TARGET := $(BUILD)/test_fault_log

.PHONY: all run clean

all: $(TARGET)

$(BUILD):
	@$(MKDIR_P) $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/fault_log.o: $(APP_DIR)/fault_log.c | $(BUILD)
	$(CC) $(LIB_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/crc32.o: $(APP_DIR)/libs/crc32.c | $(BUILD)
	$(CC) $(LIB_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/cp56time2a.o: $(APP_DIR)/libiec104/cp56time2a.c | $(BUILD)
	$(CC) $(LIB_CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(TEST_OBJS) $(LIB_OBJS)
	$(CC) $(TEST_OBJS) $(LIB_OBJS) -o $@

run: $(TARGET)
	@./$(TARGET)

clean:
	@$(RM_RF) $(BUILD)
