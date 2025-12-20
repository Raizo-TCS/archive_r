# archive_r Python Bindings

> ⚠️ **Development Status**: This library is currently under development. The API may change without notice.

## Overview

Python bindings for archive_r, a libarchive-based library for processing many archive formats.
It streams entry data directly from the source to recursively read nested archives without extracting to temporary files or loading large in-memory buffers.
The bindings expose a Pythonic iterator API with context manager support.

---

## Installation

### From PyPI

```bash
pip install archive_r_python
```

### From Source

```bash
cd archive_r/bindings/python
pip install .
```

### Development Installation (Editable Mode)

```bash
cd archive_r/bindings/python
pip install -e .
```

### Building with Parent Build Script

```bash
cd archive_r
./build.sh --with-python
```

This builds the core library and Python bindings, placing artifacts in `build/bindings/python/`.

---

## Basic Usage

### Simple Traversal

```python
import archive_r

# Context manager ensures proper resource cleanup
with archive_r.Traverser("test.zip") as traverser:
    for entry in traverser:
        print(f"Path: {entry.path} (depth={entry.depth})")
        if entry.is_file:
            print(f"  Size: {entry.size} bytes")
```

### Reading Entry Content

```python
import archive_r

with archive_r.Traverser("archive.tar.gz") as traverser:
    for entry in traverser:
        if entry.is_file and entry.path.endswith('.txt'):
            # Read full content
            content = entry.read()
            print(f"Content of {entry.path}:")
            print(content.decode('utf-8', errors='replace'))
```

### Chunked Reading (Large Files)

```python
import archive_r

with archive_r.Traverser("large_archive.zip") as traverser:
    for entry in traverser:
        if entry.is_file:
            # Read in 8KB chunks
            chunk_size = 8192
            total_bytes = 0
            while True:
                chunk = entry.read(chunk_size)
                if not chunk:
                    break
                total_bytes += len(chunk)
                # Process chunk...
            
            print(f"{entry.path}: {total_bytes} bytes read")
```

### Searching in Entry Content

```python
import archive_r

def search_in_entry(entry, keyword):
    """Stream search within entry content (buffer boundary aware)"""
    overlap = b''
    buffer_size = 8192
    keyword_bytes = keyword.encode('utf-8')
    
    while True:
        chunk = entry.read(buffer_size)
        if not chunk:
            break
        
        search_text = overlap + chunk
        if keyword_bytes in search_text:
            return True
        
        # Preserve tail for next iteration
        if len(chunk) >= len(keyword_bytes) - 1:
            overlap = chunk[-(len(keyword_bytes) - 1):]
        else:
            overlap = chunk
    
    return False

with archive_r.Traverser("documents.zip") as traverser:
    for entry in traverser:
        if entry.is_file and entry.path.endswith('.txt'):
            if search_in_entry(entry, "important"):
                print(f"Found keyword in: {entry.path}")
```

### Controlling Archive Descent

```python
import archive_r

with archive_r.Traverser("test.zip") as traverser:
    for entry in traverser:
        # Don't expand Office files (they are ZIP internally)
        if entry.path.endswith(('.docx', '.xlsx', '.pptx')):
            entry.set_descent(False)
        
        print(f"Path: {entry.path}, Will descend: {entry.descent_enabled}")
```

You can also disable automatic descent globally:

```python
# Disable automatic descent for all entries
with archive_r.Traverser("test.zip", descend_archives=False) as traverser:
    for entry in traverser:
        # Manually enable descent for specific entries
        if entry.path.endswith('.tar.gz'):
            entry.set_descent(True)
```

> ⚠️ **Note**: Reading entry content automatically disables descent. Call `entry.set_descent(True)` if you need to descend after reading.

---

## Path Representation

The Python bindings provide three ways to access entry paths:

```python
with archive_r.Traverser("outer.zip") as traverser:
    for entry in traverser:
        # Full path including top-level archive
        # Example: "outer.zip/inner.tar/file.txt"
        print(f"path: {entry.path}")
        
        # Last element of path_hierarchy
        # Example: "inner.tar/file.txt"
        print(f"name: {entry.name}")
        
        # Path hierarchy as list
        # Example: ["outer.zip", "inner.tar/file.txt"]
        print(f"path_hierarchy: {entry.path_hierarchy}")
```

`path_hierarchy` is particularly useful when you need custom path separators or want to represent the nesting structure explicitly.

---

## Metadata Access

### Basic Metadata

Entry objects provide common metadata through properties:

```python
with archive_r.Traverser("archive.tar") as traverser:
    for entry in traverser:
        print(f"Path: {entry.path}")
        print(f"  Type: {'file' if entry.is_file else 'directory'}")
        print(f"  Size: {entry.size} bytes")
        print(f"  Depth: {entry.depth}")
```

### Extended Metadata

For additional metadata (permissions, ownership, timestamps), specify `metadata_keys`:

```python
with archive_r.Traverser("archive.tar", metadata_keys=["uid", "gid", "mtime", "mode"]) as traverser:
    for entry in traverser:
        # Retrieve all specified metadata as dictionary
        metadata = entry.metadata()
        print(f"{entry.path}:")
        print(f"  UID: {metadata.get('uid')}")
        print(f"  GID: {metadata.get('gid')}")
        print(f"  Mode: {oct(metadata.get('mode', 0))}")
        
        # Or retrieve specific metadata
        mtime = entry.find_metadata("mtime")
        if mtime is not None:
            print(f"  Modified: {mtime}")
```

Available metadata keys depend on the archive format. Common keys include:
- `uid`, `gid`: User/group ID
- `mtime`, `atime`, `ctime`: Timestamps (Unix time)
- `mode`: File permissions
- `uname`, `gname`: User/group names
- `hardlink`, `symlink`: Link targets

---

## Processing Split Archives

For split archive files (e.g., `.zip.001`, `.zip.002`), use `set_multi_volume_group()`:

```python
import archive_r

with archive_r.Traverser("container.tar") as traverser:
    for entry in traverser:
        # Detect split archive parts
        if '.part' in entry.path:
            # Extract base name (e.g., "archive.zip.part001" → "archive.zip")
            pos = entry.path.rfind('.part')
            base_name = entry.path[:pos]
            entry.set_multi_volume_group(base_name)
        
        # After parent traversal, grouped parts are merged and expanded
```

---

## Format Specification

By default, all formats supported by libarchive are enabled. To restrict to specific formats:

```python
# Enable only ZIP and TAR
with archive_r.Traverser("test.zip", formats=["zip", "tar"]) as traverser:
    for entry in traverser:
        print(entry.path)
```

Common format names: `"7zip"`, `"ar"`, `"cab"`, `"cpio"`, `"iso9660"`, `"lha"`, `"rar"`, `"tar"`, `"warc"`, `"xar"`, `"zip"`

> 💡 **Tip**: Exclude pseudo-formats like `"mtree"` and `"raw"` if you encounter false positives on non-archive files.

---

## Custom Stream Factories

You can provide custom stream objects (file-like objects with `read()` method) to override the default file opening behavior:

```python
import archive_r
import io

# Register a custom stream factory
def custom_stream_factory(path):
    """Return a file-like object for the given path"""
    if path == "special_file.bin":
        # Return custom data source
        return io.BytesIO(b"custom content")
    # Return None to use default file opening
    return None

archive_r.register_stream_factory(custom_stream_factory)

with archive_r.Traverser("test.zip") as traverser:
    for entry in traverser:
        # When traverser needs to open "special_file.bin",
        # your factory will provide the BytesIO stream
        pass
```

Stream objects must provide:
- `read(size)`: Read up to `size` bytes
- Optional: `seek(offset, whence)`, `tell()` for seekable streams
- Optional: `rewind()` (defaults to `seek(0, 0)` if not provided)

---

## Error Handling

### Fault Callbacks

Data errors (corrupted archives, I/O failures) are reported via callbacks without stopping traversal:

```python
import archive_r

def fault_handler(fault_info):
    """Called when data errors occur during traversal"""
    print(f"Warning at {fault_info['hierarchy']}: {fault_info['message']}")
    if fault_info.get('errno'):
        print(f"  Error code: {fault_info['errno']}")

archive_r.on_fault(fault_handler)

with archive_r.Traverser("potentially_corrupted.zip") as traverser:
    for entry in traverser:
        # Valid entries are processed normally
        # Corrupted entries trigger fault_handler
        print(entry.path)
```

### Read Errors

Errors during `read()` raise exceptions:

```python
try:
    with archive_r.Traverser("test.zip") as traverser:
        for entry in traverser:
            if entry.is_file:
                content = entry.read()
except RuntimeError as e:
    print(f"Read error: {e}")
```

---

## Thread Safety

The Python bindings follow the same thread safety constraints as the C++ core:

- ✓ **Thread-safe**: Each thread can create and use its own `Traverser` instance independently
- ✗ **Not thread-safe**: A single `Traverser` or `Entry` instance must not be shared across threads

### Example

```python
import threading
import archive_r

# ✓ SAFE: Each thread has its own Traverser
def worker():
    with archive_r.Traverser("archive.tar.gz") as traverser:
        for entry in traverser:
            # Process entry...
            pass

t1 = threading.Thread(target=worker)
t2 = threading.Thread(target=worker)
t1.start()
t2.start()
t1.join()
t2.join()

# ✗ UNSAFE: Sharing a single Traverser instance across threads
shared_traverser = archive_r.Traverser("archive.tar.gz")
def unsafe_worker():
    for entry in shared_traverser:  # Race condition!
        pass

# Don't do this!
# t1 = threading.Thread(target=unsafe_worker)
# t2 = threading.Thread(target=unsafe_worker)
```

Additionally:
- **Global registration functions** (`register_stream_factory`, `on_fault`) should be called during single-threaded initialization
- **Entry objects** should not be shared between threads (they are tied to the Traverser's internal state)

---

## Advanced Examples

### Full Example: Recursive Archive Analyzer

```python
import archive_r
import sys
from collections import defaultdict

def analyze_archive(archive_path):
    """Analyze archive contents and print statistics"""
    stats = defaultdict(int)
    file_types = defaultdict(int)
    
    with archive_r.Traverser(archive_path, metadata_keys=["mtime"]) as traverser:
        for entry in traverser:
            stats['total_entries'] += 1
            
            if entry.is_file:
                stats['files'] += 1
                stats['total_size'] += entry.size
                
                # Count by extension
                if '.' in entry.name:
                    ext = entry.name.rsplit('.', 1)[1]
                    file_types[ext] += 1
                
                # Find largest file
                if entry.size > stats.get('max_file_size', 0):
                    stats['max_file_size'] = entry.size
                    stats['max_file_path'] = entry.path
            else:
                stats['directories'] += 1
            
            # Track maximum depth
            if entry.depth > stats.get('max_depth', 0):
                stats['max_depth'] = entry.depth
    
    # Print results
    print(f"\nArchive Analysis: {archive_path}")
    print(f"  Total entries: {stats['total_entries']}")
    print(f"  Files: {stats['files']}")
    print(f"  Directories: {stats['directories']}")
    print(f"  Total size: {stats['total_size']:,} bytes")
    print(f"  Maximum depth: {stats['max_depth']}")
    
    if 'max_file_path' in stats:
        print(f"  Largest file: {stats['max_file_path']} ({stats['max_file_size']:,} bytes)")
    
    if file_types:
        print("\n  File types:")
        for ext, count in sorted(file_types.items(), key=lambda x: x[1], reverse=True)[:10]:
            print(f"    .{ext}: {count}")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python analyze.py <archive_path>")
        sys.exit(1)
    
    analyze_archive(sys.argv[1])
```

---

## Testing

Run the Python binding tests:

```bash
cd archive_r/bindings/python
python -m unittest discover test
```

Or use the project-wide test runner:

```bash
cd archive_r
./bindings/python/run_binding_tests.sh
```

---

## API Reference

### Module: `archive_r`

#### Class: `Traverser`

Constructor:
```python
Traverser(
    roots,                           # str or list of str/list (path hierarchy)
    formats=None,                    # list of format names (default: all)
    descend_archives=True,           # automatically expand archives
    metadata_keys=None,              # list of metadata keys to capture
    passphrases=None                 # list of passphrases for encrypted archives
)
```

Methods:
- `__iter__()`: Returns self (iterator protocol)
- `__next__()`: Returns next `Entry` or raises `StopIteration`
- `__enter__()`: Context manager entry (returns self)
- `__exit__(exc_type, exc_val, exc_tb)`: Context manager exit

#### Class: `Entry`

Properties:
- `path`: Full path string (read-only)
- `name`: Last element of path hierarchy (read-only)
- `path_hierarchy`: List representation of path (read-only)
- `depth`: Nesting depth (read-only)
- `is_file`: True if entry is a file (read-only)
- `size`: File size in bytes, 0 for directories (read-only)
- `descent_enabled`: Whether this entry will be expanded as an archive (read-only)

Methods:
- `read(size=None)`: Read entry content (bytes). If `size` is omitted, reads all remaining data
- `set_descent(enabled)`: Enable/disable archive expansion for this entry
- `set_multi_volume_group(group_name)`: Register this entry as part of a split archive group
- `metadata()`: Return dictionary of all captured metadata
- `find_metadata(key)`: Return value for specific metadata key, or None if not found

#### Function: `register_stream_factory`

```python
archive_r.register_stream_factory(factory_func)
```

Register a callback to provide custom stream objects for file access.

**Parameters**:
- `factory_func`: Callable that takes a file path (str) and returns a file-like object or None

**Stream object requirements**:
- Must provide `read(size)` method
- Optional: `seek(offset, whence)`, `tell()`, `rewind()`

#### Function: `on_fault`

```python
archive_r.on_fault(callback)
```

Register a callback to receive fault notifications during traversal.

**Parameters**:
- `callback`: Callable that takes a dict with keys:
  - `hierarchy`: List of path components where fault occurred
  - `message`: Human-readable error description
  - `errno`: Optional error number from system calls

---

## Packaging

### Building Wheels

```bash
cd archive_r
./build.sh --package-python
```

This creates wheel (`.whl`) and source distribution (`.tar.gz`) in `build/bindings/python/dist/`.

### Manual Packaging

```bash
cd bindings/python
python setup.py sdist bdist_wheel
```

---

## Requirements

- Python 3.8 or later
- libarchive 3.x (runtime dependency)
- setuptools, wheel (build dependencies)
- pybind11 >= 2.6.0 (build dependency, automatically vendored during packaging)

---

## License

The Python bindings are distributed under the MIT License, consistent with the archive_r core library.

### Third-Party Licenses

- **pybind11**: BSD-style License (used for C++/Python interfacing)
- **libarchive**: New BSD License (runtime dependency)

---

## See Also

- [archive_r Core Documentation](../../README.md)
- [Ruby Bindings](../ruby/README.md)
- [Example Scripts](examples/)

---

**Note**: This document describes archive_r Python bindings version 0.1.8.

