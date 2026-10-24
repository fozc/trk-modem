# frozen_string_literal: true

require "fileutils"
require "rbconfig"
require "rubygems"

# Keep the production PIN default disabled; exercise the enabled build separately.
test_root = File.expand_path("..", __dir__)
project = File.read(File.join(test_root, "project.yml"))
project = project.sub(":build_root: build/ceedling",
                      ":build_root: build/ceedling-pin-enabled")
project = project.sub("      test_gsm_init_scenario:\n",
                      "      test_gsm_init_scenario:\n" \
                      "        - -DGSM_AUTO_PIN_ENABLED=1\n")
FileUtils.mkdir_p(File.join(test_root, "build"))
config = "build/gsm-pin-enabled.yml"
File.write(File.join(test_root, config), project)
success = system(RbConfig.ruby, Gem.bin_path("ceedling", "ceedling"),
                 "-p", config, "test:test_gsm_init_scenario", chdir: test_root)
exit(success ? 0 : 1)
