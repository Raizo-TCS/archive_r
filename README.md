# archive_r

[![CI](https://github.com/Raizo-TCS/archive_r/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Raizo-TCS/archive_r/actions/workflows/ci.yml)

> ⚠️ **Development Status**: This library is currently under development. The API may change without notice.

## Overview

archive_r is a libarchive-based library for processing many archive formats.
It streams entry data directly from the source to recursively read nested archives without extracting to temporary files or loading large in-memory buffers.

### Key Features

- **Nested Archive Support**: Recursively processes archives within archives (including split archives when specified)
- **Password-Protected Archives**: Reads encrypted archives (multiple passphrases supported)
- **Iterator API**: Follows C++ Standard Library idioms
- **Multi-Language Bindings**: Available from Python and Ruby

---

## Platform Support

- **OS**:
  - **Linux**: x86_64, aarch64 (glibc 2.28+, manylinux_2_28)
  - **macOS**: x86_64, arm64 (Universal2, macOS 11.0+)
  - **Windows**: x64 (Windows 10/11, Server 2019+)
- **Compiler**: C++17 or later (GCC 7+, Clang 5+, MSVC 2019+, etc.)
- **Dependencies**:
  - libarchive 3.x (required)

---

## Installation

### Recommended: Build Using build.sh

```bash
cd archive_r
./build.sh
```

Build artifacts will be generated under `archive_r/build/`.

**To build with language bindings**:

```bash
# Include Python bindings
./build.sh --with-python

# Include Ruby bindings
./build.sh --with-ruby

# Include both
./build.sh --with-python --with-ruby

# Full rebuild for Python-only CI workflows (skips Ruby binding steps)
./build.sh --rebuild-all --python-only
```

### For Developers: Individual Binding Builds

- Python workflows (standalone builds, packaging automation, tests, and usage examples) now live in [`bindings/python/README.md`](bindings/python/README.md).
- Ruby workflows remain documented in [`bindings/ruby/README.md`](bindings/ruby/README.md).

---

## Basic Usage Examples

### C++ Iterator API

```cpp
#include "archive_r/traverser.h"
#include <iostream>
#include <string>
#include <vector>

using namespace archive_r;

// Stream search within entry content (buffer boundary aware)
// entry.read(buffer.data(), buffer.size()) returns the number of bytes read (0 for EOF, -1 for error)
bool search_in_entry(Entry& entry, const std::string& keyword) {
    std::string overlap;  // Preserve tail from previous read
    std::vector<char> buffer(8192);
    
    while (true) {
        const ssize_t bytes_read = entry.read(buffer.data(), buffer.size());
        if (bytes_read <= 0) break;  // EOF or error
        
        std::string chunk(buffer.begin(), buffer.begin() + bytes_read);
        std::string search_text = overlap + chunk;
        
        if (search_text.find(keyword) != std::string::npos) {
            return true;
        }
        
        // Preserve tail for next iteration (keyword length - 1)
        if (chunk.size() >= keyword.size() - 1) {
            overlap = chunk.substr(chunk.size() - (keyword.size() - 1));
        } else {
            overlap = chunk;
        }
    }
    
    return false;
}

int main() {
    TraverserOptions options;
    options.formats = {
        "7zip", "ar", "cab", "cpio", "empty", "iso9660",
        "lha", "rar", "tar", "warc", "xar", "zip"
    };  // Exclude libarchive's mtree/raw pseudo formats

    // Wrap single filesystem path into PathHierarchy helper before traversal.
    Traverser traverser({make_single_path("test.zip")}, options);
    
    for (auto it = traverser.begin(); it != traverser.end(); ++it) {
        Entry& entry = *it;

        const std::string full_path = entry.path();
        std::cout << "Path: " << full_path
                  << " (depth=" << entry.depth() << ")\n";
        
        // Search text file content
        if (entry.is_file() && full_path.ends_with(".txt")) {
            if (search_in_entry(entry, "search_keyword")) {
                std::cout << "  Found keyword in: " << full_path << "\n";
            }
        }
    }
    
    return 0;
}
```

> ℹ️ **Entry Path Representation (C++)**
> - `entry.path()` returns a path string including the top-level archive name (e.g., `outer/archive.zip/dir/subdir/file.txt`).
> - `entry.name()` returns the last element of `path_hierarchy()` (e.g., `"dir/subdir/file.txt"`).
> - `entry.path_hierarchy()` returns a `PathHierarchy` (a sequence of `PathEntry` steps). In the common case, each step is a single string (conceptually like `{"outer/archive.zip", "dir/subdir/file.txt"}`), but it can also represent multi-volume and nested grouping.

For Python and Ruby usage guides (installation, API references, practical samples), see the dedicated binding documents:
- Python: [`bindings/python/README.md`](bindings/python/README.md)
- Ruby: [`bindings/ruby/README.md`](bindings/ruby/README.md)

---

## PathHierarchy Concept

### Overview

`PathHierarchy` is the core abstraction representing a path through nested or multi-volume archives.

For convenience, archive_r also provides `Traverser(const std::string& path, ...)` for the common single-root case. `PathHierarchy` remains the underlying representation returned by `Entry::path_hierarchy()` and is useful when you need to explicitly express multi-volume roots or synthetic grouping.

archive_r models each traversal step as a sequence of **path entries**, where each entry can be:

1. **Single-volume entry**: A regular file or directory (e.g., `"archive.tar"`, `"dir/file.txt"`)
2. **Multi-volume entry**: A split archive group (e.g., `{"vol.part1", "vol.part2", "vol.part3"}`)
3. **Nested entry**: A hierarchy within a hierarchy (representing recursive archive structures)

This design enables archive_r to represent complex archive structures uniformly, supporting operations like path comparison, ordering, and display.

### PathEntry Structure

A `PathEntry` is a value type that can hold three forms:

```cpp
// include/archive_r/path_hierarchy.h
class PathEntry {
public:
    struct Parts {
        std::vector<std::string> values;
        enum class Ordering { Natural, Given } ordering = Ordering::Natural;
    };

    using NodeList = std::vector<PathEntry>;

    static PathEntry single(std::string entry);
    static PathEntry multi_volume(std::vector<std::string> entries,
                                  Parts::Ordering ordering = Parts::Ordering::Natural);
    static PathEntry nested(NodeList nodes);

    bool is_single() const;
    bool is_multi_volume() const;
    bool is_nested() const;

    const std::string& single_value() const;
    const Parts& multi_volume_parts() const;
    const NodeList& nested_nodes() const;
};
```

- **Single** (`std::string`): Represents a single path component (e.g., `"archive.zip"`, `"dir/file.txt"`)
- **Multi-volume** (`Parts`): Holds a list of volume paths plus an ordering flag:
  - `Natural` ordering: Sorted by natural numeric ordering (e.g., `["vol.part1", "vol.part10", "vol.part2"]` → `["vol.part1", "vol.part2", "vol.part10"]`)
  - `Given` ordering: Preserves the order specified by the user
- **Nested** (`NodeList`): Nested node-list used for synthetic grouping

### PathHierarchy Type

```cpp
using PathHierarchy = std::vector<PathEntry>;
```

A `PathHierarchy` is a sequence of `PathEntry` elements representing the full path from the root to a target entry. For example:

- `{"archive.tar", "dir/subdir/file.txt"}` — regular nested path
- `{{"vol.part1", "vol.part2"}, "inner.zip", "data.csv"}` — multi-volume archive containing nested archive with CSV file

### Ordering and Comparison

PathHierarchy defines strict ordering rules to enable consistent path comparison:

1. **Type-based ordering**: `Single < Multi-volume < Nested`
2. **Within-type ordering**:
   - **Single**: Lexicographic string comparison
   - **Multi-volume**: First by ordering mode (`Natural < Given`), then lexicographic comparison of part lists
   - **Nested**: Recursive comparison of child hierarchies
3. **Hierarchy comparison**: Compare entries level-by-level until a difference is found

This ordering ensures that archive paths can be sorted, deduplicated, and indexed consistently across all archive types.

### Helper Functions

archive_r provides convenience builders for common cases:

```cpp
// Create a single-entry hierarchy from a filesystem path
PathHierarchy single_path = make_single_path("archive.tar.gz");
// Result: {PathEntry("archive.tar.gz")}

// Create a multi-volume hierarchy from a list of parts
PathHierarchy multi_volume = make_multi_volume_path(
    {"archive.part1", "archive.part2", "archive.part3"},
    PartOrdering::Natural  // or PartOrdering::Given
);
// Result: {PathEntry(Parts{{"archive.part1", "archive.part2", "archive.part3"}, Natural})}
```

When constructing a `Traverser`, wrap top-level paths using these helpers:

```cpp
// Single archive
Traverser tr1({make_single_path("archive.tar")});

// Multiple archives
Traverser tr2({
    make_single_path("first.zip"),
    make_single_path("second.tar.gz")
});

// Multi-volume archive
Traverser tr3({
    make_multi_volume_path({"vol.part1", "vol.part2"}, PartOrdering::Natural)
});
```

### Usage in Entry API

The `Entry` class exposes PathHierarchy through several methods:

- `entry.path_hierarchy()` — Returns the full `PathHierarchy` for the current entry
- `entry.path()` — Flattens the hierarchy into a single string (e.g., `"archive.tar/dir/file.txt"`)
- `entry.name()` — Returns the last component of the hierarchy (e.g., `"file.txt"`)

For custom display formats or deep path analysis, use `path_hierarchy()` directly:

```cpp
PathHierarchy hier = entry.path_hierarchy();
for (const PathEntry& step : hier) {
    if (step.is_single()) {
        std::cout << "Single: " << step.single_value() << "\n";
        continue;
    }
    if (step.is_multi_volume()) {
        const auto& parts = step.multi_volume_parts();
        std::cout << "Multi-volume (" << parts.values.size() << " parts)\n";
        continue;
    }
    std::cout << "Nested node-list\n";
}
```

---

## Behavioral Details

### Automatic Archive Expansion

**By default, all files are attempted to be expanded as archives.** If expansion fails or the format is unsupported, the error is ignored and the file is treated as a regular file.

> 🔧 **Default descent configuration**
> - C++: set `TraverserOptions.descend_archives` (default `true`) before constructing the traverser.
> - Python: pass `descend_archives=True/False` to `archive_r.Traverser`.
> - Ruby: provide the `descend_archives:` keyword to `Archive_r.traverse` / `Archive_r::Traverser.new`.
>   This controls the initial value reported by `entry.descent_enabled()` for every entry.

To suppress automatic expansion for specific entries, call `set_descent(false)`:

```cpp
// C++ example
for (Entry& entry : traverser) {
    // Don't attempt to expand Office files (internally ZIP but expansion unnecessary)
    std::string path = entry.path();
    if (path.ends_with(".docx") || path.ends_with(".xlsx") || path.ends_with(".pptx")) {
        entry.set_descent(false);
    }
}
```

For Python and Ruby examples, see the respective binding documentation:
- Python: [`bindings/python/README.md#controlling-archive-descent`](bindings/python/README.md#controlling-archive-descent)
- Ruby: [`bindings/ruby/README.md`](bindings/ruby/README.md)

> ⚠️ **Reading entry content temporarily disables descent**
> - Calling `Entry::read` (or the binding equivalents) automatically flips `entry.descent_enabled()` to `False` so the partially consumed payload will not be re-opened implicitly.
> - Call `entry.set_descent(True)` if you still want to descend into the entry after streaming its data.

### Retrieving Metadata

Metadata that cannot be retrieved via Entry's fixed API (`size()`, `is_file()`, etc.) can be obtained using `metadata()` or `find_metadata()`.

Specify the metadata keys to capture in advance using `TraverserOptions` (C++) or the `metadata_keys` argument in the bindings:

```cpp
// C++ example
TraverserOptions options;
options.metadata_keys = {"uid", "gid", "mtime"};

// Convert filesystem root into PathHierarchy prior to traversal.
Traverser traverser({make_single_path("test.tar")}, options);
for (Entry& entry : traverser) {
    if (auto* uid = entry.find_metadata("uid")) {
        std::cout << "UID: " << std::get<int64_t>(*uid) << "\n";
    }
}
```

For Python and Ruby examples, see the respective binding documentation:
- Python: [`bindings/python/README.md#metadata-access`](bindings/python/README.md#metadata-access)
- Ruby: [`bindings/ruby/README.md`](bindings/ruby/README.md)

### Specifying Archive Formats

By default, all formats supported by libarchive are enabled. To enable only specific formats, specify `TraverserOptions.formats` (C++) or pass the `formats` keyword argument:

```cpp
// C++ example
TraverserOptions options;
options.formats = {"zip", "tar"};  // Enable only ZIP and TAR

// Each provided root path must be expressed as a PathHierarchy.
Traverser traverser({make_single_path("test.zip")}, options);
```

For Python and Ruby examples, see the respective binding documentation:
- Python: [`bindings/python/README.md#format-specification`](bindings/python/README.md#format-specification)
- Ruby: [`bindings/ruby/README.md`](bindings/ruby/README.md)

### Processing Split Archives

When processing split archive files (`.zip.001`, `.zip.002`, ...), use `set_multi_volume_group()` to register them as the same group.

After the parent archive traversal completes, each group is automatically merged and expanded:

```cpp
// C++ example
for (Entry& entry : traverser) {
    std::string path = entry.path();
    if (path.find(".part") != std::string::npos) {
        // Extract base name from extension (e.g., "archive.zip.part001" → "archive.zip")
        // Implement actual extraction logic based on your extension conventions
        size_t pos = path.rfind(".part");
        std::string base_name = path.substr(0, pos);
        entry.set_multi_volume_group(base_name);
    }
}
```

For Python and Ruby examples, see the respective binding documentation:
- Python: [`bindings/python/README.md#processing-split-archives`](bindings/python/README.md#processing-split-archives)
- Ruby: [`bindings/ruby/README.md`](bindings/ruby/README.md)

---

## Thread Safety

archive_r supports multi-threaded usage with the following constraints:

- **Thread-safe**: Each thread can create and use its own `Traverser` instance independently.
- **Not thread-safe**: A single `Traverser` instance must not be shared across threads.

### Example

```cpp
// ✓ SAFE: Each thread has its own Traverser
std::thread t1([]{ 
    Traverser tr({make_single_path("archive.tar.gz")}); 
    for(Entry& e : tr) { /* process */ } 
});
std::thread t2([]{ 
    Traverser tr({make_single_path("archive.tar.gz")}); 
    for(Entry& e : tr) { /* process */ } 
});

// ✗ UNSAFE: Sharing a single Traverser instance
Traverser shared_tr({make_single_path("archive.tar.gz")});
std::thread t1([&]{ for(Entry& e : shared_tr) { /* process */ } });  // Race condition!
std::thread t2([&]{ for(Entry& e : shared_tr) { /* process */ } });  // Race condition!
```

Internal components (`ArchiveStackOrchestrator`, `Entry`, etc.) inherit the same constraint.

---

## Error Handling

archive_r reports recoverable data errors (corrupted archives, I/O failures) via callbacks. Faults do not stop traversal; you can decide how to react in your callback implementation.

### Exceptions vs Faults

| Situation | Reporting mechanism | Notes |
|---|---|---|
| Invalid `Traverser` arguments (e.g., empty `paths` / empty `PathHierarchy`) | Exception (`std::invalid_argument`) | Thrown during construction |
| Directory traversal errors | Exception (`std::filesystem::filesystem_error`) | Not converted to faults (current behavior) |
| Recoverable archive/data errors during traversal | Fault callback (`EntryFault`) | Traversal continues |
| Entry content read failure | `Entry::read()` returns `-1` and dispatches an `EntryFault` | See `Entry` header docs for details |

### Notes on `Entry`

- Call `set_descent()` / `set_multi_volume_group()` on the `Entry&` inside the traversal loop (before advancing). Copies do not retain traverser-managed control state.
- After a successful `read()` (including EOF), `descent` is disabled until you explicitly re-enable it with `set_descent(true)`.

### Fault Callbacks for Data Errors

Use the library-wide `register_fault_callback` helper (or the binding-level `archive_r.on_fault` / `Archive_r.on_fault`) to receive fault notifications:

```cpp
#include "archive_r/entry_fault.h"

register_fault_callback([](const EntryFault& fault) {
    std::cerr << "Warning at " << hierarchy_display(fault.hierarchy)
                        << ": " << fault.message << std::endl;
    // Traversal continues to next entry
});

Traverser traverser({make_single_path("archive.tar.gz")});
for (Entry& entry : traverser) {
    // Process valid entries; faults are reported via callback
}

// Reset when you no longer need the callback
register_fault_callback({});
```

**EntryFault structure**:
- `hierarchy`: Path where the fault occurred
- `message`: Human-readable description
- `errno_value`: Optional errno from system calls

This design allows processing valid entries even when some are corrupted.

---

## Running Tests

```bash
cd archive_r
./run_tests.sh                # core tests
./bindings/ruby/run_binding_tests.sh
./bindings/python/run_binding_tests.sh
```
Core tests run via `run_tests.sh`; Ruby/Python binding suites live in the dedicated scripts under `bindings/` (these scripts are also called from CI).

---

## License

archive_r is distributed under the MIT License. See the `LICENSE` file for details.

### Third-Party Licenses

This project depends on the following third-party libraries:

- **libarchive**: New BSD License (required at runtime)
- **pybind11**: BSD-style License (required only for building Python bindings)
- **rake**: MIT License (required only for building Ruby bindings)
- **minitest**: MIT License (required only for testing Ruby bindings)

---

## Project Structure

```
archive_r/
├── include/          # C++ header files
├── src/              # C++ implementation
├── bindings/         # Python/Ruby bindings
│   ├── python/
│   └── ruby/
├── test/             # Test code
├── examples/         # Example code
├── docs/             # Documentation
└── build.sh          # Build script
```

---

## Developer Information

### Build Options

```bash
# Build core library only
./build.sh

# Rebuild core library (clean then build)
./build.sh --rebuild

# Rebuild all (core + bindings)
./build.sh --rebuild-all

# Rebuild all artifacts but skip Ruby binding (equivalent to Python-only CI)
./build.sh --rebuild-all --python-only

# Clean core library only
./build.sh --clean

# Clean all (core + bindings)
./build.sh --clean-all

# Build with bindings
./build.sh --with-python --with-ruby
```

---

## CI/CD and Release Workflows

- `ci.yml` runs on Ubuntu 24.04 for every push/PR to `main`, executes `./build.sh --rebuild-all`, then runs `./run_tests.sh` and the Ruby binding tests (`bindings/ruby/run_binding_tests.sh`). Python is verified via the wheel-install check performed during `./build.sh --package-python`.
- `build-wheels.yml` produces manylinux_2_28 wheels for CPython 3.9–3.12 inside Docker, relying on `./build.sh --rebuild-all --python-only` before repairing wheels with `auditwheel`.
- `release.yml` ties everything together: it re-runs the full build, downloads the wheel/SDist artifacts, creates a GitHub Release, and publishes Python packages to PyPI (RubyGems publishing remains optional and requires a token when enabled).

---

## Contributing

Contributions to the project are welcome. Please submit bug reports and feature requests to GitHub Issues.

---

**Note**: This document describes archive_r version 0.1.6.

