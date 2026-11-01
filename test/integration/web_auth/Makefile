# Former Python-generated C harnesses now run directly under Unity/Ceedling.
.PHONY: run clean
run:
	$(MAKE) -C ../.. cases "TEST_PATTERN=(http_auth_scenario|http_version_scenario|http_board_scenario|http_rf_save_scenario|http_transport_scenario|bsp_rng_transition_scenario|at_socket_log)"
clean:
	@echo "Ceedling artifacts are managed by test/Makefile clean"
