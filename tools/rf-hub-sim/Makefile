# Makefile for the RF HUB simulator (host, MinGW/Win32)
#
# Targets:
#   make        - build rf_hub_sim.exe (live UART mode)
#   make test   - build + run hub_selftest.exe (no serial needed)
#   make clean  - remove build artifacts

CC       := gcc
CFLAGS   := -std=c11 -Wall -Wextra -Wshadow -Wconversion -O2 -g
LIBSCP   := ../../Application/libscp
BUILD    := build

INCLUDES := -I. -I$(LIBSCP)

SCP_OBJ     := $(BUILD)/scp.o $(BUILD)/cobs.o
SIM_OBJ     := $(BUILD)/main.o $(BUILD)/hub.o $(BUILD)/serial_win.o
TEST_OBJ    := $(BUILD)/selftest.o $(BUILD)/hub.o

all: rf_hub_sim.exe

rf_hub_sim.exe: $(SIM_OBJ) $(SCP_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

hub_selftest.exe: $(TEST_OBJ) $(SCP_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.o: main.c hub.h serial_win.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c main.c -o $@

$(BUILD)/hub.o: hub.c hub.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c hub.c -o $@

$(BUILD)/serial_win.o: serial_win.c serial_win.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c serial_win.c -o $@

$(BUILD)/selftest.o: selftest.c hub.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c selftest.c -o $@

$(BUILD)/scp.o: $(LIBSCP)/scp.c $(LIBSCP)/scp.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(LIBSCP) -c $< -o $@

$(BUILD)/cobs.o: $(LIBSCP)/cobs.c $(LIBSCP)/cobs.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(LIBSCP) -c $< -o $@

test: hub_selftest.exe
	./hub_selftest.exe

clean:
	rm -rf $(BUILD) rf_hub_sim.exe hub_selftest.exe

.PHONY: all test clean
