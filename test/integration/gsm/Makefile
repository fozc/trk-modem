# Makefile for gsm module host-based testing
#
# Usage:
#   make run_all        - cold boot ladder + restart loop + hang
#   make clean          - remove build artifacts
#
# Host GCC build for GSM recovery scenarios requiring a custom process host.

include ../../make/common.mk

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g \
          $(TARGET_ABI_FLAGS)
# Uretim .c dosyalarini include eden testler: legacy kod -Wconversion
# altinda derlenmediginden bu hedeflerde sade flag seti kullanilir.
CFLAGS_PROD := -std=c11 -Wall -O2 -g -DNO_CONSOLE_LOG \
               $(TARGET_ABI_FLAGS)
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

TARGET_LADDER := $(BUILD_DIR)/test_coldboot_ladder
TARGET_LOOP := $(BUILD_DIR)/test_restart_loop
.PHONY: clean run_all run_coldboot run_coldboot_hang run_restart_loop

$(BUILD_DIR):
	$(MKDIR_P) $(BUILD_DIR)

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

run_all: run_coldboot run_coldboot_hang run_restart_loop
	@echo "=== tum gsm host testleri bitti ==="

clean:
	$(RM_RF) $(BUILD_DIR)
