# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

#!/usr/bin/env ruby

require_relative '../lib/archive_r'

if ARGV.empty?
  puts "Usage: #{$0} <archive_file>"
  exit 1
end

archive_path = ARGV[0]

puts "=== Traversing: #{archive_path} ==="
puts

# Example 1: Basic iteration
Archive_r::Traverser.open(archive_path) do |traverser|
  traverser.each do |entry|
    indent = "  " * (entry.depth - 1)
    type = entry.file? ? "file" : "dir"
    puts "#{indent}[depth=#{entry.depth}] #{entry.name} (#{type}, #{entry.size} bytes)"
  end
end

puts
puts "=== Summary ==="

# Example 2: Counting entries (must be done inside each block)
total_entries = 0
file_count = 0
dir_count = 0

Archive_r::Traverser.open(archive_path) do |traverser|
  traverser.each do |entry|
    total_entries += 1
    file_count += 1 if entry.file?
    dir_count += 1 if entry.directory?
  end
end

puts "Total entries: #{total_entries}"
puts "Files: #{file_count}"
puts "Directories: #{dir_count}"

# Example 3: Finding entries by depth
puts
puts "=== Depth distribution ==="
depth_counts = Hash.new(0)
Archive_r::Traverser.open(archive_path) do |traverser|
  traverser.each { |entry| depth_counts[entry.depth] += 1 }
end
depth_counts.sort.each do |depth, count|
  puts "  Depth #{depth}: #{count} entries"
end

# Example 4: Filtering large files
puts
puts "=== Files larger than 1KB ==="
large_files = []
Archive_r::Traverser.open(archive_path) do |traverser|
  traverser.each do |entry|
    if entry.file? && entry.size > 1024
      large_files << "#{entry.path} (#{entry.size} bytes)"
    end
  end
end

if large_files.empty?
  puts "  (none)"
else
  large_files.first(5).each { |info| puts "  #{info}" }
  if large_files.size > 5
    puts "  ... and #{large_files.size - 5} more"
  end
end
