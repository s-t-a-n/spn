# SPN Zephyr Module Makefile
# Provides build automation for samples, tests, and CI pipeline
# Requires: Zephyr SDK, west, clang-format-21, clang-tidy-21

################################################################################
# Make Configuration
################################################################################

.DELETE_ON_ERROR:
.SUFFIXES:
.DEFAULT_GOAL := help

################################################################################
# User Configuration Variables
################################################################################

# Build and test configuration
BOARD ?= native_sim
PRISTINE ?= never
PROJECT_VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo "unknown")

# Parallelism and optimization
TWISTER_JOBS ?= $(shell nproc)
TWISTER_CACHE ?= 3
TWISTER_VERBOSE ?= 0
TWISTER_OUTDIR ?= twister-out
FAIL_FAST ?= 1

# Execution timeouts (seconds)
TEST_TIMEOUT ?= 60
SAMPLE_TIMEOUT ?= 60

# Static analysis configuration
TIDY_STRICT ?= 0
TIDY_JOBS ?= $(shell nproc)
TIDY_HEADER_FILTER ?= ^$(abspath src)/
TIDY_SOURCE_FILTER ?= ^$(abspath src)/

# cppcheck static analysis (includes MISRA C subset, threadsafety, y2038 checks)
CPPCHECK_CONFIG ?= scripts/misra.json
CPPCHECK_VERBOSE ?= 0
CPPCHECK_DETAILED ?= 0
CPPCHECK_FAIL_ON_VIOLATIONS ?= 0

# Coverage settings
COVERAGE ?= 0
COVERAGE_DIR ?= build_tests/coverage_report
CI_COVERAGE_CHECK ?= 1
CI_LINE_THRESHOLD ?= 90
CI_BRANCH_THRESHOLD ?= 50
CI_FUNCTION_THRESHOLD ?= 90

# Tool versions
CLANG_FORMAT ?= clang-format-21
CLANG_TIDY ?= clang-tidy-21

# Docker/CI configuration
ZEPHYR_VERSION ?= v3.7.1
ZEPHYR_SDK_VERSION ?= 0.16.8
WORKSPACE_DIR ?= /tmp/zephyr-workspace
MANIFEST_REPO ?= s-t-a-n/spn-workspace
MANIFEST_DIR ?= $(WORKSPACE_DIR)/spn-workspace
MODULE_DIR ?= $(WORKSPACE_DIR)/spn
DOCKER_IMAGE ?= spn-ci:local
DOCKER_CACHE_DIR ?= $(HOME)/.cache/spn-ci

################################################################################
# Internal Variables
################################################################################

# Colors for output
GREEN := \033[0;32m
RED := \033[0;31m
YELLOW := \033[1;33m
BLUE := \033[0;34m
NC := \033[0m

# Computed Twister options
TWISTER_OPTS := -j $(TWISTER_JOBS)
ifeq ($(TWISTER_CACHE),1)
TWISTER_OPTS += -n
endif
ifeq ($(TWISTER_CACHE),2)
TWISTER_OPTS += --aggressive-no-clean
endif
ifeq ($(TWISTER_VERBOSE),1)
TWISTER_OPTS += -v
endif

# Directory paths
BUILD_SAMPLES_DIR := build_samples
BUILD_TESTS_DIR := build_tests
BUILD_TIDY_DIR := build_tidy

################################################################################
# Special Targets
################################################################################

.PHONY: help version clean
.PHONY: build run menuconfig
.PHONY: test test_twister
.PHONY: build_all test_all
.PHONY: format_check format_all
.PHONY: tidy _tidy_prepare
.PHONY: cppcheck
.PHONY: ci _ci_sequential
.PHONY: coverage coverage_view _coverage_generate
.PHONY: list_samples list_tests
.PHONY: help_config help_targets help_examples
.PHONY: shell docker_build

# Force sequential execution for CI
.NOTPARALLEL: ci _ci_sequential

################################################################################
# Primary Targets
################################################################################

# Default help target
help:
	@echo "$(BLUE)SPN Zephyr Module$(NC) v$(PROJECT_VERSION)"
	@echo ""
	@echo "$(YELLOW)Quick Start:$(NC)"
	@echo "  $(GREEN)make build src/example_lib/samples/basic$(NC)  # Build sample"
	@echo "  $(GREEN)make test src/example_lib/tests$(NC)           # Run tests"
	@echo "  $(GREEN)make ci$(NC)                                   # Full CI pipeline"
	@echo ""
	@echo "$(YELLOW)Main Commands:$(NC)"
	@echo "  $(GREEN)build <path>$(NC)     Build specific sample"
	@echo "  $(GREEN)run <path>$(NC)       Build and run sample"
	@echo "  $(GREEN)test <path>$(NC)      Run specific test"
	@echo "  $(GREEN)build_all$(NC)        Build all samples"
	@echo "  $(GREEN)test_all$(NC)         Run all tests"
	@echo "  $(GREEN)format_check$(NC)     Verify code formatting"
	@echo "  $(GREEN)tidy$(NC)             Run static analysis (clang-tidy)"
	@echo "  $(GREEN)cppcheck$(NC)         Run static analysis (cppcheck)"
	@echo "  $(GREEN)coverage$(NC)         Generate coverage report"
	@echo "  $(GREEN)ci$(NC)               Run full CI pipeline"
	@echo "  $(GREEN)shell$(NC)            Enter CI Docker environment"
	@echo "  $(GREEN)clean$(NC)            Remove build artifacts"
	@echo ""
	@echo "$(YELLOW)More Help:$(NC)"
	@echo "  $(GREEN)make help_config$(NC)      # Configuration options"
	@echo "  $(GREEN)make help_targets$(NC)     # All available targets"
	@echo "  $(GREEN)make help_examples$(NC)    # Usage examples"

version:
	@echo "SPN Zephyr Module v$(PROJECT_VERSION)"
	@echo "Board: $(BOARD)"
	@echo "Twister jobs: $(TWISTER_JOBS), cache: $(TWISTER_CACHE)"

clean:
	@echo "$(YELLOW)Cleaning build artifacts...$(NC)"
	@if [ ! -f "zephyr/module.yml" ]; then \
		echo "$(RED)✗ Not in Zephyr module root directory$(NC)"; \
		exit 1; \
	fi
	@rm -rf $(BUILD_SAMPLES_DIR) $(BUILD_TESTS_DIR) $(BUILD_TIDY_DIR)
	@find . -maxdepth 3 -path "*/twister-out*" -type d -exec rm -rf {} + 2>/dev/null || true
	@find . -maxdepth 3 -name "*.dump" -type f -delete 2>/dev/null || true
	@echo "$(GREEN)✓ Clean completed$(NC)"

################################################################################
# Docker Shell
################################################################################

docker_build:
	$(call check_tool,docker)
	@docker buildx build -t $(DOCKER_IMAGE) \
		--build-arg ZEPHYR_VERSION=$(ZEPHYR_VERSION) \
		--build-arg ZEPHYR_SDK_VERSION=$(ZEPHYR_SDK_VERSION) \
		--build-arg USER_UID=$(shell id -u) \
		--build-arg USER_GID=$(shell id -g) \
		--load \
		-f .github/ci/Dockerfile .github/ci

shell: docker_build
	@echo "$(YELLOW)Preparing cache directories...$(NC)"
	@mkdir -p $(DOCKER_CACHE_DIR)/{workspace,zephyr,ccache}
	@echo "$(YELLOW)Entering CI Docker environment...$(NC)"
	@echo "Source mounted read-only from: $(PWD)"
	@docker run --rm -it \
		-v "$(PWD)":/work:ro -w /work \
		-v "$(DOCKER_CACHE_DIR)/workspace":$(WORKSPACE_DIR) \
		-v "$(DOCKER_CACHE_DIR)/zephyr":/home/runner/.cache/zephyr \
		-v "$(DOCKER_CACHE_DIR)/ccache":/home/runner/.cache/ccache \
		-e ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-$(ZEPHYR_SDK_VERSION) \
		-e CCACHE_DIR=/home/runner/.cache/ccache \
		-e ZEPHYR_VERSION=$(ZEPHYR_VERSION) \
		-e WORKSPACE_DIR=$(WORKSPACE_DIR) \
		-e MANIFEST_REPO=$(MANIFEST_REPO) \
		-e MANIFEST_DIR=$(MANIFEST_DIR) \
		-e MODULE_DIR=$(MODULE_DIR) \
		$(DOCKER_IMAGE) bash -lc '\
			set -e; \
			git config --global --add safe.directory /work 2>/dev/null || true; \
			git config --global --add safe.directory $(MODULE_DIR) 2>/dev/null || true; \
			echo "Setting up workspace..."; \
			mkdir -p $(WORKSPACE_DIR); \
			if [ ! -d $(MANIFEST_DIR) ]; then \
				echo "Cloning manifest repository..."; \
				git clone --depth=1 https://github.com/$(MANIFEST_REPO).git $(MANIFEST_DIR) || { echo "Failed to clone manifest"; exit 1; }; \
			fi; \
			cd $(WORKSPACE_DIR); \
			if [ ! -f .west/config ]; then \
				echo "Initializing west workspace..."; \
				west init -l $(MANIFEST_DIR) || { echo "Failed to initialize west"; exit 1; }; \
			fi; \
			echo "Updating west dependencies..."; \
			west update --narrow --fetch-opt=--depth=1 || { echo "Failed to update west"; exit 1; }; \
			echo "Exporting Zephyr environment..."; \
			west zephyr-export || { echo "Failed to export Zephyr"; exit 1; }; \
			export ZEPHYR_BASE=$(WORKSPACE_DIR)/zephyr ZEPHYR_EXTRA_MODULES=$(MODULE_DIR); \
			export CMAKE_C_COMPILER_LAUNCHER=ccache CMAKE_CXX_COMPILER_LAUNCHER=ccache; \
			echo "Syncing module source..."; \
			mkdir -p $(MODULE_DIR); \
			rsync -a --delete --exclude=build_* --exclude=twister-out --exclude=.cache /work/ $(MODULE_DIR)/ || { echo "Failed to sync module"; exit 1; }; \
			cd $(MODULE_DIR); \
			echo "Environment ready. Starting shell..."; \
			exec bash'

################################################################################
# Build and Run Targets
################################################################################

build:
	$(call check_tool,west)
	@$(call validate_target_path,$@)
	@$(call build_sample,$(TARGET_PATH))

run:
	@$(call validate_target_path,$@)
	@$(MAKE) --no-print-directory build $(TARGET_PATH)
	@$(call run_sample,$(TARGET_PATH))

menuconfig:
	$(call check_tool,west)
	@$(call validate_target_path,$@)
	@$(call run_menuconfig,$(TARGET_PATH))

################################################################################
# Test Targets
################################################################################

test:
	$(call check_tool,west)
	@$(call validate_target_path,$@)
	@$(call run_test,$(TARGET_PATH))

test_twister:
	$(call check_tool,west)
	@echo "$(YELLOW)Running all tests via Twister...$(NC)"
	@west twister -T src/ -p $(BOARD) --inline-logs -v $(TWISTER_OPTS) -O $(TWISTER_OUTDIR) && \
	echo "$(GREEN)✓ All tests passed$(NC)" || \
	(echo "$(RED)✗ Some tests failed$(NC)" && exit 1)
	@pkill -9 zephyr.exe 2>/dev/null || true

################################################################################
# Batch Operations
################################################################################

build_all:
	$(call check_tool,west)
	@echo "$(YELLOW)Building all samples...$(NC)"
	@mkdir -p $(BUILD_SAMPLES_DIR)
	@$(call batch_operation,samples,build_sample_batch)

test_each:
	$(call check_tool,west)
	@echo "$(YELLOW)Building and running each test app with west...$(NC)"
	@if [ "$(COVERAGE)" = "1" ]; then echo "$(YELLOW)Coverage enabled$(NC)"; fi
	@mkdir -p $(BUILD_TESTS_DIR)
	@FAILED=0; PASSED=0; TOTAL=0; \
	COVERAGE_FLAGS=""; \
	if [ "$(COVERAGE)" = "1" ]; then COVERAGE_FLAGS="-- -DCONFIG_COVERAGE=y"; fi; \
	COVERAGE_SUFFIX=""; if [ "$(COVERAGE)" = "1" ]; then COVERAGE_SUFFIX="-coverage"; fi; \
	for app in $$(find src -path "*/tests/*" -type d -exec test -f {}/CMakeLists.txt \; -print | sort); do \
	  TOTAL=$$((TOTAL + 1)); \
	  BUILD_NAME=$$(echo "$$app" | sed 's|/|_|g'); \
	  BUILD_DIR="$(BUILD_TESTS_DIR)/$$BUILD_NAME-$(BOARD)$$COVERAGE_SUFFIX"; \
	  echo "$(YELLOW)Building: $$app -> $$BUILD_DIR$(NC)"; \
	  if west build -b $(BOARD) "$$app" -d "$$BUILD_DIR" -p $(PRISTINE) $$COVERAGE_FLAGS; then \
		echo "$(YELLOW)Running: $$BUILD_DIR/zephyr/zephyr.exe$(NC)"; \
		if timeout -s INT -k 2 $(TEST_TIMEOUT) "$$BUILD_DIR/zephyr/zephyr.exe"; then \
		  echo "$(GREEN)✓ $$app$(NC)"; \
		  PASSED=$$((PASSED + 1)); \
		else \
		  echo "$(RED)✗ Test failed: $$app$(NC)"; \
		  FAILED=$$((FAILED + 1)); \
		  [ "$(FAIL_FAST)" = "1" ] && echo "$(RED)✗ stopping on first failure$(NC)" && exit 1; \
		fi; \
	  else \
		echo "$(RED)✗ Build failed: $$app$(NC)"; \
		FAILED=$$((FAILED + 1)); \
		[ "$(FAIL_FAST)" = "1" ] && echo "$(RED)✗ stopping on first failure$(NC)" && exit 1; \
	  fi; \
	  pkill -9 zephyr.exe 2>/dev/null || true; \
	done; \
	echo ""; \
	if [ $$FAILED -gt 0 ]; then \
	  echo "$(YELLOW)Test Summary:$(NC)"; \
	  echo "  Total: $$TOTAL"; \
	  echo "  $(GREEN)Success: $$PASSED$(NC)"; \
	  echo "  $(RED)Failed: $$FAILED$(NC)"; \
	  exit 1; \
	else \
	  echo "$(GREEN)✓ All $$PASSED tests passed$(NC)"; \
	fi; \
	pkill -9 zephyr.exe 2>/dev/null || true
	@if [ "$(COVERAGE)" = "1" ] && [ "$(COVERAGE_REPORT)" != "0" ]; then \
		$(MAKE) --no-print-directory _coverage_generate; \
	fi

#test_all: test_twister
test_all: test_each


################################################################################
# Format and Static Analysis
################################################################################

format_check:
	$(call check_tool,$(CLANG_FORMAT))
	@echo "$(YELLOW)Checking code format...$(NC)"
	@$(call verify_formatting)

format_all:
	$(call check_tool,$(CLANG_FORMAT))
	@echo "$(YELLOW)This will format all C++ source files$(NC)"
	@$(call list_source_files) | sed 's/^/  /'
	@echo -n "$(YELLOW)Continue? [y/N]: $(NC)"; \
	read answer; \
	if [ "$$answer" = "y" ] || [ "$$answer" = "Y" ]; then \
		echo "$(YELLOW)Formatting files...$(NC)"; \
		$(call list_source_files) | xargs $(CLANG_FORMAT) -i; \
		echo "$(GREEN)✓ All files formatted$(NC)"; \
	else \
		echo "$(YELLOW)Format cancelled$(NC)"; \
	fi

tidy: _tidy_prepare
	$(call check_tool,run-$(CLANG_TIDY))
	@echo "$(YELLOW)Running static analysis...$(NC)"
	@mkdir -p $(BUILD_TIDY_DIR)
	@LOG=$(BUILD_TIDY_DIR)/tidy.log; \
	HEADER_FILTER='$(TIDY_HEADER_FILTER)'; \
	FILE_REGEX='$(TIDY_SOURCE_FILTER)'; \
	QUIET_ARG=; \
	if [ "$(TIDY_STRICT)" != "1" ]; then \
		QUIET_ARG=-quiet; \
	fi; \
	if [ -n "$$FILE_REGEX" ]; then \
		run-$(CLANG_TIDY) -p $(BUILD_TIDY_DIR) -j $(TIDY_JOBS) -header-filter="$$HEADER_FILTER" $$QUIET_ARG "$$FILE_REGEX" >"$$LOG" 2>&1; \
	else \
		run-$(CLANG_TIDY) -p $(BUILD_TIDY_DIR) -j $(TIDY_JOBS) -header-filter="$$HEADER_FILTER" $$QUIET_ARG >"$$LOG" 2>&1; \
	fi; \
	STATUS=$$?; \
	if [ $$STATUS -ne 0 ]; then \
		cat "$$LOG"; \
		echo "$(RED)✗ clang-tidy invocation failed$(NC)"; \
		exit $$STATUS; \
	fi; \
	if grep -Eq "^[^:]+:[0-9]+:[0-9]+: error:" "$$LOG" || grep -q "Error while processing" "$$LOG"; then \
		cat "$$LOG"; \
		echo "$(RED)✗ clang-tidy errors$(NC)"; \
		exit 1; \
	fi; \
	if [ "$(TIDY_STRICT)" = "1" ]; then \
		cat "$$LOG"; \
	else \
		grep -E 'warning: ' "$$LOG" || true; \
	fi; \
	echo "$(GREEN)✓ Static analysis complete$(NC)"

cppcheck:
	@echo "$(YELLOW)Running cppcheck analysis...$(NC)"
	@VERBOSE_FLAG=; \
	DETAILED_FLAG=; \
	FAIL_FLAG=; \
	if [ "$(CPPCHECK_VERBOSE)" = "1" ]; then VERBOSE_FLAG=-v; fi; \
	if [ "$(CPPCHECK_DETAILED)" = "1" ]; then DETAILED_FLAG=-d; fi; \
	if [ "$(CPPCHECK_FAIL_ON_VIOLATIONS)" = "1" ]; then FAIL_FLAG=--fail-on-violations; fi; \
	if [ -x scripts/cppcheck_misra.py ]; then \
		python3 scripts/cppcheck_misra.py --config $(CPPCHECK_CONFIG) $$VERBOSE_FLAG $$DETAILED_FLAG $$FAIL_FLAG && \
		echo "$(GREEN)✓ cppcheck analysis complete$(NC)" || \
		{ echo "$(YELLOW)⚠ cppcheck violations found (see report above)$(NC)"; \
		  [ "$(CPPCHECK_FAIL_ON_VIOLATIONS)" = "1" ] && exit 1 || exit 0; }; \
	else \
		echo "$(RED)✗ cppcheck wrapper script not found or not executable$(NC)"; \
		exit 1; \
	fi


################################################################################
# CI Pipeline
################################################################################

ci:
	@echo "$(YELLOW)Starting CI pipeline...$(NC)"
	@$(MAKE) --no-print-directory _ci_sequential

_ci_sequential:
	@echo "$(YELLOW)[1/6] Building all samples...$(NC)"
	@$(MAKE) --no-print-directory build_all
	@echo "$(YELLOW)[2/6] Building and running all tests with coverage...$(NC)"
	@COVERAGE=1 COVERAGE_REPORT=0 $(MAKE) --no-print-directory test_all
	@echo "$(YELLOW)[3/6] Checking code format...$(NC)"
	@$(MAKE) --no-print-directory format_check
	@echo "$(YELLOW)[4/6] Running static analysis (clang-tidy)...$(NC)"
	@$(MAKE) --no-print-directory -j$(TIDY_JOBS) tidy
	@echo "$(YELLOW)[5/6] Running static analysis (cppcheck)...$(NC)"
	@$(MAKE) --no-print-directory cppcheck || echo "$(YELLOW)⚠ cppcheck violations detected (non-blocking)$(NC)"
	@echo "$(YELLOW)[6/6] Generating coverage report...$(NC)"
	@$(MAKE) --no-print-directory _coverage_generate
	@$(call check_coverage_thresholds)
	@echo ""
	@echo "$(GREEN)✓ CI pipeline completed successfully$(NC)"

################################################################################
# Coverage Analysis
################################################################################

coverage:
	@COVERAGE=1 $(MAKE) --no-print-directory test_each

coverage_view:
	@if [ -f "$(COVERAGE_DIR)/index.html" ]; then \
		echo "$(YELLOW)Opening coverage report...$(NC)"; \
		if xdg-open $(COVERAGE_DIR)/index.html 2>/dev/null || firefox $(COVERAGE_DIR)/index.html 2>/dev/null; then \
			echo "$(GREEN)✓ Coverage report opened in browser$(NC)"; \
		else \
			echo "$(YELLOW)Browser not available, showing text summary:$(NC)"; \
			if [ -f "$(COVERAGE_DIR)/coverage.json" ] && [ -x scripts/coverage_summary.py ]; then \
				python3 scripts/coverage_summary.py $(COVERAGE_DIR)/coverage.json; \
			else \
				echo "Coverage report available at: $(COVERAGE_DIR)/index.html"; \
			fi; \
		fi; \
	else \
		echo "$(RED)Coverage report not found. Run 'make coverage' first.$(NC)"; \
		exit 1; \
	fi

################################################################################
# Utility Targets
################################################################################

list_samples:
	@echo "$(YELLOW)Available samples:$(NC)"
	@find src -path "*/samples/*" -name CMakeLists.txt -exec dirname {} \; | sort | sed 's/^/  /'

list_tests:
	@echo "$(YELLOW)Available tests:$(NC)"
	@find src -path "*/tests" -type d | sort | sed 's/^/  /'

################################################################################
# Extended Help
################################################################################

help_config:
	@echo "$(BLUE)Configuration Options:$(NC)"
	@echo ""
	@echo "$(YELLOW)Build Configuration:$(NC)"
	@echo "  BOARD=$(BOARD)                    # Target board"
	@echo "  PRISTINE=$(PRISTINE)                   # Pristine builds (never/auto/always)"
	@echo "  COVERAGE=$(COVERAGE)                      # Enable coverage (0/1)"
	@echo "  TWISTER_JOBS=$(TWISTER_JOBS)                   # Parallel jobs"
	@echo "  TWISTER_CACHE=$(TWISTER_CACHE)                  # Caching level (0-3)"
	@echo "  TWISTER_VERBOSE=$(TWISTER_VERBOSE)                # Verbose output"
	@echo "  FAIL_FAST=$(FAIL_FAST)                     # Stop on first failure"
	@echo "  TEST_TIMEOUT=$(TEST_TIMEOUT)                  # Test execution timeout (seconds)"
	@echo "  SAMPLE_TIMEOUT=$(SAMPLE_TIMEOUT)                # Sample execution timeout (seconds)"
	@echo ""
	@echo "$(YELLOW)Static Analysis:$(NC)"
	@echo "  TIDY_STRICT=$(TIDY_STRICT)                   # Show full clang-tidy output"
	@echo "  TIDY_JOBS=$(TIDY_JOBS)                     # Parallel tidy jobs"
	@echo "  CPPCHECK_VERBOSE=$(CPPCHECK_VERBOSE)                 # Verbose cppcheck output"
	@echo "  CPPCHECK_DETAILED=$(CPPCHECK_DETAILED)                # Detailed cppcheck violations"
	@echo "  CPPCHECK_FAIL_ON_VIOLATIONS=$(CPPCHECK_FAIL_ON_VIOLATIONS)    # Fail build on violations"
	@echo ""
	@echo "$(YELLOW)Coverage Thresholds:$(NC)"
	@echo "  CI_LINE_THRESHOLD=$(CI_LINE_THRESHOLD)            # Line coverage %"
	@echo "  CI_BRANCH_THRESHOLD=$(CI_BRANCH_THRESHOLD)          # Branch coverage %"
	@echo "  CI_FUNCTION_THRESHOLD=$(CI_FUNCTION_THRESHOLD)        # Function coverage %"

help_targets:
	@echo "$(BLUE)All Available Targets:$(NC)"
	@echo ""
	@echo "$(YELLOW)Build Targets:$(NC)"
	@echo "  build <path>        Build specific sample"
	@echo "  run <path>          Build and run sample"
	@echo "  menuconfig <path>   Configure sample"
	@echo "  build_all           Build all samples"
	@echo ""
	@echo "$(YELLOW)Test Targets:$(NC)"
	@echo "  test <path>         Run specific test"
	@echo "  test_twister        Run all tests via Twister"
	@echo "  test_all            Alias for test_twister"
	@echo ""
	@echo "$(YELLOW)Quality Targets:$(NC)"
	@echo "  format_check        Verify code formatting"
	@echo "  format_all          Format all source files"
	@echo "  tidy                Run clang-tidy static analysis"
	@echo "  cppcheck            Run cppcheck (MISRA, threadsafety, y2038)"
	@echo "  coverage            Generate coverage report"
	@echo "  coverage_view       View coverage report"
	@echo ""
	@echo "$(YELLOW)CI and Utility:$(NC)"
	@echo "  ci                  Full CI pipeline"
	@echo "  shell               Enter CI Docker environment"
	@echo "  docker_build        Build CI Docker image"
	@echo "  clean               Remove build artifacts"
	@echo "  list_samples        List available samples"
	@echo "  list_tests          List available tests"
	@echo "  version             Show version info"

help_examples:
	@echo "$(BLUE)Usage Examples:$(NC)"
	@echo ""
	@echo "$(YELLOW)Basic Usage:$(NC)"
	@echo "  make build src/example_lib/samples/basic"
	@echo "  make run src/example_lib/samples/basic"
	@echo "  make test src/example_lib/tests"
	@echo ""
	@echo "$(YELLOW)Configuration Examples:$(NC)"
	@echo "  COVERAGE=1 make test src/example_lib/tests          # Test with coverage"
	@echo "  PRISTINE=always make build_all                      # Force clean builds"
	@echo "  TWISTER_CACHE=0 make test src/example_lib/tests     # Clean build"
	@echo "  TWISTER_JOBS=16 make test_all                       # Custom parallelism"
	@echo "  TIDY_STRICT=1 make tidy                             # Verbose clang-tidy output"
	@echo "  CPPCHECK_DETAILED=1 make cppcheck                   # Detailed violations inline"
	@echo "  CPPCHECK_VERBOSE=1 make cppcheck                    # Verbose cppcheck output"
	@echo "  CPPCHECK_FAIL_ON_VIOLATIONS=1 make cppcheck         # Fail on violations"
	@echo ""
	@echo "$(YELLOW)CI and Coverage:$(NC)"
	@echo "  make ci                                             # Full pipeline"
	@echo "  make coverage && make coverage_view                 # Generate and view coverage"

################################################################################
# Internal Helper Targets
################################################################################

_tidy_prepare:
	$(call check_tool,west)
	@mkdir -p $(BUILD_TIDY_DIR)
	@if [ ! -f $(BUILD_TIDY_DIR)/compile_commands.json ]; then \
		echo "$(YELLOW)Generating compile database...$(NC)"; \
	else \
		echo "$(YELLOW)Refreshing compile database...$(NC)"; \
	fi
	@west build -b $(BOARD) src/develop_target -d $(BUILD_TIDY_DIR) -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@if [ ! -f $(BUILD_TIDY_DIR)/compile_commands.json ]; then \
		echo "$(RED)✗ compile_commands.json not found$(NC)"; \
		exit 1; \
	fi
	@echo "$(YELLOW)Normalizing compile database...$(NC)"
	@FILE=$(BUILD_TIDY_DIR)/compile_commands.json; \
	sed -i 's| --param=[^ ]*||g' "$$FILE"; \
	for FLAG in -fno-freestanding -ffreestanding -fno-reorder-functions -fno-defer-pop -fcheck-new -fmacro-prefix-map= -Wexpansion-to-defined -Wno-unused-but-set-variable -Wno-register; do \
		sed -i "s| $$FLAG[^ ]*||g" "$$FILE"; \
	done; \
	sed -i 's|  | |g' "$$FILE"
	@echo "$(GREEN)✓ Compile database ready$(NC)"


_coverage_generate:
	$(call check_tool,gcovr)
	@mkdir -p $(COVERAGE_DIR)
	@$(call generate_coverage_report)

################################################################################
# Helper Functions
################################################################################

# Tool validation function
# Usage: $(call check_tool,command_name)
define check_tool
	@if ! command -v $(1) >/dev/null 2>&1; then \
		echo "$(RED)✗ Required tool not found: $(1)$(NC)"; \
		exit 1; \
	fi
endef

# Extract target path from command line arguments
TARGET_PATH = $(filter-out $@,$(MAKECMDGOALS))

# Validate target path argument
define validate_target_path
	@if [ -z "$(TARGET_PATH)" ]; then \
		echo "$(RED)Error: Please specify a path$(NC)"; \
		echo "Usage: make $1 <relative_path>"; \
		echo "Example: make $1 src/example_lib/samples/basic"; \
		exit 1; \
	fi; \
	if [ ! -d "$(TARGET_PATH)" ]; then \
		echo "$(RED)Error: Directory $(TARGET_PATH) does not exist$(NC)"; \
		exit 1; \
	fi
endef

# Build a sample
define build_sample
	@set -e; \
	if [ ! -f "$1/CMakeLists.txt" ]; then \
		echo "$(RED)Error: $1 is not a valid sample (no CMakeLists.txt)$(NC)"; \
		exit 1; \
	fi; \
	mkdir -p $(BUILD_SAMPLES_DIR); \
	BUILD_NAME=$$(echo "$1" | sed 's|/|_|g'); \
	BUILD_DIR="$(BUILD_SAMPLES_DIR)/$$BUILD_NAME-$(BOARD)"; \
	echo "$(YELLOW)Building: $1 -> $$BUILD_DIR$(NC)"; \
	if west build -b $(BOARD) "$1" -d "$$BUILD_DIR"; then \
		echo "$(GREEN)✓ Build successful: $1$(NC)"; \
		BUILD_SUCCESS=0; \
	else \
		echo "$(RED)✗ Build failed: $1$(NC)"; \
		BUILD_SUCCESS=1; \
	fi; \
	pkill -9 zephyr.exe 2>/dev/null || true; \
	exit $$BUILD_SUCCESS
endef

# Run a sample
define run_sample
	@set -e; \
	BUILD_NAME=$$(echo "$1" | sed 's|/|_|g'); \
	BUILD_DIR="$(BUILD_SAMPLES_DIR)/$$BUILD_NAME-$(BOARD)"; \
	EXECUTABLE="$$BUILD_DIR/zephyr/zephyr.exe"; \
	if [ -f "$$EXECUTABLE" ]; then \
		echo "$(YELLOW)Executing: $$EXECUTABLE$(NC)"; \
		if timeout -s INT -k 2 $(SAMPLE_TIMEOUT) "$$EXECUTABLE"; then \
			echo "$(GREEN)✓ Execution completed: $1$(NC)"; \
			RUN_SUCCESS=0; \
		else \
			echo "$(YELLOW)⚠ Execution ended (timeout/exit): $1$(NC)"; \
			RUN_SUCCESS=1; \
		fi; \
	else \
		echo "$(RED)✗ Executable not found: $$EXECUTABLE$(NC)"; \
		RUN_SUCCESS=1; \
	fi; \
	pkill -9 zephyr.exe 2>/dev/null || true; \
	exit $$RUN_SUCCESS
endef

# Run menuconfig for a sample
define run_menuconfig
	@BUILD_NAME=$$(echo "$1" | sed 's|/|_|g'); \
	BUILD_DIR="$(BUILD_SAMPLES_DIR)/$$BUILD_NAME-$(BOARD)"; \
	if [ ! -d "$$BUILD_DIR" ]; then \
		echo "$(YELLOW)Build directory missing. Building first...$(NC)"; \
		$(call build_sample,$1); \
	fi; \
	echo "$(YELLOW)Running menuconfig: $1$(NC)"; \
	west build -b $(BOARD) "$1" -d "$$BUILD_DIR" -t menuconfig && \
	echo "$(GREEN)✓ Menuconfig completed: $1$(NC)" || \
	(echo "$(RED)✗ Menuconfig failed: $1$(NC)" && exit 1)
endef

# Run a test
define run_test
	@echo "$(YELLOW)Running tests: $1$(NC)"; \
	if [ "$(TWISTER_CACHE)" = "3" ] && [ -f "$1/CMakeLists.txt" ]; then \
		echo "$(YELLOW)Using west build with smart caching...$(NC)"; \
		mkdir -p $(BUILD_TESTS_DIR); \
		BUILD_NAME=$$(echo "$1" | sed 's|/|_|g'); \
		COVERAGE_SUFFIX=""; if [ "$(COVERAGE)" = "1" ]; then COVERAGE_SUFFIX="-coverage"; fi; \
		BUILD_DIR="$(BUILD_TESTS_DIR)/$$BUILD_NAME$$COVERAGE_SUFFIX"; \
		COVERAGE_FLAGS=""; \
		if [ "$(COVERAGE)" = "1" ]; then COVERAGE_FLAGS="-- -DCONFIG_COVERAGE=y"; fi; \
		if west build -b $(BOARD) "$1" -d "$$BUILD_DIR" -p $(PRISTINE) $$COVERAGE_FLAGS; then \
			echo "$(YELLOW)Executing test binary...$(NC)"; \
			timeout -s INT -k 2 $(TEST_TIMEOUT) "$$BUILD_DIR/zephyr/zephyr.exe" && \
			echo "$(GREEN)✓ Tests passed: $1$(NC)" || \
			(echo "$(RED)✗ Tests failed: $1$(NC)" && exit 1); \
		else \
			echo "$(RED)✗ Build failed: $1$(NC)"; \
			exit 1; \
		fi; \
	else \
		west twister -T "$1" -p $(BOARD) --inline-logs -v $(TWISTER_OPTS) -O $(TWISTER_OUTDIR) && \
		echo "$(GREEN)✓ Tests passed: $1$(NC)" || \
		(echo "$(RED)✗ Tests failed: $1$(NC)" && exit 1); \
	fi; \
	pkill -9 zephyr.exe 2>/dev/null || true
endef

# List all source files
define list_source_files
	find src -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.c" | grep -v build | sort
endef

# Verify code formatting
define verify_formatting
	@FILES=$$($(call list_source_files)); \
	if [ -z "$$FILES" ]; then echo "$(GREEN)✓ No source files found$(NC)"; exit 0; fi; \
	FAIL=0; \
	for f in $$FILES; do \
		if ! $(CLANG_FORMAT) "$$f" | diff -u "$$f" - >/dev/null; then \
			echo "$(RED)✗ Not formatted: $$f$(NC)"; \
			FAIL=1; \
		fi; \
	done; \
	if [ $$FAIL -eq 0 ]; then \
		echo "$(GREEN)✓ All files properly formatted$(NC)"; \
	else \
		echo "$(YELLOW)Hint: run 'make format_all' to fix formatting$(NC)"; \
		exit 1; \
	fi
endef

# Check coverage thresholds
define check_coverage_thresholds
	@if [ "$(CI_COVERAGE_CHECK)" = "1" ]; then \
		echo "$(YELLOW)Checking coverage thresholds...$(NC)"; \
		if gcovr -r $(PWD) --filter 'src/.*' --exclude '.*test.*' --exclude '.*sample.*' --exclude '.*build.*' \
			--fail-under-line $(CI_LINE_THRESHOLD) --fail-under-branch $(CI_BRANCH_THRESHOLD) \
			--fail-under-function $(CI_FUNCTION_THRESHOLD) --print-summary >/dev/null 2>&1; then \
			echo "$(GREEN)✓ Coverage thresholds met (L:$(CI_LINE_THRESHOLD)% B:$(CI_BRANCH_THRESHOLD)% F:$(CI_FUNCTION_THRESHOLD)%)$(NC)"; \
		else \
			echo "$(RED)✗ Coverage below thresholds (L:$(CI_LINE_THRESHOLD)% B:$(CI_BRANCH_THRESHOLD)% F:$(CI_FUNCTION_THRESHOLD)%)$(NC)"; \
			exit 1; \
		fi; \
	fi
endef

# Generate coverage report
define generate_coverage_report
	@echo "$(YELLOW)Generating coverage report...$(NC)"
	@gcovr -r $(PWD) --filter 'src/.*' --exclude '.*test.*' --exclude '.*sample.*' --exclude '.*build.*' \
		--html-details $(COVERAGE_DIR)/index.html --json $(COVERAGE_DIR)/coverage.json --print-summary
	@echo "$(GREEN)✓ Coverage report: $(COVERAGE_DIR)/index.html$(NC)"
	@[ -x scripts/coverage_summary.py ] && python3 scripts/coverage_summary.py $(COVERAGE_DIR)/coverage.json || true
endef

# Batch operation helper
define batch_operation
	@FAILED=0; TOTAL=0; SUCCESS=0; \
	for item in $$(find src -path "*/$1/*" -name CMakeLists.txt -exec dirname {} \; | sort); do \
		TOTAL=$$((TOTAL + 1)); \
		echo "$(YELLOW)Processing: $$item$(NC)"; \
		if $(call $2,$$item); then \
			echo "$(GREEN)✓ $$item$(NC)"; \
			SUCCESS=$$((SUCCESS + 1)); \
		else \
			echo "$(RED)✗ $$item$(NC)"; \
			FAILED=$$((FAILED + 1)); \
			[ "$(FAIL_FAST)" = "1" ] && echo "$(RED)✗ stopping on first failure$(NC)" && exit 1; \
		fi; \
		pkill -9 zephyr.exe 2>/dev/null || true; \
	done; \
	echo ""; \
	if [ $$FAILED -gt 0 ]; then \
		echo "$(YELLOW)Summary: $$SUCCESS/$$TOTAL succeeded, $$FAILED failed$(NC)"; \
		exit 1; \
	else \
		echo "$(GREEN)✓ All $$SUCCESS items completed successfully$(NC)"; \
	fi
endef

# Batch build helper
define build_sample_batch
	mkdir -p $(BUILD_SAMPLES_DIR); \
	BUILD_NAME=$$(echo "$1" | sed 's|/|_|g'); \
	BUILD_DIR="$(BUILD_SAMPLES_DIR)/$$BUILD_NAME-$(BOARD)"; \
	west build -b $(BOARD) "$1" -d "$$BUILD_DIR" -p $(PRISTINE)
endef

# Prevent make from treating arguments as targets when using path arguments
%:
	@:
