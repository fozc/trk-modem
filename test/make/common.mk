# Portable filesystem helpers for integration-test Makefiles.
MKDIR_P = ruby -rfileutils -e "ARGV.each { |path| FileUtils.mkdir_p(path) }"
RM_RF = ruby -rfileutils -e "ARGV.each { |path| FileUtils.rm_rf(path) }"
