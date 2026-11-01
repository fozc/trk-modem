# frozen_string_literal: true
# Native Ceedling cases with ASan, isolated from the normal build cache.
require "yaml"
require "fileutils"
require "rbconfig"
require "rubygems"

root = File.expand_path("..", __dir__)
configure_only = ARGV.delete("--configure-only")
pattern = ARGV.first
pattern = "spi_flash_log" if pattern.nil? || pattern.empty?
config = YAML.load_file(File.join(root, "project.yml"), permitted_classes: [Symbol], aliases: true)
config[:project][:build_root] = "build/ceedling-asan"
config[:flags][:test][:compile]["*"] += %w[-fsanitize=address -fno-omit-frame-pointer]
(config[:flags][:test][:link]["*"] ||= []) << "-fsanitize=address"
path = File.join(root, "build", "asan-project.yml")
FileUtils.mkdir_p(File.dirname(path))
File.write(path, YAML.dump(config))
if configure_only
  puts path
  exit 0
end
Dir.chdir(root) do
  exec(RbConfig.ruby, Gem.bin_path("ceedling", "ceedling"), "--project=#{path}", "test:pattern[#{pattern}]")
end
