SHELL := /bin/bash

PRESET ?= default
BUILD_DIR ?= build
OBS_INCLUDE_DIR ?=
OBS_LIBRARY ?=
OBS_SOURCE_DIR ?=
SIMDE_INCLUDE_DIR ?=

.PHONY: help setup find-obs-dev-paths configure build test coverage install run check-plugin-log dev clean reconfigure release test-pkg

help:
	@echo "streamn-obs-scoreboard development targets"
	@echo ""
	@echo "Usage:"
	@echo "  make <target> [PRESET=default|debug|ninja|coverage] [OBS_INCLUDE_DIR=...] [OBS_LIBRARY=...] [OBS_SOURCE_DIR=...] [SIMDE_INCLUDE_DIR=...]"
	@echo ""
	@echo "Targets:"
	@echo "  setup                Install local prerequisites"
	@echo "  find-obs-dev-paths   Search for OBS headers/libs and print configure guidance"
	@echo "  configure            Configure CMake (passes OBS_INCLUDE_DIR/OBS_LIBRARY if set)"
	@echo "  build                Build plugin"
	@echo "  test                 Run tests"
	@echo "  coverage             Run coverage preset and enforce 100% line coverage for scoreboard core"
	@echo "  install              Install plugin artifact to OBS user plugin folder"
	@echo "  run                  Launch OBS with verbose logging"
	@echo "  check-plugin-log     Check latest OBS log for scoreboard plugin load lines"
	@echo "  dev                  configure + build + test"
	@echo "  release              Build macOS .pkg installer"
	@echo "  test-pkg             Build .pkg, install it, and verify plugin loads"
	@echo "  clean                Remove build directory"
	@echo "  reconfigure          clean + configure"

setup:
	./scripts/dev-setup-macos.sh

find-obs-dev-paths:
	./scripts/find-obs-dev-paths.sh

configure:
	@if [[ -n "$(OBS_INCLUDE_DIR)" && -n "$(OBS_LIBRARY)" ]]; then \
		if [[ -n "$(SIMDE_INCLUDE_DIR)" && -n "$(OBS_SOURCE_DIR)" ]]; then \
			./scripts/configure.sh "$(PRESET)" -DOBS_INCLUDE_DIR="$(OBS_INCLUDE_DIR)" -DOBS_LIBRARY="$(OBS_LIBRARY)" -DOBS_SOURCE_DIR="$(OBS_SOURCE_DIR)" -DSIMDE_INCLUDE_DIR="$(SIMDE_INCLUDE_DIR)"; \
		elif [[ -n "$(SIMDE_INCLUDE_DIR)" ]]; then \
			./scripts/configure.sh "$(PRESET)" -DOBS_INCLUDE_DIR="$(OBS_INCLUDE_DIR)" -DOBS_LIBRARY="$(OBS_LIBRARY)" -DSIMDE_INCLUDE_DIR="$(SIMDE_INCLUDE_DIR)"; \
		elif [[ -n "$(OBS_SOURCE_DIR)" ]]; then \
			./scripts/configure.sh "$(PRESET)" -DOBS_INCLUDE_DIR="$(OBS_INCLUDE_DIR)" -DOBS_LIBRARY="$(OBS_LIBRARY)" -DOBS_SOURCE_DIR="$(OBS_SOURCE_DIR)"; \
		else \
			./scripts/configure.sh "$(PRESET)" -DOBS_INCLUDE_DIR="$(OBS_INCLUDE_DIR)" -DOBS_LIBRARY="$(OBS_LIBRARY)"; \
		fi; \
	else \
		./scripts/configure.sh "$(PRESET)"; \
	fi

build:
	./scripts/build.sh "$(PRESET)"

test:
	./scripts/test.sh "$(PRESET)"

coverage:
	@if [[ -n "$(OBS_INCLUDE_DIR)" && -n "$(OBS_LIBRARY)" ]]; then \
		if [[ -n "$(SIMDE_INCLUDE_DIR)" && -n "$(OBS_SOURCE_DIR)" ]]; then \
			./scripts/configure.sh coverage -DOBS_INCLUDE_DIR="$(OBS_INCLUDE_DIR)" -DOBS_LIBRARY="$(OBS_LIBRARY)" -DOBS_SOURCE_DIR="$(OBS_SOURCE_DIR)" -DSIMDE_INCLUDE_DIR="$(SIMDE_INCLUDE_DIR)"; \
		elif [[ -n "$(SIMDE_INCLUDE_DIR)" ]]; then \
			./scripts/configure.sh coverage -DOBS_INCLUDE_DIR="$(OBS_INCLUDE_DIR)" -DOBS_LIBRARY="$(OBS_LIBRARY)" -DSIMDE_INCLUDE_DIR="$(SIMDE_INCLUDE_DIR)"; \
		elif [[ -n "$(OBS_SOURCE_DIR)" ]]; then \
			./scripts/configure.sh coverage -DOBS_INCLUDE_DIR="$(OBS_INCLUDE_DIR)" -DOBS_LIBRARY="$(OBS_LIBRARY)" -DOBS_SOURCE_DIR="$(OBS_SOURCE_DIR)"; \
		else \
			./scripts/configure.sh coverage -DOBS_INCLUDE_DIR="$(OBS_INCLUDE_DIR)" -DOBS_LIBRARY="$(OBS_LIBRARY)"; \
		fi; \
	else \
		./scripts/configure.sh coverage; \
	fi
	./scripts/build.sh coverage
	./scripts/test.sh coverage
	./scripts/coverage.sh build-coverage

install:
	./scripts/install-dev-plugin.sh "$(BUILD_DIR)"

run:
	./scripts/run-obs.sh

check-plugin-log:
	./scripts/check-plugin-log.sh

dev: configure build test

release:
	./scripts/package-macos.sh

test-pkg: release
	@echo "Installing .pkg (requires admin password)..."
	@PKG=$$(ls -t streamn-obs-scoreboard-*-macos.pkg 2>/dev/null | head -1); \
	if [ -z "$$PKG" ]; then echo "Error: no .pkg found"; exit 1; fi; \
	sudo installer -pkg "$$PKG" -target / && \
	echo "" && \
	echo "Installed. Verifying..." && \
	ls -la ~/Library/Application\ Support/obs-studio/plugins/streamn-obs-scoreboard.plugin/Contents/MacOS/ && \
	echo "" && \
	echo "Success — restart OBS to test."

clean:
	rm -rf "$(BUILD_DIR)"

reconfigure: clean configure
