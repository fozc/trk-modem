# Only the deliberately non-returning terminal state needs an external process.
include ../../make/common.mk
CC := gcc
APP_DIR := ../../../Application
BUILD_DIR := build
TARGET_HANG := $(BUILD_DIR)/test_coldboot_hang
CFLAGS := -std=c11 -Wall -O2 -g -DNO_CONSOLE_LOG $(TARGET_ABI_FLAGS)
INC := -I$(APP_DIR)/gsm -I$(APP_DIR) -I$(APP_DIR)/bsp \
       -I$(APP_DIR)/cslog -I$(APP_DIR)/libs -I$(APP_DIR)/libiec104 \
       -I$(APP_DIR)/rf -I../../../contiki-kernel
.PHONY: run run_all run_coldboot run_restart_loop run_coldboot_hang clean
$(BUILD_DIR):
	$(MKDIR_P) $(BUILD_DIR)
$(TARGET_HANG): test_coldboot_hang.c ../../support/gsm_coldboot_fixture.inc $(APP_DIR)/gsm/gsm_init.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INC) -o $@ $<
run_coldboot:
	$(MAKE) -C ../.. cases TEST_PATTERN=gsm_coldboot
run_restart_loop:
	$(MAKE) -C ../.. cases TEST_PATTERN=gsm_restart_loop
run_coldboot_hang: $(TARGET_HANG)
	ruby ../../scripts/expect_timeout.rb 3 ./$(TARGET_HANG)
run: run_all
run_all: run_coldboot run_restart_loop run_coldboot_hang
clean:
	@echo "Use test/Makefile clean for Ceedling and external artifacts"
