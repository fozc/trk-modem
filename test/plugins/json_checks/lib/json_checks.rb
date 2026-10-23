# frozen_string_literal: true

require "ceedling/plugin"

# Ceedling 1.0.1 validates task plugins through its programmatic inventory.
# Tasks live in json_checks.rake; this plugin needs no build hooks.
class JsonChecks < Plugin
end
