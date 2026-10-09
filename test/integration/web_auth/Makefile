CC = gcc
PYTHON ?= python

.PHONY: run clean

run:
	$(PYTHON) run_tests.py --cc "$(CC)"
	$(PYTHON) run_transport_tests.py --cc "$(CC)"

clean:
	$(PYTHON) run_tests.py --clean
