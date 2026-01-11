# Examples directory Makefile
# Delegates to root Makefile for consistent builds

ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))/../

.PHONY: build test lint clean

build:
	@$(MAKE) -C $(ROOT_DIR) build

test:
	@$(MAKE) -C $(ROOT_DIR) test

lint:
	@$(MAKE) -C $(ROOT_DIR) lint

clean:
	@$(MAKE) -C $(ROOT_DIR) clean
