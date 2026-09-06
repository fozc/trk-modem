# Makefile for gsm module host-based testing
#
# Usage:
#   make                - build ring_buff test binary
#   make run            - build and run ring_buff tests
#   make run_all        - ring_buff + cold boot ladder + restart loop + hang
#   make clean          - remove build artifacts
#
# Pattern follows Application/rf/test (host gcc).

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g
# Uretim .c dosyalarini include eden testler: legacy kod -Wconversion
# altinda derlenmediginden bu hedeflerde sade flag seti kullanilir.
CFLAGS_PROD := -std=c11 -Wall -O2 -g -DNO_CONSOLE_LOG
BUILD_DIR := build

# Kernel + uygulama include yollari (uretim dosyalarini include edenler)
INC := -I.. -I../.. -I../../bsp -I../../cslog -I../../libs \
       -I../../libiec104 -I../../rf -I../../../contiki-kernel
KERNEL_SRC := ../../../contiki-kernel/sys/process.c \
              ../../../contiki-kernel/sys/etimer.c \
              ../../../contiki-kernel/sys/timer.c \
              ../../../contiki-kernel/sys/clock.c

SRC := ../ring_buff.c
TEST_SRC := test_ring_buff.c

SRC_OBJ := $(BUILD_DIR)/ring_buff.o
TEST_OBJ := $(BUILD_DIR)/test_ring_buff.o

TARGET := $(BUILD_DIR)/test_ring_buff
TARGET_LADDER := $(BUILD_DIR)/test_coldboot_ladder
TARGET_LOOP := $(BUILD_DIR)/test_restart_loop

.PHONY: all run clean run_all run_coldboot run_coldboot_hang run_restart_loop

all: $(TARGET)

$(TARGET): $(SRC_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SRC_OBJ): $(SRC) ../ring_buff.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_OBJ): $(TEST_SRC) ../ring_buff.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I.. -c $< -o $@

run: $(TARGET)
	./$(TARGET)

# --- Kurtarma merdiveni karar testleri (gsm_init.c include edilir) ---

$(TARGET_LADDER): test_coldboot_ladder.c ../gsm_init.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_PROD) $(INC) -o $@ $<

run_coldboot: $(TARGET_LADDER)
	./$(TARGET_LADDER)

# Haklar bitince adim while(1)'e dusmeli: timeout ile oldurulmesi
# (cikis 124) BASARI sayilir.
run_coldboot_hang: $(TARGET_LADDER)
	@timeout 3 ./$(TARGET_LADDER) hang; rc=$$?; \
	if [ $$rc -eq 124 ]; then \
		echo "PASS: exhaustion enters while(1) (killed by timeout)"; \
	else \
		echo "FAIL: exhaustion returned rc=$$rc"; exit 1; \
	fi

# --- Surec yeniden baslatma dongusu (gercek Contiki cekirdegi) -------

$(TARGET_LOOP): test_restart_loop.c ../../gsm_process_contiki.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_PROD) $(INC) -o $@ $< $(KERNEL_SRC)

run_restart_loop: $(TARGET_LOOP)
	./$(TARGET_LOOP)

run_all: run run_coldboot run_coldboot_hang run_restart_loop
	@echo "=== tum gsm host testleri bitti ==="

clean:
	rm -rf $(BUILD_DIR)
