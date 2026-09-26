# Makefile for gsm module host-based testing
#
# Usage:
#   make                - build ring_buff test binary
#   make run            - build and run ring_buff tests
#   make run_all        - ring_buff + cold boot ladder + restart loop + hang
#   make clean          - remove build artifacts
#
# Host GCC build for GSM recovery and ring-buffer scenarios.

include ../../make/common.mk

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g
# Uretim .c dosyalarini include eden testler: legacy kod -Wconversion
# altinda derlenmediginden bu hedeflerde sade flag seti kullanilir.
CFLAGS_PROD := -std=c11 -Wall -O2 -g -DNO_CONSOLE_LOG
BUILD_DIR := build

# Kernel + uygulama include yollari (uretim dosyalarini include edenler)
APP_DIR := ../../../Application
INC := -I$(APP_DIR)/gsm -I$(APP_DIR) -I$(APP_DIR)/bsp \
       -I$(APP_DIR)/cslog -I$(APP_DIR)/libs -I$(APP_DIR)/libiec104 \
       -I$(APP_DIR)/rf -I../../../contiki-kernel
KERNEL_SRC := ../../../contiki-kernel/sys/process.c \
              ../../../contiki-kernel/sys/etimer.c \
              ../../../contiki-kernel/sys/timer.c \
              ../../../contiki-kernel/sys/clock.c

SRC := $(APP_DIR)/gsm/ring_buff.c
TEST_SRC := test_ring_buff.c

SRC_OBJ := $(BUILD_DIR)/ring_buff.o
TEST_OBJ := $(BUILD_DIR)/test_ring_buff.o

TARGET := $(BUILD_DIR)/test_ring_buff
TARGET_LADDER := $(BUILD_DIR)/test_coldboot_ladder
TARGET_LOOP := $(BUILD_DIR)/test_restart_loop
TARGET_WTD := $(BUILD_DIR)/test_wtd_liveness

.PHONY: all run clean run_all run_coldboot run_coldboot_hang run_restart_loop run_wtd_liveness

all: $(TARGET)

$(TARGET): $(SRC_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR):
	$(MKDIR_P) $(BUILD_DIR)

$(SRC_OBJ): $(SRC) $(APP_DIR)/gsm/ring_buff.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_OBJ): $(TEST_SRC) $(APP_DIR)/gsm/ring_buff.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(APP_DIR)/gsm -c $< -o $@

run: $(TARGET)
	./$(TARGET)

# --- Kurtarma merdiveni karar testleri (gsm_init.c include edilir) ---

$(TARGET_LADDER): test_coldboot_ladder.c $(APP_DIR)/gsm/gsm_init.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_PROD) $(INC) -o $@ $<

run_coldboot: $(TARGET_LADDER)
	./$(TARGET_LADDER)

# Haklar bitince adim while(1)'e dusmeli: timeout ile oldurulmesi
# (cikis 124) BASARI sayilir.
run_coldboot_hang: $(TARGET_LADDER)
	ruby ../../scripts/expect_timeout.rb 3 ./$(TARGET_LADDER) hang

# --- Surec yeniden baslatma dongusu (gercek Contiki cekirdegi) -------

$(TARGET_LOOP): test_restart_loop.c $(APP_DIR)/gsm_process_contiki.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_PROD) $(INC) -o $@ $< $(KERNEL_SRC)

run_restart_loop: $(TARGET_LOOP)
	./$(TARGET_LOOP)

# --- Stuck-FREE liveness (gsm_wtd.c include edilir) -----------------

$(TARGET_WTD): test_wtd_liveness.c $(APP_DIR)/gsm/gsm_wtd.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_PROD) $(INC) -o $@ $<

run_wtd_liveness: $(TARGET_WTD)
	./$(TARGET_WTD)

run_all: run run_coldboot run_coldboot_hang run_restart_loop run_wtd_liveness
	@echo "=== tum gsm host testleri bitti ==="

clean:
	$(RM_RF) $(BUILD_DIR)
