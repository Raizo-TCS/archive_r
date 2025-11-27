# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

require 'mkmf'

# Find archive_r core library
archive_r_root = File.expand_path('../../../..', __dir__)
archive_r_include = File.join(archive_r_root, 'include')
archive_r_src = File.join(archive_r_root, 'src')
archive_r_lib = File.join(archive_r_root, 'build')

# Check if core library exists
unless Dir.exist?(archive_r_include)
  abort "archive_r core library not found at #{archive_r_root}"
end

# Add include paths
$INCFLAGS << " -I#{archive_r_include}"
$INCFLAGS << " -I#{archive_r_src}"

# C++17 standard
$CXXFLAGS << " -std=c++17"

# Check for libarchive
unless have_library('archive')
  abort "libarchive is required but not found"
end

# Try to link with pre-built static library first
if File.exist?(File.join(archive_r_lib, 'libarchive_r_experimental2.a'))
  $LOCAL_LIBS << " #{File.join(archive_r_lib, 'libarchive_r_experimental2.a')}"
  puts "Using pre-built archive_r core library"
else
  # Build from source as fallback
  puts "Pre-built library not found, will build from source"
  
  # Add all source files
  srcs = Dir.glob(File.join(archive_r_src, '*.cc'))
  
  srcs.each do |src|
    $srcs << src
  end
end

# Create Makefile
create_makefile('archive_r/archive_r')
