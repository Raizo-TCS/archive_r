#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

# archive_r Build Script
# Usage: ./build.sh [OPTIONS]

set -e

# === Configuration ===
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"

# Detect Python interpreter (prefer /opt/python/<tag> when PYTHON_TAG is given, e.g., manylinux)
PYTHON_EXEC=""
if [[ -n "${PYTHON_TAG:-}" && -x "/opt/python/${PYTHON_TAG}/bin/python" ]]; then
    PYTHON_EXEC="/opt/python/${PYTHON_TAG}/bin/python"
elif command -v python3 >/dev/null 2>&1; then
    PYTHON_EXEC="python3"
elif command -v python >/dev/null 2>&1; then
    PYTHON_EXEC="python"
fi

# Extra pip flags for externally managed Python installations (e.g., Debian PEP 668)
PIP_INSTALL_EXTRA=()
if [ -n "$PYTHON_EXEC" ] && "$PYTHON_EXEC" -m pip help install 2>/dev/null | grep -q -- "--break-system-packages"; then
    PIP_INSTALL_EXTRA+=(--break-system-packages)
fi

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default macOS deployment target to keep wheel compatibility
if [[ "$(uname -s)" == "Darwin" && -z "${MACOSX_DEPLOYMENT_TARGET:-}" ]]; then
    export MACOSX_DEPLOYMENT_TARGET="11.0"
fi

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

pip_install_with_retry() {
    local attempts=${1:-3}
    shift

    local n=1
    while true; do
        if "$PYTHON_EXEC" -m pip install "${PIP_INSTALL_EXTRA[@]}" "$@"; then
            return 0
        fi

        if (( n >= attempts )); then
            log_error "pip install failed after ${attempts} attempts: $*"
            return 1
        fi

        log_warning "pip install failed (attempt ${n}/${attempts}); retrying in 5s..."
        sleep 5
        n=$((n+1))
    done
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
        # Copy LICENSE.txt to bindings/ruby/LICENSE for gemspec inclusion
        cp "$ROOT_DIR/LICENSE.txt" "$ROOT_DIR/bindings/ruby/LICENSE"
    fi

    return 0
}

cleanup_ruby_vendor_sources() {
    local vendor_root="$ROOT_DIR/bindings/ruby/ext/archive_r/vendor/archive_r"
    rm -rf "$vendor_root"
    # Remove temporary LICENSE file
    rm -f "$ROOT_DIR/bindings/ruby/LICENSE"
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
    # CMP0074: find_package() uses <PackageName>_ROOT variables
    CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_DEFAULT_CMP0074=NEW)

    # Detect Windows/MinGW environment and set generator if needed
    if [ -n "$CMAKE_GENERATOR" ]; then
        log_info "Using external CMAKE_GENERATOR: $CMAKE_GENERATOR"
    elif [[ "$(uname -s)" == *"MINGW"* ]] || [[ "$(uname -s)" == *"MSYS"* ]]; then
        if ! command -v make >/dev/null 2>&1 && command -v mingw32-make >/dev/null 2>&1; then
            log_info "Detected MinGW environment with mingw32-make. Using 'MinGW Makefiles' generator."
            CMAKE_ARGS+=(-G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=mingw32-make)
        elif command -v make >/dev/null 2>&1; then
            log_info "Detected MinGW/MSYS environment with make. Using 'Unix Makefiles' generator."
            CMAKE_ARGS+=(-G "Unix Makefiles")
        fi
    fi

    cmake .. "${CMAKE_ARGS[@]}"

    log_info "Building core library..."
    # Use cmake --build for cross-platform compatibility (handles Makefiles, MSVC, Ninja, etc.)
    # Detect CPU count for parallel build
    CPU_COUNT=1
    if command -v nproc >/dev/null 2>&1; then
        CPU_COUNT=$(nproc)
    elif command -v sysctl >/dev/null 2>&1; then
        CPU_COUNT=$(sysctl -n hw.ncpu 2>/dev/null || echo 1)
    fi
    cmake --build . --config Release --parallel "$CPU_COUNT"

    # Check for build artifacts (Unix-style or Windows-style)
    # Windows MSVC builds often place artifacts in Release/ or Debug/ subdirectories
    lib_found=false
    exe_found=false
    lib_path=""
    exe_path=""

    if [ -f "$BUILD_DIR/libarchive_r_core.so" ]; then
        lib_found=true
        lib_path="$BUILD_DIR/libarchive_r_core.so"
    elif [ -f "$BUILD_DIR/libarchive_r_core.dylib" ]; then
        lib_found=true
        lib_path="$BUILD_DIR/libarchive_r_core.dylib"
    elif [ -f "$BUILD_DIR/archive_r_core.dll" ]; then
        lib_found=true
        lib_path="$BUILD_DIR/archive_r_core.dll"
    elif [ -f "$BUILD_DIR/libarchive_r_core.dll" ]; then
        # MinGW produces a lib-prefixed DLL
        lib_found=true
        lib_path="$BUILD_DIR/libarchive_r_core.dll"
    elif [ -f "$BUILD_DIR/libarchive_r_core.a" ]; then
        lib_found=true
        lib_path="$BUILD_DIR/libarchive_r_core.a"
    elif [ -f "$BUILD_DIR/libarchive_r_core.dll.a" ]; then
        # Import library produced alongside the DLL
        lib_found=true
        lib_path="$BUILD_DIR/libarchive_r_core.dll.a"
    elif [ -f "$BUILD_DIR/archive_r_core.lib" ]; then
        lib_found=true
        lib_path="$BUILD_DIR/archive_r_core.lib"
    elif [ -f "$BUILD_DIR/Release/archive_r_core.lib" ]; then
        lib_found=true
        lib_path="$BUILD_DIR/Release/archive_r_core.lib"
    elif [ -f "$BUILD_DIR/Release/libarchive_r_core.dll" ]; then
        lib_found=true
        lib_path="$BUILD_DIR/Release/libarchive_r_core.dll"
    fi

    if [ -f "$BUILD_DIR/find_and_traverse" ]; then
        exe_found=true
        exe_path="$BUILD_DIR/find_and_traverse"
    elif [ -f "$BUILD_DIR/find_and_traverse.exe" ]; then
        exe_found=true
        exe_path="$BUILD_DIR/find_and_traverse.exe"
    elif [ -f "$BUILD_DIR/examples/find_and_traverse.exe" ]; then
        exe_found=true
        exe_path="$BUILD_DIR/examples/find_and_traverse.exe"
    elif [ -f "$BUILD_DIR/examples/find_and_traverse" ]; then
        exe_found=true
        exe_path="$BUILD_DIR/examples/find_and_traverse"
    elif [ -f "$BUILD_DIR/Release/find_and_traverse.exe" ]; then
        exe_found=true
        exe_path="$BUILD_DIR/Release/find_and_traverse.exe"
    elif [ -f "$BUILD_DIR/Release/examples/find_and_traverse.exe" ]; then
        exe_found=true
        exe_path="$BUILD_DIR/Release/examples/find_and_traverse.exe"
    fi

    if [ "$lib_found" = true ] && [ "$exe_found" = true ]; then
        echo ""
        log_success "Core build completed"
        log_success "  Library: $lib_path"
        log_success "  Example: $exe_path"
        echo ""
    else
        log_error "Core build failed - expected files not found"
        log_error "Checked locations:"
        log_error "  $BUILD_DIR/libarchive_r_core.so"
        log_error "  $BUILD_DIR/libarchive_r_core.dylib"
        log_error "  $BUILD_DIR/archive_r_core.dll"
        log_error "  $BUILD_DIR/libarchive_r_core.dll"
        log_error "  $BUILD_DIR/libarchive_r_core.a"
        log_error "  $BUILD_DIR/libarchive_r_core.dll.a"
        log_error "  $BUILD_DIR/archive_r_core.lib"
        log_error "  $BUILD_DIR/Release/archive_r_core.lib"
        log_error "  $BUILD_DIR/Release/libarchive_r_core.dll"
        log_error "  $BUILD_DIR/find_and_traverse"
        log_error "  $BUILD_DIR/find_and_traverse.exe"
        log_error "  $BUILD_DIR/examples/find_and_traverse"
        log_error "  $BUILD_DIR/examples/find_and_traverse.exe"
        log_error "  $BUILD_DIR/Release/find_and_traverse.exe"
        log_error "  $BUILD_DIR/Release/examples/find_and_traverse.exe"
        exit 1
    fi

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
    
    # Run extconf.rb
    local extconf_args=""
    if [ -n "$LIBARCHIVE_ROOT" ]; then
        # Convert Windows path to Unix path if needed (for MinGW/Cygwin)
        # But here we assume LIBARCHIVE_ROOT is a valid path for the environment
        extconf_args="--with-archive-include=$LIBARCHIVE_ROOT/include --with-archive-lib=$LIBARCHIVE_ROOT/lib"
    fi
    
    ruby extconf.rb $extconf_args
    
    # Build
    make

    local built_ext=""
    if [ -f "archive_r.so" ]; then
        built_ext="archive_r.so"
    elif [ -f "archive_r.bundle" ]; then
        built_ext="archive_r.bundle"
    elif [ -f "archive_r.dll" ]; then
        built_ext="archive_r.dll"
    fi

    if [ -z "$built_ext" ]; then
        cd "$ROOT_DIR"
        log_error "Ruby binding build failed"
        return 1
    fi

    local ruby_output_dir="$BUILD_DIR/bindings/ruby"
    mkdir -p "$ruby_output_dir"
    cp "$built_ext" "$ruby_output_dir/"

    log_success "Ruby binding built successfully"
    log_success "  Extension: $ruby_output_dir/$built_ext"

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
    gem_name=$(echo "$gem_name" | tr -d '\r')
    local gem_version=$(ruby -rrubygems -e "spec = Gem::Specification.load('archive_r.gemspec'); puts spec.version" 2>/dev/null || true)
    gem_version=$(echo "$gem_version" | tr -d '\r')

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
        # Use portable find (avoid -printf)
        gem_file=$(find . -maxdepth 1 -type f -name "${gem_name}-*.gem" | sort | tail -n 1)
        gem_file=$(basename "$gem_file")
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
    
    if [ -z "$PYTHON_EXEC" ]; then
        log_warning "Python not found - skipping Python binding"
        return 1
    fi

    # Ensure setuptools/wheel/pybind11 are present for setup.py builds
    if ! "$PYTHON_EXEC" -m pip install "${PIP_INSTALL_EXTRA[@]}" --upgrade pip setuptools wheel pybind11 >/dev/null 2>&1; then
        log_error "Failed to prepare Python build dependencies via pip"
        return 1
    fi
    
    cd "$ROOT_DIR/bindings/python"
    
    # Build extension in-place
    "$PYTHON_EXEC" setup.py build_ext --inplace
    
    if ls *.so 1>/dev/null 2>&1 || ls *.pyd 1>/dev/null 2>&1; then
        log_success "Python binding built successfully"
        log_success "  Extension: $ROOT_DIR/bindings/python/*.{so,pyd}"
        cd "$ROOT_DIR"
        return 0
    else
        cd "$ROOT_DIR"
        log_error "Python binding build failed"
        return 1
    fi
}

create_python_virtualenv() {
    local venv_dir="$1"

    rm -rf "$venv_dir"

    # Helper to check for python/pip in bin (Unix) or Scripts (Windows)
    check_venv_executables() {
        local dir="$1"
        if [ -x "$dir/bin/python" ] && [ -x "$dir/bin/pip" ]; then
            return 0
        fi
        if [ -x "$dir/Scripts/python.exe" ] && [ -x "$dir/Scripts/pip.exe" ]; then
            return 0
        fi
        return 1
    }

    if "$PYTHON_EXEC" -m venv "$venv_dir"; then
        if check_venv_executables "$venv_dir"; then
            return 0
        fi
        log_warning "Virtual environment created without pip; retrying with virtualenv module"
        rm -rf "$venv_dir"
    else
        log_warning "$PYTHON_EXEC -m venv failed. Trying virtualenv module if available..."
    fi

    if "$PYTHON_EXEC" -c "import virtualenv" >/dev/null 2>&1; then
        if "$PYTHON_EXEC" -m virtualenv "$venv_dir"; then
            if check_venv_executables "$venv_dir"; then
                return 0
            fi
            log_warning "virtualenv created directory but executables not found. Listing content:"
            ls -R "$venv_dir" || true
        fi
    else
        log_warning "Python module 'virtualenv' not found. Install it via: $PYTHON_EXEC -m pip install --user --upgrade --break-system-packages virtualenv"
    fi

    log_error "Unable to create virtual environment at $venv_dir. Install python3-venv or the virtualenv package."
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

    if [ -x "$venv_dir/bin/python" ]; then
        venv_python="$venv_dir/bin/python"
        venv_pip="$venv_dir/bin/pip"
    elif [ -x "$venv_dir/Scripts/python.exe" ]; then
        venv_python="$venv_dir/Scripts/python.exe"
        venv_pip="$venv_dir/Scripts/pip.exe"
    else
        log_error "Could not locate python executable in venv"
        return 1
    fi

    "$venv_python" -m pip install --upgrade pip

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

    if [ -z "$PYTHON_EXEC" ]; then
        log_error "Python not found - cannot package Python binding"
        return 1
    fi

    # Ensure build toolchain is available in manylinux images
    local pkg_deps=("pip" "setuptools" "wheel" "build")
    log_info "ARCHIVE_R_SKIP_TWINE_CHECK is set to: '${ARCHIVE_R_SKIP_TWINE_CHECK:-}'"
    if [ "${ARCHIVE_R_SKIP_TWINE_CHECK:-0}" -ne 1 ]; then
        pkg_deps+=("twine")
    else
        log_info "Skipping twine installation"
    fi
    
    log_info "Installing Python build dependencies: ${pkg_deps[*]}"
    if ! pip_install_with_retry 3 --upgrade "${pkg_deps[@]}"; then
        log_error "Failed to prepare Python packaging dependencies via pip"
        return 1
    fi

    local python_root="$ROOT_DIR/bindings/python"
    local dist_dir="$BUILD_DIR/bindings/python/dist"

    rm -rf "$dist_dir"
    mkdir -p "$dist_dir"

    pushd "$python_root" >/dev/null
    rm -rf dist/ build/ *.egg-info
    
    local build_args=("--sdist" "--wheel" "--outdir" "$dist_dir")
    if [ "${ARCHIVE_R_BUILD_NO_ISOLATION:-0}" -eq 1 ]; then
        log_info "Disabling build isolation (ARCHIVE_R_BUILD_NO_ISOLATION=1)"
        build_args+=("--no-isolation")
    fi
    
    log_info "Running python -m build with args: ${build_args[*]}"
    "$PYTHON_EXEC" -m build "${build_args[@]}"
    popd >/dev/null

    dist_artifacts=()
    while IFS= read -r line; do
        dist_artifacts+=("$line")
    done < <(find "$dist_dir" -maxdepth 1 -type f \( -name "*.whl" -o -name "*.tar.gz" \) | sort)
    if [ "${#dist_artifacts[@]}" -eq 0 ]; then
        log_error "No distribution artifacts produced for Python binding"
        return 1
    fi

    if [ "${ARCHIVE_R_SKIP_TWINE_CHECK:-0}" -eq 1 ]; then
        log_info "Skipping twine check (ARCHIVE_R_SKIP_TWINE_CHECK=1)"
    else
        log_info "Running twine check on built artifacts..."
        "$PYTHON_EXEC" -m twine check "${dist_artifacts[@]}"
    fi

    verify_python_package_installation "$dist_dir" || return 1

    log_success "Python packages built and verified: $dist_dir"
    return 0
}

# Build requested bindings
if [ "$BUILD_RUBY" = true ]; then
    build_ruby_binding || log_warning "Ruby binding build had issues"

    if [ "$PACKAGE_RUBY" = true ]; then
        if ! package_ruby_binding; then
            log_error "Ruby gem packaging failed"
            exit 1
        fi
    fi
fi

if [ "$BUILD_PYTHON" = true ]; then
    if [ "$PACKAGE_PYTHON" = true ]; then
        log_info "Skipping in-place Python build because packaging is requested (will test against package)"
    else
        build_python_binding || log_warning "Python binding build had issues"
    fi
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
