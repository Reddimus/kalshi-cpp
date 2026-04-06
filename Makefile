# Kalshi C++ SDK - Root Makefile
# Wraps CMake for convenient day-to-day workflow

BUILD_DIR := build
CMAKE := cmake
NPROC := $(shell nproc 2>/dev/null || echo 4)
BENCH_ITERATIONS := 254

.PHONY: all build test lint clean configure help bench bench-compare format coverage run-get_markets run-basic_usage run-get_daily_temp

# Default target
all: build

# Configure CMake (if needed)
configure:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build all targets
build: configure
	@$(CMAKE) --build $(BUILD_DIR) -j$(NPROC)

# Run tests
test: build
	@cd $(BUILD_DIR) && ctest --output-on-failure

# Run linting (clang-format check)
lint:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "Checking code formatting..."; \
		find src include tests examples \( -name '*.cpp' -o -name '*.hpp' \) -print0 | \
			xargs -0 clang-format --dry-run --Werror && \
		echo "Format check passed."; \
	else \
		echo "clang-format not found. Install clang-format to run lint."; \
		exit 1; \
	fi

# Format code in place
format:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "Formatting code..."; \
		find src include tests examples \( -name '*.cpp' -o -name '*.hpp' \) -print0 | xargs -0 clang-format -i; \
		echo "Done"; \
	else \
		echo "clang-format not found. Install clang-format to format code."; \
		exit 1; \
	fi

# Code coverage (requires lcov)
coverage:
	@mkdir -p build-coverage
	@cd build-coverage && $(CMAKE) .. -DCMAKE_BUILD_TYPE=Debug -DKALSHI_ENABLE_COVERAGE=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@$(CMAKE) --build build-coverage -j$(NPROC)
	@cd build-coverage && ctest --output-on-failure
	@lcov --capture --directory build-coverage --output-file build-coverage/coverage.info --ignore-errors mismatch
	@lcov --remove build-coverage/coverage.info '/usr/*' '*/build-coverage/_deps/*' --output-file build-coverage/coverage_filtered.info --ignore-errors unused
	@genhtml build-coverage/coverage_filtered.info --output-directory build-coverage/coverage-report
	@echo "Coverage report: build-coverage/coverage-report/index.html"

# Clean build artifacts
clean:
	@rm -rf $(BUILD_DIR) build-coverage
	@echo "Cleaned build directory"

# Run benchmark
bench: build
	@./tools/bench.sh $(BENCH_ITERATIONS)

# Compare benchmarks between HEAD and working tree (or two refs)
bench-compare:
	@./tools/bench.sh --compare $(BENCH_ITERATIONS)

# Load .env and run example (creates temp PEM if KALSHI_API_PRIVATE_KEY is set)
define run_example
	@if [ -f .env ]; then \
		set -a && . ./.env && set +a && \
		if [ -n "$$KALSHI_API_PRIVATE_KEY" ] && [ -z "$$KALSHI_API_KEY_FILE" ]; then \
			TMPFILE=$$(mktemp) && \
			echo "$$KALSHI_API_PRIVATE_KEY" | base64 -d | openssl rsa -inform DER -outform PEM -out $$TMPFILE 2>/dev/null && \
			KALSHI_API_KEY_FILE=$$TMPFILE $(1) ; \
			rm -f $$TMPFILE; \
		else \
			$(1); \
		fi; \
	else \
		$(1); \
	fi
endef

# Run examples
run-get_markets: build
	$(call run_example,./$(BUILD_DIR)/examples/example_markets)

run-basic_usage: build
	$(call run_example,./$(BUILD_DIR)/examples/example_basic)

run-get_daily_temp: build
	$(call run_example,./$(BUILD_DIR)/examples/example_daily_temp)

# Help
help:
	@echo "Kalshi C++ SDK Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make build         - Configure and build the SDK"
	@echo "  make test          - Run tests"
	@echo "  make bench         - Run benchmark ($(BENCH_ITERATIONS) iterations)"
	@echo "  make bench-compare - Compare HEAD vs working tree"
	@echo "  make lint          - Check code formatting"
	@echo "  make format        - Format code in place"
	@echo "  make coverage      - Generate code coverage report (requires lcov)"
	@echo "  make clean         - Remove build artifacts"
	@echo "  make help          - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  BENCH_ITERATIONS=N  - Override benchmark iterations (default: 254)"
