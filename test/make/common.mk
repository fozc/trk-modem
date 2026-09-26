# Portable filesystem helpers for integration-test Makefiles.
MKDIR_P = ruby -rfileutils -e "ARGV.each { |path| FileUtils.mkdir_p(path) }"
RM_RF = ruby -rfileutils -e "ARGV.each { |path| FileUtils.rm_rf(path) }"

ifneq ($(ComSpec),)
HOST_WINDOWS := 1
else ifeq ($(OS),Windows_NT)
HOST_WINDOWS := 1
endif

ifeq ($(HOST_WINDOWS),1)
TARGET_ABI_FLAGS :=
else
TARGET_ABI_FLAGS := -m32
endif
