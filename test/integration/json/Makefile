include ../../make/common.mk

PYTHON ?= python
CC := gcc

.PHONY: run clean
run:
	$(PYTHON) run_checks.py --cc "$(CC)"
	$(PYTHON) run_comparison.py --cc "$(CC)"
	$(PYTHON) run_http_checks.py --cc "$(CC)"

clean:
	$(RM_RF) build
