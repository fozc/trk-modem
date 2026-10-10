# frozen_string_literal: true

require "rbconfig"
require "rubygems"

$stdout.sync = true

# Central entry point for Ceedling unit tests and host integration suites.

TEST_ROOT = File.expand_path(__dir__)
MAKE = ENV.fetch("MAKE", "make")

INTEGRATION_SUITES = [
  ["contiki_process", "contiki_process", [[MAKE, "run"]]],
  ["fault_log", "fault_log", [[MAKE, "run"]]],
  ["gsm", "gsm", [[MAKE, "run_all"]]],
  ["libs", "libs", [[MAKE, "run"]]],
  ["nvram", "nvram", [[MAKE, "run"]]],
  ["rfwu_auth", "rfwu_auth", [[MAKE, "run"]]],
  ["rf_hub_sim", "rf_hub_sim", [[MAKE, "run"]]],
  ["rf_hil", "rf_hil", [[MAKE, "run"]]],
  ["rf_real_mh", "rf_real_mh", [[MAKE, "run"]]],
  ["iec104_master", "iec104_master", [[MAKE, "run"]]],
  ["web_auth", "web_auth", [[MAKE, "run"]]],
  ["web_navigation", "web_navigation", [[MAKE, "run"]]],
  ["web_device", "web_device", [[MAKE, "run"]]]
].freeze

def run_command(label, directory, command)
  puts "\n===== #{label}: #{command.join(' ')} ====="
  success = system(*command, chdir: directory)
  puts success ? "PASS: #{label}" : "FAIL: #{label}"
  success
end

def run_unit(task)
  command = [RbConfig.ruby, Gem.bin_path("ceedling", "ceedling"), task]
  run_command("ceedling", TEST_ROOT, command)
end

def run_integration(selected = nil)
  results = []

  INTEGRATION_SUITES.each do |name, relative_dir, commands|
    next if selected && !selected.include?(name)

    directory = File.join(TEST_ROOT, "integration", relative_dir)
    commands.each do |command|
      results << run_command(name, directory, command)
    end
  end

  results.all?
end

def clean_all
  results = [run_unit("clobber")]
  cleaned = {}

  INTEGRATION_SUITES.each do |name, relative_dir, commands|
    directory = File.join(TEST_ROOT, "integration", relative_dir)
    next if cleaned[directory]

    results << run_command("#{name} clean", directory, [MAKE, "clean"])
    cleaned[directory] = true
  end

  results.all?
end

mode = ARGV.fetch(0, "all")
results = []

case mode
when "all"
  results << run_unit("test:all")
  results << run_integration
when "unit"
  results << run_unit("test:all")
when "integration"
  results << run_integration
when "logs"
  pattern = "test_(fault_log|iec104_event_log|iec104_.*replay_scenario|" \
            "iec104_ack_delivery_scenario|rf_event_log|" \
            "spi_flash_log_sequence_wrap)"
  results << run_unit("test:pattern[#{pattern}]")
  results << run_integration(%w[fault_log libs])
when "critical"
  results << run_unit("test:all")
  results << run_integration(%w[fault_log libs gsm nvram rfwu_auth
                                iec104_master web_auth web_navigation])
when "coverage"
  results << run_unit("clobber")
  results << run_unit("gcov:all") if results.all?
  if results.all?
    results << run_command("coverage test inventory", TEST_ROOT,
                           ["python", "scripts/test_inventory.py", "--junit",
                            "build/ceedling/artifacts/gcov/junit_tests_report.xml"])
    results << run_command("coverage inventory checks", TEST_ROOT,
                           ["python", "scripts/test_logic_coverage.py"])
    results << run_command("logic coverage inventory", TEST_ROOT,
                           ["python", "scripts/logic_coverage.py"])
  end
when "clean"
  results << clean_all
else
  warn "usage: ruby test/run_all.rb " \
       "[all|unit|integration|logs|critical|coverage|clean]"
  exit 2
end

exit(results.all? ? 0 : 1)
