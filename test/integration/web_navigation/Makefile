.PHONY: run clean

run:
	python test_modbus_map.py
	python test_minifier.py
	node test_i18n_pack.js
	node test_navigation.js
	node test_industrial_audit.js
	node test_config_profiles.js
	node test_loading.js

clean:
	@ruby -e "exit 0"
