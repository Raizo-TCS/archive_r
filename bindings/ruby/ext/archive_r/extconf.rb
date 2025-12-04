# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

require 'mkmf'

def archive_r_core_root
  candidates = []

  env_root = ENV['ARCHIVE_R_CORE_ROOT']
  candidates << File.expand_path(env_root) if env_root && !env_root.empty?

  repo_root = File.expand_path('../../../..', __dir__)
  candidates << repo_root

  vendor_root = File.expand_path('vendor/archive_r', __dir__)
  candidates << vendor_root

  candidates.each do |root|
    next unless root
    include_dir = File.join(root, 'include')
    src_dir = File.join(root, 'src')
    return root if Dir.exist?(include_dir) && Dir.exist?(src_dir)
  end

  nil
end
archive_r_root = archive_r_core_root

unless archive_r_root
  abort <<~MSG
    archive_r core library not found.
    Please set ARCHIVE_R_CORE_ROOT to a repository checkout or use the vendored gem package.
  MSG
end

vendor_root = File.expand_path('vendor/archive_r', __dir__)

if archive_r_root == vendor_root
  puts 'Using vendored archive_r core sources'
elsif ENV['ARCHIVE_R_CORE_ROOT'] && File.expand_path(ENV['ARCHIVE_R_CORE_ROOT']) == archive_r_root
  puts "Using archive_r core from #{archive_r_root} (ARCHIVE_R_CORE_ROOT)"
else
  puts "Using archive_r core from #{archive_r_root}"
end

archive_r_include = File.join(archive_r_root, 'include')
archive_r_src = File.join(archive_r_root, 'src')
archive_r_lib_dir = File.join(archive_r_root, 'build')
glue_source = File.join(__dir__, 'archive_r_ext.cc')

# Ensure make can locate vendored sources via VPATH
$VPATH ||= ''
unless $VPATH.empty?
  $VPATH << File::PATH_SEPARATOR
end
$VPATH << archive_r_src

# Add include paths
$INCFLAGS << " -I#{archive_r_include}"
$INCFLAGS << " -I#{archive_r_src}"

# C++17 standard
$CXXFLAGS << " -std=c++17"

# Configure libarchive paths from environment variables
if ENV['LIBARCHIVE_ROOT']
  root = File.expand_path(ENV['LIBARCHIVE_ROOT'])
  $INCFLAGS << " -I#{File.join(root, 'include')}"
  $LIBPATH.unshift(File.join(root, 'lib'))
end

if ENV['LIBARCHIVE_INCLUDE_DIRS']
  ENV['LIBARCHIVE_INCLUDE_DIRS'].split(File::PATH_SEPARATOR).each do |dir|
    $INCFLAGS << " -I#{dir}"
  end
end

if ENV['LIBARCHIVE_LIBRARY_DIRS']
  ENV['LIBARCHIVE_LIBRARY_DIRS'].split(File::PATH_SEPARATOR).each do |dir|
    $LIBPATH.unshift(dir)
  end
end

# Check for libarchive
unless have_library('archive')
  # Try alternative names for Windows/Static builds
  unless have_library('archive_static') || have_library('libarchive')
    abort "libarchive is required but not found"
  end
end

# Try to link with pre-built static library first
prebuilt_lib = File.join(archive_r_lib_dir, 'libarchive_r_core.a')
prebuilt_lib_win = File.join(archive_r_lib_dir, 'archive_r_core.lib')
prebuilt_lib_win_release = File.join(archive_r_lib_dir, 'Release', 'archive_r_core.lib')

if File.exist?(prebuilt_lib)
  $LOCAL_LIBS << " #{prebuilt_lib}"
  puts "Using pre-built archive_r core library (Unix style)"
elsif File.exist?(prebuilt_lib_win)
  $LOCAL_LIBS << " \"#{prebuilt_lib_win}\""
  puts "Using pre-built archive_r core library (Windows style)"
elsif File.exist?(prebuilt_lib_win_release)
  $LOCAL_LIBS << " \"#{prebuilt_lib_win_release}\""
  puts "Using pre-built archive_r core library (Windows Release style)"
else
  # Build from source as fallback (ensure the Ruby glue source is compiled too)
  puts "Pre-built library not found, will build from source"

  srcs = [glue_source] + Dir.glob(File.join(archive_r_src, '*.cc'))
  $srcs = srcs
end

# Guarantee the Ruby glue source is part of the compilation list when $srcs is set
if defined?($srcs) && $srcs
  $srcs.unshift(glue_source) unless $srcs.include?(glue_source)
end

# Create Makefile
create_makefile('archive_r/archive_r')
