# Compatibility entry point; native C cases are owned by Ceedling.
.PHONY: run all clean
all: run
run:
	$(MAKE) -C ../.. cases TEST_PATTERN=nvram_sync
clean:
	@echo "Ceedling artifacts are managed by test/Makefile clean"
