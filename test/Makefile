.PHONY: all unit integration coverage clean cases cases_asan

all:
	ruby run_all.rb all

unit:
	ruby run_all.rb unit

integration:
	ruby run_all.rb integration

coverage:
	ruby run_all.rb coverage

clean:
	ruby run_all.rb clean

# Selected native C scenarios, also used by the former integration entry points.
cases:
	ruby -S ceedling "test:pattern[$(TEST_PATTERN)]"

cases_asan:
	ruby scripts/run_asan.rb "$(TEST_PATTERN)"
