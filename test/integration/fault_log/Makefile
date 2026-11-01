# Native persistence cases plus Python checks of real-device evidence helpers.
.PHONY: run all evidence clean
all: run
run:
	$(MAKE) -C ../.. cases TEST_PATTERN=fault_log
	$(MAKE) evidence
evidence:
	python ../../system/device_fault/test_fault_evidence.py
	python ../../system/test_gsm_firmware_evidence.py
clean:
	@echo "Ceedling artifacts are managed by test/Makefile clean"
