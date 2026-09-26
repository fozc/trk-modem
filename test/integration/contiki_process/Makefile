CC = gcc
CXX = g++

include ../../make/common.mk

ifeq ($(HOST_WINDOWS),1)
TARGET_EXEC ?= contiki_host.exe
else
TARGET_EXEC ?= contiki_host
endif

CONTIKI_DIR ?= ../../../contiki-kernel
include $(CONTIKI_DIR)/Makefile.contiki

SRCS := $(CONTIKI_DIR)/arch/cpu/x86/timer.c \
        $(CONTIKI_DIR)/sys/autostart.c \
        $(CONTIKI_DIR)/sys/clock.c \
        $(CONTIKI_DIR)/sys/etimer.c \
        $(CONTIKI_DIR)/sys/process.c \
        $(CONTIKI_DIR)/sys/timer.c

BUILD_DIR ?= build
SRC_DIRS += .
INC_DIRS += .

SRCS += main.c
CONTIKI_SRCS := $(filter $(CONTIKI_DIR)/%,$(SRCS))
LOCAL_SRCS := $(filter-out $(CONTIKI_SRCS),$(SRCS))
OBJS := $(patsubst $(CONTIKI_DIR)/%.c,$(BUILD_DIR)/contiki/%.o,$(CONTIKI_SRCS))
OBJS += $(patsubst %.c,$(BUILD_DIR)/local/%.o,$(LOCAL_SRCS))
DEPS := $(OBJS:.o=.d)


INC_FLAGS := $(addprefix -I,$(INC_DIRS))

CPPFLAGS ?= $(INC_FLAGS) -MMD -MP

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/contiki/%.o: $(CONTIKI_DIR)/%.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/local/%.o: %.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@


.PHONY: run clean

run: $(BUILD_DIR)/$(TARGET_EXEC)
	ruby ../../scripts/expect_timeout.rb 1 ./$(BUILD_DIR)/$(TARGET_EXEC)

clean:
	$(RM_RF) $(BUILD_DIR)

-include $(DEPS)
