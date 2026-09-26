# frozen_string_literal: true

# Runs a process and succeeds only when the process remains alive until the
# requested deadline. This keeps hang/liveness tests portable across hosts.

if ARGV.length < 2
  warn "usage: ruby expect_timeout.rb SECONDS COMMAND [ARG ...]"
  exit 2
end

seconds = Float(ARGV.shift)
pid = Process.spawn(*ARGV)
deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + seconds

loop do
  completed = Process.waitpid(pid, Process::WNOHANG)
  unless completed.nil?
    warn "FAIL: process exited before the expected timeout"
    exit 1
  end

  if Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline
    Process.kill("KILL", pid)
    Process.wait(pid)
    puts "PASS: process remained alive until the expected timeout"
    exit 0
  end

  sleep 0.05
end
