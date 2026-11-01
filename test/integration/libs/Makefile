# Native C behavior is owned by Ceedling; compiler/thread checks remain external.
PYTHON ?= python
CC := gcc
ARM_CC ?=
.PHONY: all run platform asan clean
all: run
run:
	$(MAKE) -C ../.. cases TEST_PATTERN=spi_flash_log
	$(MAKE) platform
asan:
	$(MAKE) -C ../.. cases_asan TEST_PATTERN=spi_flash_log
platform:
	$(PYTHON) ../../scripts/test_host_test_tools.py
	$(PYTHON) run_ring_buffer_checks.py --cc "$(CC)" $(if $(ARM_CC),--arm-cc "$(ARM_CC)")
clean:
	@echo "Ceedling artifacts are managed by test/Makefile clean"
