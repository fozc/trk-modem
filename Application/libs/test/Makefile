# Host tests for the xsnprintf return contract.
#
#   make          - build
#   make run      - build and run
#   make clean    - remove build artifacts

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Wshadow -O1 -g
BUILD   := build

LIB_DIR := ..
INCLUDES := -I$(LIB_DIR)

TEST_SRCS := test_xsnprintf.c
LIB_SRCS  := $(LIB_DIR)/xprintf.c

TEST_OBJS := $(addprefix $(BUILD)/,$(TEST_SRCS:.c=.o))
LIB_OBJS  := $(BUILD)/xprintf.o

TARGET := $(BUILD)/test_xsnprintf

.PHONY: all run clean

all: $(TARGET)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/xprintf.o: $(LIB_DIR)/xprintf.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(TEST_OBJS) $(LIB_OBJS)
	$(CC) $^ -o $@

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILD)
