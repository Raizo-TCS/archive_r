#!/bin/bash

# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team
# archive_r Test Runner Script
# Usage: ./run_tests.sh [--perf-archive <path>]

echo "DEBUG: run_tests.sh started"

set -e
set -o pipefail

detect_timeout_cmd() {
    if command -v timeout >/dev/null 2>&1; then
        echo "timeout"
    elif command -v gtimeout >/dev/null 2>&1; then
        echo "gtimeout"
    else
        echo ""
    fi
}

# Helper to find executable (Unix or Windows/MSVC)
find_executable() {
    local name="$1"
    if [ -x "$BUILD_DIR/$name.exe" ]; then
        echo "$BUILD_DIR/$name.exe"
    elif [ -x "$BUILD_DIR/core/$name.exe" ]; then
        echo "$BUILD_DIR/core/$name.exe"
    elif [ -x "$BUILD_DIR/Release/$name.exe" ]; then
        echo "$BUILD_DIR/Release/$name.exe"
    elif [ -x "$BUILD_DIR/core/Release/$name.exe" ]; then
        echo "$BUILD_DIR/core/Release/$name.exe"
    elif [ -x "$BUILD_DIR/$name" ]; then
        echo "$BUILD_DIR/$name"
    elif [ -f "$BUILD_DIR/$name.exe" ]; then
        echo "$BUILD_DIR/$name.exe"
    elif [ -f "$BUILD_DIR/core/$name.exe" ]; then
        echo "$BUILD_DIR/core/$name.exe"
    elif [ -f "$BUILD_DIR/Release/$name.exe" ]; then
        echo "$BUILD_DIR/Release/$name.exe"
    elif [ -f "$BUILD_DIR/core/Release/$name.exe" ]; then
        echo "$BUILD_DIR/core/Release/$name.exe"
    elif [ -f "$BUILD_DIR/$name" ]; then
        echo "$BUILD_DIR/$name"
    else
        echo ""
    fi
}

# === Configuration ===
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR" || exit 1
BUILD_DIR="$ROOT_DIR/build"
TEST_DATA_DIR="$ROOT_DIR/test_data"
EXECUTABLE=$(find_executable "find_and_traverse")
if [ -z "$EXECUTABLE" ]; then
    EXECUTABLE="$BUILD_DIR/find_and_traverse" # Fallback for error message
fi
echo "Using test executable: $EXECUTABLE"
TIMEOUT=20
WRAPPER_TIMEOUT_DEFAULT=600
WRAPPER_TIMEOUT="${RUN_TESTS_WRAPPER_TIMEOUT:-$WRAPPER_TIMEOUT_DEFAULT}"
ORIGINAL_ARGS=("$@")
PERF_RAW_AVG=""
PERF_TRAVERSER_AVG=""
PERF_RATIO=""
PERF_ARCHIVE_PATH="$TEST_DATA_DIR/test_perf.zip"
# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# === Argument Parsing ===
print_usage() {
    cat <<USAGE
Usage: ./run_tests.sh [--perf-archive <path>]
Options:
  --perf-archive <path>  Use the specified archive for performance_compare test.
                         Relative paths are resolved against the repository root.
  --wrapper-timeout <s>  Override global timeout (seconds). Use 0 to disable.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --perf-archive)
            shift
            if [[ -z "$1" ]]; then
                echo "Error: --perf-archive requires a path" >&2
                exit 1
            fi
            if [[ "$1" = /* ]]; then
                PERF_ARCHIVE_PATH="$1"
            else
                PERF_ARCHIVE_PATH="$ROOT_DIR/$1"
            fi
            ;;
        --wrapper-timeout)
            shift
            if [[ -z "$1" ]]; then
                echo "Error: --wrapper-timeout requires seconds" >&2
                exit 1
            fi
            WRAPPER_TIMEOUT="$1"
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo "Warning: Unknown argument '$1'" >&2
            ;;
    esac
    shift
done

validate_wrapper_timeout() {
    local value="$1"
    if [[ -z "$value" ]]; then
        echo "Error: wrapper timeout value is empty" >&2
        exit 1
    fi
    if ! [[ "$value" =~ ^[0-9]+$ ]]; then
        echo "Error: wrapper timeout must be a non-negative integer" >&2
        exit 1
    fi
}

validate_wrapper_timeout "$WRAPPER_TIMEOUT"

if [[ -z "$RUN_TESTS_TIMEOUT_ACTIVE" ]] && [[ "$WRAPPER_TIMEOUT" -gt 0 ]]; then
    TIMEOUT_CMD_GLOBAL=$(detect_timeout_cmd)
    if [ -n "$TIMEOUT_CMD_GLOBAL" ]; then
        export RUN_TESTS_TIMEOUT_ACTIVE=1
        export RUN_TESTS_WRAPPER_TIMEOUT="$WRAPPER_TIMEOUT"
        exec "$TIMEOUT_CMD_GLOBAL" "$WRAPPER_TIMEOUT" "$0" "${ORIGINAL_ARGS[@]}"
    else
        echo "Warning: 'timeout' command not found; continuing without global timeout" >&2
    fi
fi

export RUN_TESTS_TIMEOUT_ACTIVE=1

# === Test Counters ===
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# === Utility Functions ===

TIMEOUT_CMD=$(detect_timeout_cmd)

run_with_timeout_cmd() {
    local duration="$1"
    shift
    if [ -n "$TIMEOUT_CMD" ]; then
        "$TIMEOUT_CMD" "$duration" "$@"
    else
        "$@"
    fi
}

log_info() {
    echo -e "${BLUE}▶${NC} $1"
}

log_success() {
    echo -e "${GREEN}✓${NC} $1"
}

log_error() {
    echo -e "${RED}✗${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

log_test_header() {
    echo ""
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}  TEST: $1${NC}"
    echo -e "${CYAN}========================================${NC}"
}

# === Test Function ===
run_test() {
    local test_file="$1"
    local test_name="$(basename "$test_file" .tar.gz)"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    log_test_header "$test_name"
    
    if [ ! -f "$test_file" ]; then
        log_error "Test file not found: $test_file"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
    
    log_info "Running: $EXECUTABLE \"$test_file\""
    
    if run_with_timeout_cmd "$TIMEOUT" "$EXECUTABLE" "$test_file"; then
        log_success "Test PASSED: $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        local exit_code=$?
        if [ $exit_code -eq 124 ]; then
            log_error "Test TIMEOUT: $test_name (exceeded ${TIMEOUT}s)"
        else
            log_error "Test FAILED: $test_name (exit code: $exit_code)"
        fi
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# === Verification ===
if [ ! -f "$EXECUTABLE" ]; then
    log_error "Executable not found: $EXECUTABLE"
    log_info "Please run ./build.sh first"
    exit 1
fi

if [ ! -d "$TEST_DATA_DIR" ]; then
    log_error "Test data directory not found: $TEST_DATA_DIR"
    exit 1
fi

# === Main Test Execution ===
log_info "archive_r Test Runner Starting..."
log_info "Executable: $EXECUTABLE"
log_info "Test Data:  $TEST_DATA_DIR"
echo ""

# Phase 1 Tests (Archive traversal)
run_test "$TEST_DATA_DIR/multi_volume_test.tar.gz" || true
run_test "$TEST_DATA_DIR/nested_with_multi_volume.tar.gz" || true
run_test "$TEST_DATA_DIR/deeply_nested.tar.gz" || true
run_test "$TEST_DATA_DIR/deeply_nested_multi_volume.tar.gz" || true
run_test "$TEST_DATA_DIR/stress_test_ultimate.tar.gz" || true

log_info "Running iterator and descent regression tests..."
TEST_EXE=$(find_executable "test_simple_count")
if [ -n "$TEST_EXE" ]; then
    log_test_header "simple_count"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$TEST_DATA_DIR/deeply_nested.tar.gz"; then
        log_success "Test PASSED: simple_count"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: simple_count"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
else
    log_warning "test_simple_count executable not found - skipping"
fi

TEST_EXE=$(find_executable "test_iterator")
if [ -n "$TEST_EXE" ]; then
    log_test_header "iterator"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$TEST_DATA_DIR/deeply_nested.tar.gz"; then
        log_success "Test PASSED: iterator"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: iterator"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
else
    log_warning "test_iterator executable not found - skipping"
fi

TEST_EXE=$(find_executable "test_descent")
if [ -n "$TEST_EXE" ]; then
    log_test_header "descent"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$TEST_DATA_DIR/deeply_nested.tar.gz"; then
        log_success "Test PASSED: descent"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: descent"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
else
    log_warning "test_descent executable not found - skipping"
fi

TEST_EXE=$(find_executable "test_skip_descent")
if [ -n "$TEST_EXE" ]; then
    log_test_header "skip_descent"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$TEST_DATA_DIR/deeply_nested.tar.gz"; then
        log_success "Test PASSED: skip_descent"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: skip_descent"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
else
    log_warning "test_skip_descent executable not found - skipping"
fi

TEST_EXE=$(find_executable "test_entry_read")
if [ -n "$TEST_EXE" ]; then
    log_test_header "entry_read"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE"; then
        log_success "Test PASSED: entry_read"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: entry_read"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

TEST_EXE=$(find_executable "test_nested_root_acceptance")
if [ -n "$TEST_EXE" ]; then
    log_test_header "nested_root_acceptance"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$TEST_DATA_DIR/deeply_nested.tar.gz"; then
        log_success "Test PASSED: nested_root_acceptance"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: nested_root_acceptance"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

# Error handling Step0 regression tests
log_info "Running Error Handling Step0 tests..."
TEST_EXE=$(find_executable "test_error_handling_step0")
if [ -n "$TEST_EXE" ]; then
    log_test_header "error_handling_step0"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE"; then
        log_success "Test PASSED: error_handling_step0"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: error_handling_step0"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

log_info "Running RootStreamFactory tests..."
TEST_EXE=$(find_executable "test_custom_root_stream")
if [ -n "$TEST_EXE" ]; then
    log_test_header "custom_root_stream"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE"; then
        log_success "Test PASSED: custom_root_stream"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: custom_root_stream"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

log_info "Running Traverser thread-safety regression..."
TEST_EXE=$(find_executable "test_threaded_traverser")
if [ -n "$TEST_EXE" ]; then
    log_test_header "threaded_traverser"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE"; then
        log_success "Test PASSED: threaded_traverser"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: threaded_traverser"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

# Multi-volume functionality test
log_info "Running Multi-Volume functionality test..."
TEST_EXE=$(find_executable "test_multi_volume_functionality")
if [ -n "$TEST_EXE" ]; then
    log_test_header "multi_volume_functionality"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$TEST_DATA_DIR/multi_volume_test.tar.gz"; then
        log_success "Test PASSED: multi_volume_functionality"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_warning "Test FAILED: multi_volume_functionality (EXPECTED - not yet implemented)"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

log_info "Running Multi-Volume ordering verification..."
TEST_EXE=$(find_executable "test_multi_volume_ordering")
if [ -n "$TEST_EXE" ]; then
    log_test_header "multi_volume_ordering"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE"; then
        log_success "Test PASSED: multi_volume_ordering"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: multi_volume_ordering"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

log_info "Running Multi-Volume traversal regression..."
TEST_EXE=$(find_executable "test_multi_volume_traversal_regression")
if [ -n "$TEST_EXE" ]; then
    log_test_header "multi_volume_traversal_regression"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE"; then
        log_success "Test PASSED: multi_volume_traversal_regression"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: multi_volume_traversal_regression"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

log_info "Running Multi-Volume entry persistence test..."
TEST_EXE=$(find_executable "test_multi_volume_entry_persistence")
if [ -n "$TEST_EXE" ]; then
    log_test_header "multi_volume_entry_persistence"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE"; then
        log_success "Test PASSED: multi_volume_entry_persistence"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: multi_volume_entry_persistence"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

log_info "Running Root File multi-volume activation test..."
TEST_EXE=$(find_executable "test_root_file_multi_volume")
if [ -n "$TEST_EXE" ]; then
    log_test_header "root_file_multi_volume"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE"; then
        log_success "Test PASSED: root_file_multi_volume"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: root_file_multi_volume"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

log_info "Running additional multi-volume regression tests..."
TEST_EXE=$(find_executable "test_multi_volume_input")
if [ -n "$TEST_EXE" ]; then
    log_test_header "multi_volume_input"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$TEST_DATA_DIR/test_input.tar.gz.part00"; then
        log_success "Test PASSED: multi_volume_input"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: multi_volume_input"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

TEST_EXE=$(find_executable "test_multi_volume_iterator")
if [ -n "$TEST_EXE" ]; then
    log_test_header "multi_volume_iterator"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$TEST_DATA_DIR/multi_volume_test.tar.gz"; then
        log_success "Test PASSED: multi_volume_iterator"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: multi_volume_iterator"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

TEST_EXE=$(find_executable "test_multi_volume_debug")
if [ -n "$TEST_EXE" ]; then
    log_test_header "multi_volume_debug"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$TEST_DATA_DIR/test_input.tar.gz.part00"; then
        log_success "Test PASSED: multi_volume_debug"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: multi_volume_debug"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

TEST_EXE=$(find_executable "test_multi_volume_retry")
if [ -n "$TEST_EXE" ]; then
    log_test_header "multi_volume_retry"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE"; then
        log_success "Test PASSED: multi_volume_retry"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: multi_volume_retry"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

log_info "Running ArchiveStackOrchestrator read verification..."
TEST_EXE=$(find_executable "test_data_source_reader_read")
if [ -n "$TEST_EXE" ]; then
    log_test_header "data_source_reader_read"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE"; then
        log_success "Test PASSED: data_source_reader_read"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: data_source_reader_read"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

# Stress test ultimate verification
log_info "Running Stress Test Ultimate verification..."
TEST_EXE=$(find_executable "test_stress_ultimate_verification")
if [ -n "$TEST_EXE" ]; then
    log_test_header "stress_ultimate_verification"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd 60 "$TEST_EXE" "$TEST_DATA_DIR/stress_test_ultimate.tar.gz"; then
        log_success "Test PASSED: stress_ultimate_verification"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: stress_ultimate_verification"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

# Stress test multi-volume source
log_info "Running Stress Test Multi-Volume Source verification..."
TEST_EXE=$(find_executable "test_stress_multi_volume_source")
if [ -n "$TEST_EXE" ]; then
    log_test_header "stress_multi_volume_source"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd 60 "$TEST_EXE" "$TEST_DATA_DIR/stress_test_ultimate.tar.gz.part001"; then
        log_success "Test PASSED: stress_multi_volume_source"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: stress_multi_volume_source"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

log_info "Running Stress Test Multi-Volume source (legacy traversal)..."
TEST_EXE=$(find_executable "test_stress_multipart_source")
if [ -n "$TEST_EXE" ]; then
    log_test_header "stress_multipart_source"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd 60 "$TEST_EXE" "$TEST_DATA_DIR/stress_test_ultimate.tar.gz.part001"; then
        log_success "Test PASSED: stress_multipart_source"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: stress_multipart_source"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

log_info "Running Stress Test Ultimate validation (legacy stack)..."
TEST_EXE=$(find_executable "test_stress_ultimate_validation")
if [ -n "$TEST_EXE" ]; then
    log_test_header "stress_ultimate_validation"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd 60 "$TEST_EXE" "$TEST_DATA_DIR/stress_test_ultimate.tar.gz"; then
        log_success "Test PASSED: stress_ultimate_validation"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: stress_ultimate_validation"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

# Metadata option verification
log_info "Running metadata option verification..."
TEST_EXE=$(find_executable "test_metadata_options")
if [ -n "$TEST_EXE" ]; then
    log_test_header "metadata_options"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" \
        "$TEST_DATA_DIR/deeply_nested.tar.gz" \
        "$TEST_DATA_DIR/no_uid.zip"; then
        log_success "Test PASSED: metadata_options"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: metadata_options"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

log_info "Running filesystem metadata verification..."
TEST_EXE=$(find_executable "test_metadata_filesystem")
if [ -n "$TEST_EXE" ]; then
    log_test_header "metadata_filesystem"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" \
        "$TEST_DATA_DIR/directory_test"; then
        log_success "Test PASSED: metadata_filesystem"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: metadata_filesystem"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
fi

# Phase 2 Tests (Directory support)
log_info "Running Phase 2 tests (Directory support)..."
TEST_EXE=$(find_executable "test_directory_container")
if [ -n "$TEST_EXE" ]; then
    log_test_header "directory_container_basic"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$TEST_DATA_DIR/directory_test"; then
        log_success "Test PASSED: directory_container_basic"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: directory_container_basic"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
fi

TEST_EXE=$(find_executable "test_directory_navigation")
if [ -n "$TEST_EXE" ]; then
    log_test_header "directory_navigation_state"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$TEST_DATA_DIR/directory_test"; then
        log_success "Test PASSED: directory_navigation_state"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: directory_navigation_state"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
fi

TEST_EXE=$(find_executable "test_multi_root_traversal")
if [ -n "$TEST_EXE" ]; then
    log_test_header "multi_root_traversal"
    TESTS_RUN=$((TESTS_RUN + 1))
    if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" \
        "$TEST_DATA_DIR/deeply_nested.tar.gz" \
        "$TEST_DATA_DIR/directory_test"; then
        log_success "Test PASSED: multi_root_traversal"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "Test FAILED: multi_root_traversal"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
fi

log_info "Running performance comparison archive: $PERF_ARCHIVE_PATH"
TEST_EXE=$(find_executable "test_performance_compare")
if [ -n "$TEST_EXE" ]; then
    log_test_header "performance_compare"
    TESTS_RUN=$((TESTS_RUN + 1))
    if [ ! -f "$PERF_ARCHIVE_PATH" ]; then
        log_error "Performance archive not found: $PERF_ARCHIVE_PATH"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        echo ""
    else
        perf_tmp="$(mktemp)"
        if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$PERF_ARCHIVE_PATH" | tee "$perf_tmp"; then
            log_success "Test PASSED: performance_compare"
            TESTS_PASSED=$((TESTS_PASSED + 1))
            raw_avg=$(sed -n 's/.*libarchive (raw) avg: \([0-9.]*\) s/\1/p' "$perf_tmp" | tail -n 1)
            traverser_avg=$(sed -n 's/.*archive_r Traverser avg: \([0-9.]*\) s/\1/p' "$perf_tmp" | tail -n 1)
            if [ -n "$raw_avg" ] && [ -n "$traverser_avg" ]; then
                log_info "performance_compare summary → libarchive=${raw_avg}s, archive_r=${traverser_avg}s"
                PERF_RAW_AVG="$raw_avg"
                PERF_TRAVERSER_AVG="$traverser_avg"
                PERF_RATIO=$(awk -v traverser="$traverser_avg" -v raw="$raw_avg" 'BEGIN { if (raw > 0) printf "%.3f", traverser / raw }')
                if [ -n "$PERF_RATIO" ]; then
                    log_info "performance_compare ratio → archive_r/libarchive=${PERF_RATIO}x"
                fi
            fi
        else
            log_error "Test FAILED: performance_compare"
            TESTS_FAILED=$((TESTS_FAILED + 1))
        fi
        rm -f "$perf_tmp"
    fi
    echo ""
fi

TEST_EXE=$(find_executable "test_performance_no_descend")
if [ -n "$TEST_EXE" ]; then
    log_test_header "performance_no_descend"
    TESTS_RUN=$((TESTS_RUN + 1))
    if [ ! -f "$PERF_ARCHIVE_PATH" ]; then
        log_error "Performance archive not found: $PERF_ARCHIVE_PATH"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        echo ""
    else
        perf_node_tmp="$(mktemp)"
        if run_with_timeout_cmd "$TIMEOUT" "$TEST_EXE" "$PERF_ARCHIVE_PATH" | tee "$perf_node_tmp"; then
            log_success "Test PASSED: performance_no_descend"
            TESTS_PASSED=$((TESTS_PASSED + 1))
        else
            log_error "Test FAILED: performance_no_descend"
            TESTS_FAILED=$((TESTS_FAILED + 1))
        fi
        rm -f "$perf_node_tmp"
    fi
    echo ""
fi

# === Binding Tests ===
log_info "Binding tests moved to bindings/ruby/run_binding_tests.sh and bindings/python/run_binding_tests.sh"

# === Summary ===
echo ""
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  TEST SUMMARY${NC}"
echo -e "${CYAN}========================================${NC}"
echo -e "Total:  $TESTS_RUN"
echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
echo -e "${RED}Failed: $TESTS_FAILED${NC}"
echo ""

if [ -n "$PERF_RATIO" ]; then
    echo -e "Performance ratio (archive_r/libarchive): ${PERF_RATIO}x"
fi

if [ $TESTS_FAILED -eq 0 ]; then
    log_success "All tests passed!"
    exit 0
else
    log_error "Some tests failed"
    exit 1
fi
