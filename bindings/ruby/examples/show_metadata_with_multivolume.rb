#!/usr/bin/env ruby
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

# Example: Display entry metadata (username, groupname) and handle multi-volume archives
#
# This example demonstrates:
# 1. Retrieving metadata (uname, gname) for each entry
# 2. Detecting multi-volume archive patterns (.partNNN, .NNN)
# 3. Automatically setting multi-volume groups for detected patterns
# 4. Traversing into multi-volume archives

require_relative '../lib/archive_r'

def detect_multivolume_pattern(name)
  # Pattern 1: .partNNN (e.g., archive.tar.part001, archive.tar.part002)
  if name =~ /^(.*)\.part\d+$/
    return $1
  end

  # Pattern 2: .NNN (e.g., archive.tar.001, archive.tar.002)
  if name =~ /^(.*)\.\d{2,}$/
    return $1
  end

  nil
end

def show_metadata_with_multivolume(archive_path)
  puts "=== Traversing: #{archive_path} ==="
  puts "Looking for metadata (username/groupname) and multi-volume archives"
  puts
  
  # Request metadata for uname and gname
  options = {
    metadata_keys: ['uname', 'gname']
  }
  
  multivolume_groups = Hash.new { |hash, key| hash[key] = { parts: [], detected: false, configured: false } }
  
  Archive_r::Traverser.open(archive_path, options) do |traverser|
    traverser.each do |entry|
      indent = "  " * (entry.depth)
      type_marker = entry.directory? ? "📁" : "📄" 
      
      # Display basic info
      print "#{indent}#{type_marker} #{entry.path}"
      print " (#{entry.size} bytes)" if entry.file?
      
      info_parts = []
      info_parts << "user: #{entry.metadata['uname']}"
      info_parts << "group: #{entry.metadata['gname']}"
      print " [#{info_parts.join(', ')}]"
      puts
      
      if entry.file? && entry.name.match?(/\.(part\d+|\d{2,})$/)
        raw_base = detect_multivolume_pattern(entry.name)
        next unless raw_base

        group_name = "#{raw_base}.*"
        group = multivolume_groups[group_name]
        group[:parts] << entry.name

        unless group[:detected]
          puts "#{indent}  🔗 Detected multi-volume group: #{group_name}"
          group[:detected] = true
        end

        begin
          entry.set_multi_volume_group(group_name)
          unless group[:configured]
            puts "#{indent}  ✓ Set multi-volume group: #{group_name}"
            group[:configured] = true
          end
        rescue => e
          puts "#{indent}  ⚠ Failed to set multi-volume group: #{e.message}"
        end
      end
    end
  end
  
  # Summary
  puts
  puts "=== Summary ==="
  if multivolume_groups.empty?
    puts "No multi-volume archives detected"
  else
    puts "Multi-volume groups detected: #{multivolume_groups.size}"
    multivolume_groups.each do |base_name, info|
      parts = info[:parts]
      puts "  - #{base_name}: #{parts.size} part(s)"
      parts.sort.each do |part|
        puts "      #{part}"
      end
    end
  end
end

# Main
if ARGV.empty?
  puts "Usage: #{$0} <archive_file>"
  puts
  puts "This script:"
  puts "  1. Shows username/groupname metadata for each entry"
  puts "  2. Detects multi-volume archive patterns (.partNNN, .NNN)"
  puts "  3. Automatically configures multi-volume groups"
  puts "  4. Traverses into multi-volume archives"
  puts
  puts "Example:"
  puts "  #{$0} test_data/stress_test_ultimate.tar.gz"
  exit 1
end

begin
  show_metadata_with_multivolume(ARGV[0])
rescue => e
  puts "Error: #{e.message}"
  puts e.backtrace.join("\n")
  exit 1
end
