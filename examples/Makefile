# Examples directory Makefile
# Delegates to root Makefile for consistent builds

ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))/../

.PHONY: build test lint clean run-get_markets run-basic_usage

build:
	@$(MAKE) -C $(ROOT_DIR) build

test:
	@$(MAKE) -C $(ROOT_DIR) test

lint:
	@$(MAKE) -C $(ROOT_DIR) lint

clean:
	@$(MAKE) -C $(ROOT_DIR) clean

# Load .env and run example (creates temp PEM if KALSHI_API_PRIVATE_KEY is set)
define run_example
	@if [ -f $(ROOT_DIR)/.env ]; then \
		set -a && . $(ROOT_DIR)/.env && set +a && \
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

run-get_markets: build
	$(call run_example,$(ROOT_DIR)/build/examples/example_markets)

run-basic_usage: build
	$(call run_example,$(ROOT_DIR)/build/examples/example_basic)
