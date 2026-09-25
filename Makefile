# Shortcuts around CMake for day-to-day work. Run `make help` for the list.

BUILD_DIR ?= build
BUILD_TYPE ?= Release
CMAKE_ARGS ?=
JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
PYTHON ?= python3
CLANG_FORMAT ?= $(shell command -v clang-format-18 2>/dev/null || command -v clang-format 2>/dev/null)
CLANG_FORMAT_MAJOR := 18
# Tracked and new C++ sources that exist on disk (deleted files are skipped).
CPP_SOURCES = git ls-files -z --cached --others --exclude-standard '*.cpp' '*.hpp' | \
	xargs -0 sh -c 'for f; do [ -e "$$f" ] && printf "%s\0" "$$f"; done' _

.PHONY: all configure build debug test sanitize tsan tidy bench consumers codegen lint lint-docs \
	format pre-commit install-hooks coverage clean help

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(CMAKE_ARGS)

build: configure
	cmake --build $(BUILD_DIR) --parallel $(JOBS)

debug:
	$(MAKE) build BUILD_DIR=build-debug BUILD_TYPE=Debug

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure --parallel $(JOBS)

sanitize:
	$(MAKE) test BUILD_DIR=build-asan BUILD_TYPE=Debug \
		CMAKE_ARGS="-DKALSHI_ENABLE_SANITIZERS=ON -DKALSHI_BUILD_EXAMPLES=OFF $(CMAKE_ARGS)"

tsan:
	$(MAKE) test BUILD_DIR=build-tsan BUILD_TYPE=Debug \
		CMAKE_ARGS="-DKALSHI_ENABLE_THREAD_SANITIZER=ON -DKALSHI_BUILD_EXAMPLES=OFF $(CMAKE_ARGS)"

tidy:
	$(MAKE) build BUILD_DIR=build-tidy BUILD_TYPE=Debug \
		CMAKE_ARGS="-DKALSHI_ENABLE_CLANG_TIDY=ON -DKALSHI_BUILD_TESTS=OFF -DKALSHI_BUILD_EXAMPLES=OFF $(CMAKE_ARGS)"

bench:
	$(MAKE) build BUILD_DIR=build-bench BUILD_TYPE=Release \
		CMAKE_ARGS="-DKALSHI_BUILD_BENCHMARKS=ON -DKALSHI_BUILD_TESTS=OFF -DKALSHI_BUILD_EXAMPLES=OFF $(CMAKE_ARGS)"
	./build-bench/benchmarks/kalshi_benchmarks $(BENCH_ARGS)

consumers:
	./tools/test_consumers.sh

# Needs PyYAML and clang-format 18.
codegen:
	$(PYTHON) tools/codegen/generate.py

lint:
	@test -n "$(CLANG_FORMAT)" || { echo "clang-format $(CLANG_FORMAT_MAJOR) is required"; exit 1; }
	@major=$$($(CLANG_FORMAT) --version | sed -E 's/.*version ([0-9]+).*/\1/'); \
		test "$$major" = "$(CLANG_FORMAT_MAJOR)" || \
		{ echo "clang-format $(CLANG_FORMAT_MAJOR) is required; found $$major"; exit 1; }
	$(CPP_SOURCES) | xargs -0 $(CLANG_FORMAT) --dry-run --Werror
	$(PYTHON) tools/cpp_auto_audit.py
	$(PYTHON) tools/codegen/generate.py --check

lint-docs:
	markdownlint-cli2

format:
	$(CPP_SOURCES) | xargs -0 $(CLANG_FORMAT) -i

pre-commit: format lint

# Installs a hook that runs `make pre-commit`. Works in worktrees too.
install-hooks:
	@hook="$$(git rev-parse --git-path hooks)/pre-commit"; \
		printf '#!/bin/sh\nexec make pre-commit\n' > "$$hook" && chmod +x "$$hook" && \
		echo "Installed $$hook"

# Needs lcov and genhtml.
coverage:
	$(MAKE) test BUILD_DIR=build-coverage BUILD_TYPE=Debug \
		CMAKE_ARGS="-DKALSHI_ENABLE_COVERAGE=ON -DKALSHI_BUILD_EXAMPLES=OFF $(CMAKE_ARGS)"
	lcov --capture --directory build-coverage --output-file build-coverage/coverage.info \
		--ignore-errors mismatch,inconsistent
	lcov --extract build-coverage/coverage.info "$(CURDIR)/src/*" "$(CURDIR)/include/*" \
		--output-file build-coverage/coverage.info
	genhtml build-coverage/coverage.info --output-directory build-coverage/html
	@echo "Report: build-coverage/html/index.html"

clean:
	rm -rf build build-*

# Runs an example with .env loaded and ARGS as its arguments:
#   make run-market_data ARGS=KXHIGHNY
run-%: build
	@set -a; if [ -f .env ]; then . ./.env; fi; set +a; ./$(BUILD_DIR)/examples/example_$* $(ARGS)

help:
	@echo "make build        Configure and build (BUILD_TYPE=$(BUILD_TYPE), BUILD_DIR=$(BUILD_DIR))"
	@echo "make test         Build and run the test suite"
	@echo "make debug        Debug build in build-debug/"
	@echo "make sanitize     ASan + UBSan tests in build-asan/"
	@echo "make tsan         ThreadSanitizer tests in build-tsan/"
	@echo "make tidy         clang-tidy build in build-tidy/"
	@echo "make bench        Google Benchmark suite in build-bench/ (BENCH_ARGS=...)"
	@echo "make consumers    Check install and FetchContent consumers"
	@echo "make codegen      Regenerate the REST client from spec/openapi.yaml"
	@echo "make lint         clang-format, explicit-type audit, generated-code check"
	@echo "make lint-docs    markdownlint"
	@echo "make format       Format C++ sources in place"
	@echo "make pre-commit   format + lint"
	@echo "make install-hooks  Run pre-commit on every git commit"
	@echo "make coverage     lcov report in build-coverage/html"
	@echo "make run-NAME     Run examples/NAME.cpp with .env loaded (ARGS=...)"
	@echo "make clean        Remove build directories"
