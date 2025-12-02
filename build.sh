# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

#!/bin/bash
# archive_r Build Script
# Usage: ./build.sh [OPTIONS]

set -e

# === Configuration ===
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

PYTHON_CMD=""
CMAKE_GENERATOR_ARGS=()
CMAKE_PLATFORM_ARGS=()
DEFAULT_BUILD_JOBS=1
CORE_LIBRARY_PATH=""
FIND_AND_TRAVERSE_PATH=""

detect_python_command() {
    if [ -n "${ARCHIVE_R_PYTHON:-}" ]; then
        PYTHON_CMD="$ARCHIVE_R_PYTHON"
        return
    fi

    for candidate in python3 python; do
        if command -v "$candidate" >/dev/null 2>&1; then
            PYTHON_CMD="$candidate"
            return
        fi
    done

    PYTHON_CMD=""
}

detect_parallel_jobs() {
    if [ -n "${ARCHIVE_R_BUILD_JOBS:-}" ]; then
        echo "$ARCHIVE_R_BUILD_JOBS"
        return
    fi

    if command -v nproc >/dev/null 2>&1; then
        nproc
        return
    fi

    if command -v sysctl >/dev/null 2>&1; then
        sysctl -n hw.logicalcpu 2>/dev/null && return
    fi

    if command -v getconf >/dev/null 2>&1; then
        getconf _NPROCESSORS_ONLN 2>/dev/null && return
    fi

    echo 1
}

configure_cmake_generator() {
    if [ -n "${ARCHIVE_R_CMAKE_GENERATOR:-}" ]; then
        CMAKE_GENERATOR_ARGS=("-G" "$ARCHIVE_R_CMAKE_GENERATOR")
        return
    fi

    if [ -n "${CMAKE_GENERATOR:-}" ]; then
        # Respect user-provided CMAKE_GENERATOR without overriding it.
        CMAKE_GENERATOR_ARGS=()
        return
    fi

    if command -v ninja >/dev/null 2>&1; then
        CMAKE_GENERATOR_ARGS=("-G" "Ninja")
        return
    fi

    CMAKE_GENERATOR_ARGS=()
}

configure_cmake_platform() {
    if [ -n "${ARCHIVE_R_CMAKE_GENERATOR_PLATFORM:-}" ]; then
        CMAKE_PLATFORM_ARGS=("-A" "$ARCHIVE_R_CMAKE_GENERATOR_PLATFORM")
        return
    fi

    if [ -n "${CMAKE_GENERATOR_PLATFORM:-}" ]; then
        CMAKE_PLATFORM_ARGS=("-A" "$CMAKE_GENERATOR_PLATFORM")
        return
    fi

    CMAKE_PLATFORM_ARGS=()
}

detect_python_command
DEFAULT_BUILD_JOBS="$(detect_parallel_jobs)"
configure_cmake_generator
configure_cmake_platform

UNAME_S="$(uname -s 2>/dev/null || echo "")"
case "$UNAME_S" in
    MINGW*|MSYS*|CYGWIN*|Windows_NT*)
        IS_WINDOWS_ENV=true
        ;;
    *)
        IS_WINDOWS_ENV=false
        ;;
esac

RIDK_AVAILABLE=false
RIDK_EXEC_CMD=()
if [ "$IS_WINDOWS_ENV" = true ]; then
    if RIDK_PATH="$(command -v ridk 2>/dev/null)"; then
        RIDK_EXEC_CMD=("$RIDK_PATH" "exec")
    elif RIDK_PATH="$(command -v ridk.cmd 2>/dev/null)"; then
        RIDK_EXEC_CMD=("$RIDK_PATH" "exec")
    elif RIDK_PATH="$(command -v ridk.bat 2>/dev/null)"; then
        RIDK_EXEC_CMD=("$RIDK_PATH" "exec")
    fi

    if [ "${#RIDK_EXEC_CMD[@]}" -gt 0 ]; then
        RIDK_AVAILABLE=true
    fi
fi

detect_core_library_path() {
    local candidates=(
        "$BUILD_DIR/libarchive_r_core.a"
        "$BUILD_DIR/libarchive_r_core.lib"
        "$BUILD_DIR/archive_r_core.lib"
        "$BUILD_DIR/Release/libarchive_r_core.lib"
        "$BUILD_DIR/Release/archive_r_core.lib"
    )

    for candidate in "${candidates[@]}"; do
        if [ -f "$candidate" ]; then
            CORE_LIBRARY_PATH="$candidate"
            return 0
        fi
    done

    CORE_LIBRARY_PATH=""
    return 1
}

detect_find_and_traverse_path() {
    local candidates=(
        "$BUILD_DIR/find_and_traverse"
        "$BUILD_DIR/find_and_traverse.exe"
        "$BUILD_DIR/Release/find_and_traverse"
        "$BUILD_DIR/Release/find_and_traverse.exe"
    )

    for candidate in "${candidates[@]}"; do
        if [ -f "$candidate" ]; then
            FIND_AND_TRAVERSE_PATH="$candidate"
            return 0
        fi
    done

    FIND_AND_TRAVERSE_PATH=""
    return 1
}

# === Utility Functions ===
log_info() {
    echo -e "${BLUE}▶${NC} $1"
}

log_success() {
    echo -e "${GREEN}✓${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

log_error() {
    echo -e "${RED}✗${NC} $1"
}

# archive_r Build Script
show_help() {
    cat << HELP
archive_r Build Script

Usage: ./build.sh [OPTIONS]

Options:
    --clean         Remove core build artifacts only
    --clean-all     Remove core build artifacts and binding artifacts
    --rebuild       Clean core artifacts and rebuild the core library
    --rebuild-all   Clean core/binding artifacts and rebuild everything
    --with-ruby     Build Ruby binding after core library
    --with-python   Build Python binding after core library
    --package-ruby  (default) Build Ruby binding and package gem into build/bindings/ruby
    --skip-ruby-package
                    Build Ruby binding without packaging the gem
    --package-python
                    Build Python binding artifacts (wheel + sdist), run twine check, and
                    verify installation inside a temporary virtual environment
    --skip-python-package
                    Disable Python packaging when previously enabled
    --bindings-only Build only bindings (skip core library)
    --python-only   Skip Ruby binding entirely (implies --with-python)
    --help          Show this help message

Examples:
    ./build.sh                    # Normal build (core only)
    ./build.sh --clean            # Remove build artifacts only
    ./build.sh --rebuild          # Clean core and rebuild from scratch
    ./build.sh --with-ruby        # Build core + Ruby binding
    ./build.sh --with-python      # Build core + Python binding
    ./build.sh --rebuild-all      # Full rebuild including bindings

HELP
}

copy_directory_contents() {
    local source="$1"
    local destination="$2"

    if [ ! -d "$source" ]; then
        log_error "Source directory not found: $source"
        return 1
    fi

    rm -rf "$destination"
    mkdir -p "$destination"

    if command -v rsync >/dev/null 2>&1; then
        rsync -a --delete "$source/" "$destination/"
    else
        cp -R "$source/." "$destination/"
    fi

    return 0
}

sync_ruby_vendor_sources() {
    local vendor_root="$ROOT_DIR/bindings/ruby/ext/archive_r/vendor/archive_r"
    local include_src="$ROOT_DIR/include"
    local src_src="$ROOT_DIR/src"

    if [ ! -d "$include_src" ] || [ ! -d "$src_src" ]; then
        log_error "Cannot embed archive_r core sources for Ruby gem (missing include/src directories)"
        return 1
    fi

    log_info "Embedding archive_r core sources for Ruby gem..."

    copy_directory_contents "$include_src" "$vendor_root/include" || return 1
    copy_directory_contents "$src_src" "$vendor_root/src" || return 1

    if [ -f "$ROOT_DIR/LICENSE.txt" ]; then
        mkdir -p "$vendor_root"
        cp "$ROOT_DIR/LICENSE.txt" "$vendor_root/LICENSE.txt"
    fi

    return 0
}

cleanup_ruby_vendor_sources() {
    local vendor_root="$ROOT_DIR/bindings/ruby/ext/archive_r/vendor/archive_r"
    rm -rf "$vendor_root"
}

purge_ruby_binding_artifacts() {
    local ruby_dir="$ROOT_DIR/bindings/ruby"
    local ext_dir="$ruby_dir/ext/archive_r"
    if [ ! -d "$ext_dir" ]; then
        return 0
    fi

    pushd "$ext_dir" >/dev/null
    make clean >/dev/null 2>&1 || true
    rm -f Makefile mkmf.log archive_r.so
    find . -maxdepth 1 -name "*.o" -delete 2>/dev/null || true
    find . -name "*.so" -delete 2>/dev/null || true
    find . -name ".*.time" -delete 2>/dev/null || true
    popd >/dev/null

    return 0
}

## Parse arguments
CLEAN_CORE=false
CLEAN_BINDINGS=false
REBUILD=false
REBUILD_ALL=false
PERFORM_BUILD=true
BUILD_RUBY=false
BUILD_PYTHON=false
BINDINGS_ONLY=false
PACKAGE_RUBY=true
PACKAGE_PYTHON=false
PYTHON_ONLY_MODE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_CORE=true
            CLEAN_BINDINGS=true
            PERFORM_BUILD=false
            shift
            ;;
        --clean-all)
            CLEAN_CORE=true
            CLEAN_BINDINGS=true
            PERFORM_BUILD=false
            shift
            ;;
        --rebuild)
            CLEAN_CORE=true
            REBUILD=true
            PERFORM_BUILD=true
            BINDINGS_ONLY=false
            shift
            ;;
        --rebuild-all)
            CLEAN_CORE=true
            CLEAN_BINDINGS=true
            REBUILD_ALL=true
            PERFORM_BUILD=true
            BUILD_RUBY=true
            BUILD_PYTHON=true
            BINDINGS_ONLY=false
            shift
            ;;
        --with-ruby)
            BUILD_RUBY=true
            shift
            ;;
        --with-python)
            BUILD_PYTHON=true
            shift
            ;;
        --package-ruby)
            BUILD_RUBY=true
            PACKAGE_RUBY=true
            shift
            ;;
        --skip-ruby-package)
            PACKAGE_RUBY=false
            shift
            ;;
        --package-python)
            BUILD_PYTHON=true
            PACKAGE_PYTHON=true
            shift
            ;;
        --skip-python-package)
            PACKAGE_PYTHON=false
            shift
            ;;
        --bindings-only)
            BINDINGS_ONLY=true
            shift
            ;;
        --python-only)
            PYTHON_ONLY_MODE=true
            BUILD_PYTHON=true
            shift
            ;;
        --help)
            show_help
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

if [ "$PYTHON_ONLY_MODE" = true ]; then
    BUILD_RUBY=false
    PACKAGE_RUBY=false
fi

# If user requested clean/clean-all without rebuild, skip build steps entirely
if [ "$PERFORM_BUILD" = false ]; then
    # Still allow bindings-only clean if specified
    BUILD_RUBY=false
    BUILD_PYTHON=false
fi

# === Main Build Process ===
log_info "archive_r Build Starting..."

if [ "$PYTHON_ONLY_MODE" = true ]; then
    log_info "Python-only mode enabled - skipping Ruby binding build and packaging"
fi

# Check for CMake when building core
if [ "$PERFORM_BUILD" = true ] && [ "$BINDINGS_ONLY" = false ]; then
    if ! command -v cmake >/dev/null 2>&1; then
        log_error "CMake not found. Please install CMake."
        exit 1
    fi
fi

# Clean steps
if [ "$CLEAN_CORE" = true ]; then
    log_info "Cleaning core build artifacts..."
    rm -rf "$BUILD_DIR"
    log_success "Core build directory cleaned"
fi

if [ "$CLEAN_BINDINGS" = true ]; then
    log_info "Cleaning Ruby binding artifacts..."
    purge_ruby_binding_artifacts

    log_info "Cleaning Python binding artifacts..."
    if [ -d "$ROOT_DIR/bindings/python" ]; then
        pushd "$ROOT_DIR/bindings/python" >/dev/null
        rm -rf build/ dist/ *.egg-info
        find . -name "*.so" -delete
        find . -name "*.pyc" -delete
        find . -name "__pycache__" -delete
        popd >/dev/null
    fi

    log_success "Binding artifacts cleaned"
fi

# Skip build if this was a clean-only request
if [ "$PERFORM_BUILD" = false ]; then
    log_success "Clean operation completed"
    exit 0
fi

# Build core library (unless --bindings-only)
if [ "$BINDINGS_ONLY" = false ]; then
    mkdir -p "$BUILD_DIR"

    log_info "Configuring with CMake..."
    cd "$BUILD_DIR"
    cmake .. -DCMAKE_BUILD_TYPE=Release "${CMAKE_GENERATOR_ARGS[@]}" "${CMAKE_PLATFORM_ARGS[@]}"

    build_jobs="$DEFAULT_BUILD_JOBS"
    if ! [[ "$build_jobs" =~ ^[0-9]+$ ]] || [ "$build_jobs" -lt 1 ]; then
        build_jobs=1
    fi

    log_info "Building core library (parallel jobs: $build_jobs)..."
    cmake --build . --config Release --parallel "$build_jobs"

    if ! detect_core_library_path; then
        log_error "Core build failed - archive_r_core library not found"
        exit 1
    fi

    if ! detect_find_and_traverse_path; then
        log_error "Core build failed - find_and_traverse example not found"
        exit 1
    fi

    echo ""
    log_success "Core build completed"
    log_success "  Library: $CORE_LIBRARY_PATH"
    log_success "  Example: $FIND_AND_TRAVERSE_PATH"
    echo ""

    cd "$ROOT_DIR"
fi

# === Build Bindings ===

build_ruby_binding() {
    log_info "Building Ruby binding..."
    
    if ! command -v ruby >/dev/null 2>&1; then
        log_warning "Ruby not found - skipping Ruby binding"
        return 1
    fi
    
    local ruby_root="$ROOT_DIR/bindings/ruby"
    local ext_dir="$ruby_root/ext/archive_r"

    if [ ! -d "$ext_dir" ]; then
        log_error "Ruby extension directory not found: $ext_dir"
        return 1
    fi

    cd "$ext_dir"
    
    local ruby_cmd=("ruby")
    local make_cmd=("make")
    if [ "$RIDK_AVAILABLE" = true ]; then
        ruby_cmd=("${RIDK_EXEC_CMD[@]}" "ruby")
        make_cmd=("${RIDK_EXEC_CMD[@]}" "make")
    fi

    # Run extconf.rb
    if ! "${ruby_cmd[@]}" extconf.rb; then
        log_error "ruby extconf.rb failed"
        cd "$ROOT_DIR"
        return 1
    fi
    
    # Build
    if ! "${make_cmd[@]}"; then
        log_error "Ruby extension build failed"
        cd "$ROOT_DIR"
        return 1
    fi

    if [ ! -f "archive_r.so" ]; then
        cd "$ROOT_DIR"
        log_error "Ruby binding build failed"
        return 1
    fi

    local ruby_output_dir="$BUILD_DIR/bindings/ruby"
    mkdir -p "$ruby_output_dir"
    cp "archive_r.so" "$ruby_output_dir/"

    log_success "Ruby binding built successfully"
    log_success "  Extension: $ruby_output_dir/archive_r.so"

    cd "$ROOT_DIR"
    purge_ruby_binding_artifacts
    return 0
}

package_ruby_binding() {
    log_info "Packaging Ruby gem..."

    if ! command -v gem >/dev/null 2>&1; then
        log_error "RubyGems (gem) command not found - cannot package Ruby binding"
        return 1
    fi

    if ! sync_ruby_vendor_sources; then
        return 1
    fi

    pushd "$ROOT_DIR/bindings/ruby" >/dev/null

    if [ ! -f "archive_r.gemspec" ]; then
        popd >/dev/null
        cleanup_ruby_vendor_sources
        log_error "archive_r.gemspec not found"
        return 1
    fi

    local gem_version
    local gem_name=$(ruby -rrubygems -e "spec = Gem::Specification.load('archive_r.gemspec'); puts spec.name" 2>/dev/null || echo "archive_r")
    local gem_version=$(ruby -rrubygems -e "spec = Gem::Specification.load('archive_r.gemspec'); puts spec.version" 2>/dev/null || true)

    rm -f "${gem_name}"-*.gem
    if ! gem build archive_r.gemspec; then
        popd >/dev/null
        cleanup_ruby_vendor_sources
        log_error "gem build failed"
        return 1
    fi

    local gem_file
    if [ -n "$gem_version" ] && [ -f "${gem_name}-${gem_version}.gem" ]; then
        gem_file="${gem_name}-${gem_version}.gem"
    else
        local gem_pattern="${gem_name}-*.gem"
        if compgen -G "$gem_pattern" >/dev/null 2>&1; then
            gem_file=$(compgen -G "$gem_pattern" | sort | tail -n 1)
        else
            gem_file=""
        fi
    fi

    if [ -z "$gem_file" ] || [ ! -f "$gem_file" ]; then
        popd >/dev/null
        cleanup_ruby_vendor_sources
        log_error "Expected gem file $gem_file was not generated"
        return 1
    fi

    local gem_output_dir="$BUILD_DIR/bindings/ruby"
    mkdir -p "$gem_output_dir"
    mv "$gem_file" "$gem_output_dir/"

    popd >/dev/null
    cleanup_ruby_vendor_sources

    log_success "Ruby gem packaged: $gem_output_dir/$gem_file"
    return 0
}

build_python_binding() {
    log_info "Building Python binding..."
    
    if [ -z "$PYTHON_CMD" ]; then
        log_warning "Python interpreter not found - skipping Python binding"
        return 1
    fi
    
    cd "$ROOT_DIR/bindings/python"
    
    # Build extension in-place
    "$PYTHON_CMD" setup.py build_ext --inplace
    
    if compgen -G "*.so" >/dev/null 2>&1 || compgen -G "*.pyd" >/dev/null 2>&1; then
        log_success "Python binding built successfully"
        log_success "  Extension: $ROOT_DIR/bindings/python/*.so|*.pyd"
        cd "$ROOT_DIR"
        return 0
    else
        cd "$ROOT_DIR"
        log_error "Python binding build failed"
        return 1
    fi
}

get_venv_python_path() {
    local venv_dir="$1"
    local candidates=(
        "$venv_dir/bin/python"
        "$venv_dir/bin/python3"
        "$venv_dir/Scripts/python.exe"
        "$venv_dir/Scripts/python"
    )

    for candidate in "${candidates[@]}"; do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

get_venv_pip_path() {
    local venv_dir="$1"
    local candidates=(
        "$venv_dir/bin/pip"
        "$venv_dir/bin/pip3"
        "$venv_dir/Scripts/pip.exe"
        "$venv_dir/Scripts/pip"
    )

    for candidate in "${candidates[@]}"; do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

create_python_virtualenv() {
    local venv_dir="$1"

    rm -rf "$venv_dir"

    if [ -z "$PYTHON_CMD" ]; then
        log_error "Python interpreter not found. Install Python 3.x to use virtual environments."
        return 1
    fi

    if "$PYTHON_CMD" -m venv "$venv_dir"; then
        if get_venv_python_path "$venv_dir" >/dev/null && get_venv_pip_path "$venv_dir" >/dev/null; then
            return 0
        fi
        log_warning "Virtual environment created without pip; retrying with virtualenv module"
        rm -rf "$venv_dir"
    else
        log_warning "${PYTHON_CMD} -m venv failed. Trying virtualenv module if available..."
    fi

    if "$PYTHON_CMD" -c "import virtualenv" >/dev/null 2>&1; then
        if "$PYTHON_CMD" -m virtualenv "$venv_dir"; then
            if get_venv_python_path "$venv_dir" >/dev/null && get_venv_pip_path "$venv_dir" >/dev/null; then
                return 0
            fi
        fi
    else
        log_warning "Python module 'virtualenv' not found. Install it via: ${PYTHON_CMD} -m pip install --user --upgrade virtualenv"
    fi

    log_error "Unable to create virtual environment at $venv_dir. Install python3-venv (or ensure ${PYTHON_CMD} -m venv works)."
    return 1
}

verify_python_package_installation() {
    local dist_dir="$1"
    local venv_dir="$BUILD_DIR/bindings/python/package_test_env"

    log_info "Verifying Python wheel inside temporary virtual environment..."

    if ! create_python_virtualenv "$venv_dir"; then
        return 1
    fi

    local venv_python
    local venv_pip

    if ! venv_python=$(get_venv_python_path "$venv_dir") || ! venv_pip=$(get_venv_pip_path "$venv_dir"); then
        log_error "Unable to locate python/pip executables inside $venv_dir"
        return 1
    fi

    "$venv_pip" install --upgrade pip

    local wheel_path
    wheel_path=$(find "$dist_dir" -maxdepth 1 -type f -name "*.whl" | head -n 1)
    if [ -z "$wheel_path" ]; then
        log_error "No wheel artifact found in $dist_dir"
        return 1
    fi

    "$venv_pip" install "$wheel_path"

    # Run the binding tests using the installed package only.
    ARCHIVE_R_TEST_USE_LOCAL_SOURCE=0 "$venv_python" "$ROOT_DIR/bindings/python/test/test_traverser.py"

    log_success "Python wheel installation verified ($venv_dir)"
    return 0
}

package_python_binding() {
    log_info "Packaging Python binding (wheel + sdist)..."

    if [ -z "$PYTHON_CMD" ]; then
        log_error "Python interpreter not found - cannot package Python binding"
        return 1
    fi

    if ! "$PYTHON_CMD" -c "import build" >/dev/null 2>&1; then
        log_error "Python module 'build' is required. Install it via: ${PYTHON_CMD} -m pip install --upgrade build"
        return 1
    fi

    if ! "$PYTHON_CMD" -c "import twine" >/dev/null 2>&1; then
        log_error "Python module 'twine' is required. Install it via: ${PYTHON_CMD} -m pip install --upgrade twine"
        return 1
    fi

    local python_root="$ROOT_DIR/bindings/python"
    local dist_dir="$BUILD_DIR/bindings/python/dist"

    rm -rf "$dist_dir"
    mkdir -p "$dist_dir"

    pushd "$python_root" >/dev/null
    rm -rf dist/ build/ *.egg-info
    "$PYTHON_CMD" -m build --sdist --wheel --outdir "$dist_dir"
    popd >/dev/null

    dist_artifacts=()
    while IFS= read -r artifact; do
        dist_artifacts+=("$artifact")
    done < <(find "$dist_dir" -maxdepth 1 -type f \( -name "*.whl" -o -name "*.tar.gz" \) | sort)
    if [ "${#dist_artifacts[@]}" -eq 0 ]; then
        log_error "No distribution artifacts produced for Python binding"
        return 1
    fi

    log_info "Running twine check on built artifacts..."
    "$PYTHON_CMD" -m twine check "${dist_artifacts[@]}"

    verify_python_package_installation "$dist_dir" || return 1

    log_success "Python packages built and verified: $dist_dir"
    return 0
}

# Build requested bindings
if [ "$BUILD_RUBY" = true ]; then
    if ! build_ruby_binding; then
        log_error "Ruby binding build failed"
        exit 1
    fi

    if [ "$PACKAGE_RUBY" = true ]; then
        if ! package_ruby_binding; then
            log_error "Ruby gem packaging failed"
            exit 1
        fi
    fi
fi

if [ "$BUILD_PYTHON" = true ]; then
    build_python_binding || log_warning "Python binding build had issues"
fi

if [ "$PACKAGE_PYTHON" = true ]; then
    if ! package_python_binding; then
        log_error "Python packaging workflow failed"
        exit 1
    fi
fi

# Run tests if requested
# Final summary
echo ""
log_success "Build process completed!"
