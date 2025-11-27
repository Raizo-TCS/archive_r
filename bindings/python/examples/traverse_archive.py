# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

#!/usr/bin/env python3

import sys
import archive_r
from collections import defaultdict

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} <archive_file>")
    sys.exit(1)

archive_path = sys.argv[1]

print(f"=== Traversing: {archive_path} ===")
print()

# Example 1: Basic iteration
with archive_r.Traverser(archive_path) as traverser:
    for entry in traverser:
        indent = "  " * (entry.depth - 1)
        entry_type = "file" if entry.is_file else "dir"
        print(f"{indent}[depth={entry.depth}] {entry.name} ({entry_type}, {entry.size} bytes)")

print()
print("=== Summary ===")

# Example 2: Counting entries
total_entries = sum(1 for _ in archive_r.Traverser(archive_path))
file_count = sum(1 for e in archive_r.Traverser(archive_path) if e.is_file)
dir_count = sum(1 for e in archive_r.Traverser(archive_path) if e.is_directory)

print(f"Total entries: {total_entries}")
print(f"Files: {file_count}")
print(f"Directories: {dir_count}")

# Example 3: Depth distribution
print()
print("=== Depth distribution ===")
depth_counts = defaultdict(int)
for entry in archive_r.Traverser(archive_path):
    depth_counts[entry.depth] += 1

for depth in sorted(depth_counts.keys()):
    print(f"  Depth {depth}: {depth_counts[depth]} entries")

# Example 4: Large files
print()
print("=== Files larger than 1KB ===")
large_files = [e for e in archive_r.Traverser(archive_path) 
               if e.is_file and e.size > 1024]

for entry in large_files[:5]:
    print(f"  {entry.path} ({entry.size} bytes)")

if len(large_files) > 5:
    print(f"  ... and {len(large_files) - 5} more")

# Example 5: Controlling descent per entry
print()
print("=== Skipping nested archives (first level only) ===")
count = 0
with archive_r.Traverser(archive_path) as traverser:
    for entry in traverser:
        count += 1
        print(f"  {entry.path}")
        # Skip descending into nested archives
        if entry.depth == 1 and entry.is_file:
            entry.set_descent(False)

print(f"Total entries (with descent disabled at depth 1): {count}")
