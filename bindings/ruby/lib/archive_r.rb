# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

begin
  # Windows/MSVC specific: Ensure dependent DLLs (like libarchive.dll) are found.
  # If LIBARCHIVE_ROOT is set, add its bin directory to the DLL search path.
  if RUBY_PLATFORM =~ /mswin|mingw|cygwin/
    if ENV['LIBARCHIVE_ROOT']
      bin_dir = File.join(ENV['LIBARCHIVE_ROOT'], 'bin')
      if Dir.exist?(bin_dir)
        if defined?(RubyInstaller::Runtime)
          RubyInstaller::Runtime.add_dll_directory(bin_dir)
        else
          ENV['PATH'] = "#{bin_dir};#{ENV['PATH']}"
        end
      end
    end
  end

  # Prefer the packaged gem layout (lib/archive_r/archive_r.so)
  require_relative 'archive_r/archive_r'
rescue LoadError
  begin
    # Fallback to the local development layout (bindings/ruby/archive_r.so)
    require_relative '../archive_r'
  rescue LoadError
    begin
      # Fallback for Windows/MinGW where extension might be directly in lib/ or current dir
      require 'archive_r/archive_r'
    rescue LoadError
      # Last resort: try requiring without directory prefix (common in some Windows setups)
      require 'archive_r'
    end
  end
end

module Archive_r
  VERSION = "0.1.6"
  # Common archive formats excluding libarchive's mtree/raw pseudo formats
  STANDARD_FORMATS = %w[
    7zip ar cab cpio empty iso9660 lha rar tar warc xar zip
  ].freeze

  def self.normalize_options(opts = nil)
    options =
      case opts
      when nil
        {}
      when Hash
        opts.dup
      else
        opts.to_hash.dup
      end

    options[:formats] = STANDARD_FORMATS unless options.key?(:formats)
    options
  end

  def self.traverse(paths, **opts, &block)
    options = normalize_options(opts)

    if block
      Traverser.open(paths, options) { |traverser| traverser.each(&block) }
    else
      Traverser.new(paths, options).each
    end
  end
  
  class Entry
    # Additional helper methods can be added here
    
    def to_s
      path
    end
    
    def inspect
    "#<Archive_r::Entry path=#{path.inspect} size=#{size} depth=#{depth}>"
    end
  end
  
  class Traverser
    # Additional helper methods can be added here
    
    class << self
      alias_method :__archive_r_c_open, :open

      def open(paths, opts = nil, &block)
        __archive_r_c_open(paths, Archive_r.normalize_options(opts), &block)
      end

      def open_hierarchy(hierarchy, opts = nil, &block)
        open([hierarchy], opts, &block)
      end
    end

    alias_method :__archive_r_c_initialize, :initialize

    def initialize(paths, opts = nil)
      __archive_r_c_initialize(paths, Archive_r.normalize_options(opts))
    end

    # Count entries
    def count
      n = 0
      each { |entry| n += 1 }
      n
    end
  end
end
