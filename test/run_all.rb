# frozen_string_literal: true

# Central entry point for Ceedling unit tests and host integration suites.

TEST_ROOT = File.expand_path(__dir__)
MAKE = ENV.fetch("MAKE", "make")

INTEGRATION_SUITES = [
  ["contiki_process", "contiki_process", [[MAKE, "run"]]],
  ["fault_log", "fault_log", [[MAKE, "run"]]],
  ["gsm", "gsm", [[MAKE, "run_all"]]],
  ["libiec104", "libiec104", [[MAKE, "run"]]],
  ["libs", "libs", [[MAKE, "run"]]],
  ["nvram", "nvram", [[MAKE, "run"]]],
  ["power_board", "power_board", [[MAKE, "run"]]],
  ["rf", "rf", [[MAKE, "run"], [MAKE, "-f", "Makefile.scp", "run"]]],
  ["rf_hub_sim", "rf_hub_sim", [[MAKE, "run"]]],
  ["web_navigation", "web_navigation", [[MAKE, "run"]]],
  ["web_server", "web_server", [[MAKE, "run"]]]
].freeze

def run_command(label, directory, command)
  puts "\n===== #{label}: #{command.join(' ')} ====="
  success = system(*command, chdir: directory)
  puts success ? "PASS: #{label}" : "FAIL: #{label}"
  success
end

def run_unit(task)
  run_command("ceedling", TEST_ROOT, ["ceedling", task])
end

def run_integration
  results = []

  INTEGRATION_SUITES.each do |name, relative_dir, commands|
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
    if commands.any? { |command| command.include?("Makefile.scp") }
      results << run_command("#{name} scp clean", directory,
                             [MAKE, "-f", "Makefile.scp", "clean"])
    end
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
when "coverage"
  results << run_unit("gcov:all")
when "clean"
  results << clean_all
else
  warn "usage: ruby test/run_all.rb [all|unit|integration|coverage|clean]"
  exit 2
end

exit(results.all? ? 0 : 1)
