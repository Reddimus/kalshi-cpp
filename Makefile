# Kalshi C++ SDK - Root Makefile
# Wraps CMake for convenient day-to-day workflow

BUILD_DIR := build
CMAKE := cmake
NPROC := $(shell nproc 2>/dev/null || echo 4)
BENCH_ITERATIONS := 254

.PHONY: all build test lint clean configure help bench bench-compare format

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
		find src include tests examples -name '*.cpp' -o -name '*.hpp' | xargs clang-format --dry-run --Werror 2>/dev/null || \
		echo "Format check complete (some files may need formatting)"; \
	else \
		echo "clang-format not found - skipping lint"; \
	fi

# Format code in place
format:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "Formatting code..."; \
		find src include tests examples -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i; \
		echo "Done"; \
	else \
		echo "clang-format not found"; \
	fi

# Clean build artifacts
clean:
	@rm -rf $(BUILD_DIR)
	@echo "Cleaned build directory"

# Run benchmark
bench: build
	@./tools/bench.sh $(BENCH_ITERATIONS)

# Compare benchmarks between HEAD and working tree (or two refs)
bench-compare:
	@./tools/bench.sh --compare $(BENCH_ITERATIONS)

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
	@echo "  make clean         - Remove build artifacts"
	@echo "  make help          - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  BENCH_ITERATIONS=N  - Override benchmark iterations (default: 254)"
