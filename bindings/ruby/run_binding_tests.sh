#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BINDING_DIR="$ROOT_DIR/bindings/ruby"
BUILD_DIR="$ROOT_DIR/build"
LOG_DIR="$BUILD_DIR/logs"
RUBY_GEM_HOME="$BUILD_DIR/ruby_gem_home"
RUBY_TEST_ENV=()
RUBY_GEM_INSTALL_LOG="$LOG_DIR/ruby_gem_install.log"

info() { echo "[ruby-binding] $*"; }
warn() { echo "[ruby-binding][warn] $*" >&2; }
error() { echo "[ruby-binding][error] $*" >&2; }

prepare_ruby_gem_env() {
    RUBY_TEST_ENV=()

    if ! command -v ruby >/dev/null 2>&1; then
        warn "Ruby not found - skipping Ruby gem setup"
        return 1
    fi

    if ! command -v gem >/dev/null 2>&1; then
        error "RubyGems (gem) command not found"
        return 1
    fi

    local gemspec="$BINDING_DIR/archive_r.gemspec"
    if [ ! -f "$gemspec" ]; then
        error "Ruby gemspec not found: $gemspec"
        return 1
    fi

    local gem_cache_dir="$BUILD_DIR/bindings/ruby"
    mkdir -p "$gem_cache_dir"

    local gem_name
    gem_name=$(ruby -rrubygems -e "spec = Gem::Specification.load('$gemspec'); puts spec.name" 2>/dev/null || echo "archive_r")
    gem_name=$(echo "$gem_name" | tr -d '\r')

    local gem_file
    gem_file=$(find "$gem_cache_dir" -maxdepth 1 -type f -name "${gem_name}-*.gem" | sort | tail -n 1)
    if [ -z "$gem_file" ]; then
        gem_file=$(find "$gem_cache_dir" -maxdepth 1 -type f -name "${gem_name}*.gem" | sort | tail -n 1)
    fi
    gem_file=$(basename "$gem_file")

    if [ -z "$gem_file" ] || [ ! -f "$gem_cache_dir/$gem_file" ]; then
        error "Ruby gem not found in $gem_cache_dir. Please run ./build.sh --with-ruby first."
        return 1
    fi

    rm -rf "$RUBY_GEM_HOME"
    mkdir -p "$RUBY_GEM_HOME"
    mkdir -p "$LOG_DIR"

    local path_sep
    path_sep=$(ruby -e 'print File::PATH_SEPARATOR')

    local ruby_gem_home_env="$RUBY_GEM_HOME"
    if [ "$path_sep" = ";" ] && command -v cygpath >/dev/null 2>&1; then
        ruby_gem_home_env=$(cygpath -m "$RUBY_GEM_HOME")
    fi

    local ruby_system_paths
    ruby_system_paths="$(ruby -rrubygems -e "puts Gem.path.join('$path_sep')" 2>/dev/null || true)"

    local ruby_gem_path="$ruby_gem_home_env"
    if [ -n "$ruby_system_paths" ]; then
        ruby_gem_path="${ruby_gem_path}${path_sep}${ruby_system_paths}"
    fi

    local install_env=("GEM_HOME=$ruby_gem_home_env" "GEM_PATH=$ruby_gem_path")

    local core_root="$BUILD_DIR"
    if [ "$path_sep" = ";" ] && command -v cygpath >/dev/null 2>&1; then
        core_root=$(cygpath -m "$BUILD_DIR")
    fi

    if [ -f "$BUILD_DIR/libarchive_r_core.a" ] || [ -f "$BUILD_DIR/libarchive_r_core.lib" ] || [ -f "$BUILD_DIR/Release/libarchive_r_core.lib" ]; then
        install_env+=("ARCHIVE_R_CORE_ROOT=$core_root")
    fi

    if [ -n "${LIBARCHIVE_ROOT:-}" ]; then
        install_env+=("LIBARCHIVE_ROOT=$LIBARCHIVE_ROOT")
    fi
    if [ -n "${LIBARCHIVE_INCLUDE_DIRS:-}" ]; then
        install_env+=("LIBARCHIVE_INCLUDE_DIRS=$LIBARCHIVE_INCLUDE_DIRS")
    fi
    if [ -n "${LIBARCHIVE_LIBRARY_DIRS:-}" ]; then
        install_env+=("LIBARCHIVE_LIBRARY_DIRS=$LIBARCHIVE_LIBRARY_DIRS")
    fi

    : > "$RUBY_GEM_INSTALL_LOG"
    info "Installing Ruby gem from $gem_cache_dir/$gem_file (log: $RUBY_GEM_INSTALL_LOG)"

    if ! env "${install_env[@]}" \
        gem install --local --no-document --install-dir "$RUBY_GEM_HOME" "$gem_cache_dir/$gem_file" \
        2>&1 | tee "$RUBY_GEM_INSTALL_LOG"; then
        error "Failed to install Ruby gem for tests"
        return 1
    fi

    RUBY_TEST_ENV=("GEM_HOME=$ruby_gem_home_env" "GEM_PATH=$ruby_gem_path")
    return 0
}

if [ ! -f "$BINDING_DIR/archive_r.gemspec" ]; then
    info "Ruby binding sources not found - skipping Ruby tests"
    exit 0
fi

if ! prepare_ruby_gem_env; then
    info "Ruby gem setup unavailable - skipping Ruby tests"
    exit 0
fi

pushd "$BINDING_DIR" >/dev/null
if env "${RUBY_TEST_ENV[@]}" ruby test/test_traverser.rb > "$LOG_DIR/ruby_test.log" 2>&1; then
    info "Ruby binding tests passed"
else
    error "Ruby binding tests failed"
    echo "--- Ruby Test Output ---"
    cat "$LOG_DIR/ruby_test.log"
    echo "------------------------"
    popd >/dev/null
    exit 1
fi
popd >/dev/null
info "Ruby binding tests completed"
