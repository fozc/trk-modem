# Host integration test for the spi_flash_log contract.
#
#   make          - build
#   make run      - build and run
#   make asan     - rebuild with -fsanitize=address and run
#                   (toolchain'inde libasan olmayan MinGW'da calismaz)
#   make clean    - remove build artifacts

include ../../make/common.mk

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Wshadow -O1 -g
ASAN_CFLAGS := $(CFLAGS) -fsanitize=address -fno-omit-frame-pointer
BUILD   := build
ASAN_BUILD := $(BUILD)/asan

LIB_DIR := ../../../Application/libs
# spi_flash_log.c bsp.h (bsp_kick_wdt) ceker; o da cslog/xprintf basliklarini
# icerir - host testleri icin bu yollar da gerekli.
INCLUDES := -I$(LIB_DIR) -I$(LIB_DIR)/../bsp -I$(LIB_DIR)/../cslog

.PHONY: all run clean asan

all: $(BUILD)/test_spi_flash_log

$(BUILD):
	@$(MKDIR_P) $(BUILD)

$(BUILD)/test_spi_flash_log: test_spi_flash_log.c $(LIB_DIR)/spi_flash_log.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

run: all
	$(BUILD)/test_spi_flash_log

$(ASAN_BUILD):
	@$(MKDIR_P) $(ASAN_BUILD)

asan: | $(ASAN_BUILD)
	$(CC) $(ASAN_CFLAGS) $(INCLUDES) test_spi_flash_log.c $(LIB_DIR)/spi_flash_log.c -o $(ASAN_BUILD)/test_spi_flash_log
	$(ASAN_BUILD)/test_spi_flash_log

clean:
	$(RM_RF) $(BUILD)
