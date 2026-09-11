# Host tests for the xprintf/xscanf and spi_flash_log contracts.
#
#   make          - build
#   make run      - build and run
#   make asan     - rebuild both suites with -fsanitize=address and run
#                   (toolchain'inde libasan olmayan MinGW'da calismaz)
#   make clean    - remove build artifacts

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Wshadow -O1 -g
ASAN_CFLAGS := $(CFLAGS) -fsanitize=address -fno-omit-frame-pointer
BUILD   := build
ASAN_BUILD := $(BUILD)/asan

LIB_DIR := ..
# spi_flash_log.c bsp.h (bsp_kick_wdt) ceker; o da cslog/xprintf basliklarini
# icerir - host testleri icin bu yollar da gerekli.
INCLUDES := -I$(LIB_DIR) -I$(LIB_DIR)/../bsp -I$(LIB_DIR)/../cslog

.PHONY: all run clean asan

all: $(BUILD)/test_xsnprintf $(BUILD)/test_xscanf $(BUILD)/test_spi_flash_log

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/test_xsnprintf: test_xsnprintf.c $(LIB_DIR)/xprintf.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

$(BUILD)/test_xscanf: test_xscanf.c $(LIB_DIR)/xscanf.c | $(BUILD)
	$(CC) $(CFLAGS) -DXSCANF_ENABLE_TESTS $(INCLUDES) $^ -o $@

$(BUILD)/test_spi_flash_log: test_spi_flash_log.c $(LIB_DIR)/spi_flash_log.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

run: all
	$(BUILD)/test_xsnprintf
	$(BUILD)/test_xscanf
	$(BUILD)/test_spi_flash_log

$(ASAN_BUILD):
	@mkdir -p $(ASAN_BUILD)

asan: | $(ASAN_BUILD)
	$(CC) $(ASAN_CFLAGS) $(INCLUDES) test_xsnprintf.c $(LIB_DIR)/xprintf.c -o $(ASAN_BUILD)/test_xsnprintf
	$(CC) $(ASAN_CFLAGS) -DXSCANF_ENABLE_TESTS $(INCLUDES) test_xscanf.c $(LIB_DIR)/xscanf.c -o $(ASAN_BUILD)/test_xscanf
	$(CC) $(ASAN_CFLAGS) $(INCLUDES) test_spi_flash_log.c $(LIB_DIR)/spi_flash_log.c -o $(ASAN_BUILD)/test_spi_flash_log
	$(ASAN_BUILD)/test_xsnprintf
	$(ASAN_BUILD)/test_xscanf
	$(ASAN_BUILD)/test_spi_flash_log

clean:
	rm -rf $(BUILD)
