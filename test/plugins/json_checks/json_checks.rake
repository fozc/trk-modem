# frozen_string_literal: true

# A Ceedling entry point keeps native Unity and independent reference checks
# together, without duplicating Python's JSON parser or the host CRT in C.
namespace :json do
  desc "Run the standalone JSON Unity tests"
  task :unit do
    pattern = 'test_json_(reader|writer|numbers)\.c$'
    abort "JSON Unity test inventory is incomplete" unless
      COLLECTION_ALL_TESTS.grep(Regexp.new(pattern)).length == 3
    Rake::Task["test:pattern"].invoke(pattern)
  end

  desc "Compare JSON/Unicode/binary32 with independent host implementations"
  task :reference do
    script = File.expand_path("../../integration/json/run_checks.py", __dir__)
    command = [ENV.fetch("PYTHON", "python"), script,
               "--cc", ENV.fetch("CC", "gcc"),
               "--count", ENV.fetch("JSON_TEST_COUNT", "100000")]
    abort "JSON reference checks failed" unless system(*command)
  end

  desc "Run JSON unit tests and all repeatable reference/property checks"
  task check: [:unit, :reference, :compare, :http]

  desc "Exercise production HTTP JSON handlers with storage/transport fixtures"
  task :http do
    script = File.expand_path("../../integration/json/run_http_checks.py", __dir__)
    abort "HTTP JSON checks failed" unless
      system(ENV.fetch("PYTHON", "python"), script,
             "--cc", ENV.fetch("CC", "gcc"))
  end

  desc "Benchmark current production JSON against json.c adapters"
  task :compare do
    script = File.expand_path("../../integration/json/run_comparison.py", __dir__)
    command = [ENV.fetch("PYTHON", "python"), script,
               "--cc", ENV.fetch("CC", "gcc")]
    command.concat(["--arm-cc", ENV["ARM_CC"]]) if ENV["ARM_CC"]
    if ENV["JSON_BASELINE_SOURCE"]
      command.concat(["--baseline-source", ENV["JSON_BASELINE_SOURCE"]])
    end
    abort "JSON comparison failed" unless system(*command)
  end

  desc "Collect native JSON Unity coverage with the existing gcov plugin"
  task :coverage do
    names = %w[reader writer numbers].map { |name| "test_json_#{name}" }
    # Start only these generated profiles fresh; other modules are untouched.
    # GCC otherwise reports stale timestamps after recompiling json.c.
    names.each do |name|
      Dir.glob(File.join(GCOV_BUILD_OUTPUT_PATH, name, "*.gcda")).each do |path|
        File.delete(path)
      end
    end
    Rake::Task["gcov:pattern"].invoke('test_json_(reader|writer|numbers)\.c$')
    abort "JSON coverage unit tests failed" if names.any? do |name|
      File.exist?(File.join(GCOV_RESULTS_PATH, "#{name}.fail"))
    end
    artifacts = File.join(GCOV_ARTIFACTS_PATH)
    command = [ENV.fetch("PYTHON", "python"), "-m", "gcovr", "--root", "..",
               "--filter", '.*libraries/json/json\.c$', "--print-summary",
               "--fail-under-line", "95", "--fail-under-branch", "90",
               "--fail-under-function", "100",
               "--json-summary", File.join(artifacts, "json-summary.json"),
               "--html-details", File.join(artifacts, "json.html")]
    command.concat(names.map { |name| File.join(GCOV_BUILD_OUTPUT_PATH, name) })
    abort "JSON coverage gates failed" unless system(*command)
  end

  desc "Measure standalone Cortex-M JSON size/stack (ARM_CC required)"
  task :arm do
    compiler = ENV.fetch("ARM_CC") do
      abort "Set ARM_CC to the project's arm-none-eabi-gcc executable"
    end
    script = File.expand_path("../../integration/json/run_target_checks.py", __dir__)
    abort "JSON target checks failed" unless
      system(ENV.fetch("PYTHON", "python"), script, "--cc", compiler)
  end
end
