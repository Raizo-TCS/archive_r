# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

require 'minitest/autorun'
require 'stringio'
require 'archive_r'

class TestTraverser < Minitest::Test
  DEFAULT_FORMATS = (Archive_r::STANDARD_FORMATS + ['mtree']).freeze

  class PayloadStream < Archive_r::Stream
    def initialize(hierarchy, payload, seekable: false)
      super(hierarchy, seekable: seekable)
      @payload = payload
    end

    def open_part_io(_hierarchy)
      StringIO.new(@payload.dup)
    end
  end

  def setup
    # Assuming test archives are available in ../../test_data/
    @test_data_dir = File.expand_path('../../../test_data', __dir__)
    @simple_archive = File.join(@test_data_dir, 'deeply_nested.tar.gz')
    @multi_volume_archive = File.join(@test_data_dir, 'multi_volume_test.tar.gz')
    @no_uid_archive = File.join(@test_data_dir, 'no_uid.zip')
    @directory_path = File.join(@test_data_dir, 'directory_test')
    @broken_archive = File.join(@test_data_dir, 'broken_nested.tar')
    @test_input_parts = Dir.glob(File.join(@test_data_dir, 'test_input.tar.gz.part*')).sort

    unless Dir.exist?(@directory_path)
      raise Errno::ENOENT, "Test directory not found: #{@directory_path}"
    end
    if @test_input_parts.empty?
      raise Errno::ENOENT, "Multi-volume test input parts not found under #{@test_data_dir}"
    end
    unless File.exist?(@broken_archive)
      raise Errno::ENOENT, "Broken archive not found: #{@broken_archive}"
    end

    Archive_r.register_stream_factory(nil)
    Archive_r.on_fault(nil)
  end

  def teardown
    Archive_r.register_stream_factory(nil)
    Archive_r.on_fault(nil)
  end

  def normalized_options(opts = nil)
    options = opts ? opts.dup : {}
    options[:formats] ||= DEFAULT_FORMATS
    options
  end

  def new_traverser(paths, opts = nil)
    Archive_r::Traverser.new(Array(paths), normalized_options(opts))
  end

  def collect_paths(path)
    new_traverser([path]).map(&:path)
  end

  def open_traverser(paths, opts = nil, &block)
    Archive_r::Traverser.open(Array(paths), normalized_options(opts), &block)
  end
  
  def test_traverser_creation
    traverser = new_traverser([@simple_archive])
    assert_instance_of Archive_r::Traverser, traverser
  end

  def test_traverser_open_hierarchy_helper
    hierarchy = [@simple_archive]
    count = 0
    Archive_r::Traverser.open_hierarchy(hierarchy) { |traverser| traverser.each { count += 1 } }
    assert_operator count, :>, 0
  end

  def test_stream_factory_with_io_object
    expected = collect_paths(@simple_archive)
    payload = File.binread(@simple_archive)
    calls = 0

    Archive_r.register_stream_factory(lambda do |hierarchy|
      calls += 1
      next unless hierarchy.first == File.expand_path(@simple_archive)
      PayloadStream.new(hierarchy, payload)
    end)

    actual = collect_paths(@simple_archive)
    assert_equal expected, actual
    assert_equal 1, calls
  end

  def test_stream_factory_path_remap
    virtual_path = File.join(@test_data_dir, 'virtual_missing.tar.gz')
    expected = collect_paths(@simple_archive).map { |path| path.sub(@simple_archive, virtual_path) }

    Archive_r.register_stream_factory(lambda do |hierarchy|
      if hierarchy.first == File.expand_path(virtual_path)
        PayloadStream.new(hierarchy, File.binread(@simple_archive))
      end
    end)

    actual = collect_paths(virtual_path)
    assert_equal expected, actual
  end

  def test_stream_factory_with_custom_stream_without_seek
    expected = collect_paths(@simple_archive)
    payload = File.binread(@simple_archive)
    calls = 0

    Archive_r.register_stream_factory(lambda do |hierarchy|
      normalized = File.expand_path(@simple_archive)
      next unless hierarchy.first == normalized
      calls += 1
      PayloadStream.new(hierarchy, payload)
    end)

    actual = collect_paths(@simple_archive)
    assert_equal expected, actual
    assert_equal 1, calls
  end

  def test_stream_factory_rejects_plain_io_results
    Archive_r.register_stream_factory(lambda do |_hierarchy|
      StringIO.new('payload')
    end)

    assert_raises(TypeError) { collect_paths(@simple_archive) }
  ensure
    Archive_r.register_stream_factory(nil)
  end

  def test_stream_factory_multi_volume_stream_via_custom_class
    parts = @test_input_parts.map { |path| File.expand_path(path) }
    multi_path = [parts]
    Archive_r.register_stream_factory(nil)
    expected = collect_paths(multi_path)

    stream_class = Class.new(Archive_r::Stream) do
      attr_reader :requests

      def initialize(hierarchy)
        super(hierarchy, seekable: true)
        @requests = []
      end

      def open_part_io(part_hierarchy)
        head = part_hierarchy.first
        @requests << head
        File.open(head, 'rb')
      end
    end

    streams = []
    Archive_r.register_stream_factory(lambda do |hierarchy|
      head = hierarchy.first
      assert_kind_of Array, head
      stream = stream_class.new(hierarchy)
      streams << stream
      stream
    end)

    actual = collect_paths(multi_path)
    assert_equal expected, actual
    refute_empty streams
    assert_equal parts, streams.first.requests
  ensure
    Archive_r.register_stream_factory(nil)
  end
  
  def test_root_entry_exposed
    entry = nil
    open_traverser([@simple_archive]) do |traverser|
      entry = traverser.first
    end

    refute_nil entry
    assert_equal 0, entry.depth
    assert_equal File.basename(@simple_archive), File.basename(entry.path)
  end

  def test_traverser_open_with_block
    entries = []
    open_traverser([@simple_archive]) do |traverser|
      traverser.each do |entry|
        entries << entry.path
      end
    end
    assert entries.size > 0
  end

  def test_archive_r_traverse_with_block_helper
    collected = []
    Archive_r.traverse(@simple_archive, formats: ['tar']) do |entry|
      collected << entry.path
    end

    refute_empty collected
    assert collected.all? { |path| path.is_a?(String) }
  end

  def test_archive_r_traverse_returns_enumerator_without_block
    enumerator = Archive_r.traverse(@simple_archive, formats: ['tar'])
    assert_kind_of Enumerator, enumerator

    paths = []
    enumerator.each { |entry| paths << entry.path }

    refute_empty paths
    assert paths.all? { |path| path.is_a?(String) }
  end

  def test_traverser_with_options
    traverser = new_traverser(
      [@simple_archive],
      passphrases: ['unused-passphrase'],
      formats: ['tar']
    )

    paths = []
    traverser.each { |entry| paths << entry.path }

    refute_empty paths
    assert paths.all? { |p| p.is_a?(String) }

    collected = []
    open_traverser(
      [@simple_archive],
      formats: ['tar']
    ) do |t|
      t.each { |entry| collected << entry.path }
    end

    refute_empty collected
  end

  def test_metadata_selection
    traverser = new_traverser(
      [@simple_archive],
      metadata_keys: ['pathname', 'size']
    )

    entry = traverser.detect { |e| e.depth.positive? }
    refute_nil entry

    metadata = entry.metadata
    assert_kind_of Hash, metadata
    expected_name = expected_entry_name(entry)
    assert_equal expected_name, entry.name
    assert_equal expected_name, metadata['pathname']
    assert_kind_of Integer, metadata['size']
    assert_nil entry.metadata_value('uid')
  end

  def test_metadata_missing_value
    traverser = new_traverser(
      [@no_uid_archive],
      metadata_keys: ['pathname', 'uid']
    )

    entry = traverser.detect { |e| e.depth.positive? }
    refute_nil entry

    metadata = entry.metadata
    expected_name = expected_entry_name(entry)
    assert_equal expected_name, entry.name
    assert_equal expected_name, metadata['pathname']
    refute metadata.key?('uid')
    assert_nil entry.metadata_value('uid')
  end

  def test_multi_volume_grouping
    parts = []
  open_traverser([@multi_volume_archive]) do |traverser|
      traverser.each do |entry|
        filename = File.basename(entry.path)
        parts << entry.path if multi_volume_part?(filename)
      end
    end
    refute_empty parts

    nested_entries = []
  open_traverser([@multi_volume_archive]) do |traverser|
      traverser.each do |entry|
        filename = File.basename(entry.path)
        if multi_volume_part?(filename)
          entry.set_multi_volume_group(
            multi_volume_base(filename),
            order: :given
          )
        end
        nested_entries << entry.path if entry.depth > 1
      end
    end

    refute_empty nested_entries
  end

  def test_multi_volume_grouping_with_wildcard
    parts = []
  open_traverser([@multi_volume_archive]) do |traverser|
      traverser.each do |entry|
        filename = File.basename(entry.path)
        parts << entry.path if multi_volume_part?(filename)
      end
    end
    refute_empty parts

    nested_entries = []
  open_traverser([@multi_volume_archive]) do |traverser|
      traverser.each do |entry|
        filename = File.basename(entry.path)
        if multi_volume_part?(filename)
          entry.set_multi_volume_group("#{multi_volume_base(filename)}.*")
        end
        nested_entries << entry.path if entry.depth > 1
      end
    end

    refute_empty nested_entries
  end

  def test_fault_callback_receives_nested_fault
    faults = []
    Archive_r.on_fault(proc { |fault| faults << fault })

    saw_ok = false
    begin
      open_traverser([@broken_archive]) do |traverser|
        traverser.each do |entry|
          saw_ok ||= entry.name == 'ok.txt'
        end
      end
    ensure
      Archive_r.on_fault(nil)
    end

    assert saw_ok, 'Expected to reach ok.txt even when faults occur'
    refute_empty faults
    assert faults.any? { |fault| fault[:path].include?('corrupt_inner.tar') }
    assert faults.all? { |fault| fault.key?(:message) && fault.key?(:hierarchy) }
  end

  def test_multi_root_traversal
    counts = Hash.new(0)
    total = 0

    simple_norm = normalize_path(@simple_archive)
    dir_norm = normalize_path(@directory_path)

    open_traverser([@simple_archive, @directory_path]) do |traverser|
      traverser.each do |entry|
        hierarchy = entry.path_hierarchy
        refute_nil hierarchy
        refute_empty hierarchy
        head = hierarchy.first
        head_norm = normalize_path(head)

        if head_norm == simple_norm || head_norm.start_with?(simple_norm + '/')
          counts[@simple_archive] += 1
        elsif head_norm == dir_norm || head_norm.start_with?(dir_norm + '/')
          counts[@directory_path] += 1
        else
          flunk("Unexpected root component: #{head} (normalized: #{head_norm})")
        end
        total += 1
      end
    end

    assert_equal 21, total
    assert_equal 11, counts[@simple_archive]
    assert_equal 10, counts[@directory_path]
  end
  
  def test_entry_methods
  open_traverser([@simple_archive]) do |traverser|
      traverser.each do |entry|
        assert_respond_to entry, :path
        assert_respond_to entry, :name
        assert_respond_to entry, :size
        assert_respond_to entry, :depth
        assert_respond_to entry, :file?
        assert_respond_to entry, :directory?
        assert_respond_to entry, :path_hierarchy
        
        # Basic validations
        assert_kind_of String, entry.path
        assert_kind_of String, entry.name
        assert_kind_of Integer, entry.size
        assert_kind_of Integer, entry.depth
        assert [true, false].include?(entry.file?)
        assert [true, false].include?(entry.directory?)

        hierarchy = entry.path_hierarchy
        assert_kind_of Array, hierarchy
        refute_empty hierarchy
        assert hierarchy.all? { |component| component.is_a?(String) }
        assert_equal normalize_path(hierarchy.join('/')), normalize_path(entry.path)
        
        break # Test first entry only
      end
    end
  end

  def test_entry_read_full_payload
    with_sample_file_entry do |entry|
      payload = entry.read
      assert_kind_of String, payload
      assert_equal entry.size, payload.bytesize
      assert_equal '', entry.read
    end
  end

  def test_entry_read_with_length_argument
    with_sample_file_entry(min_size: 64) do |entry|
      chunk_size = [entry.size / 4, 1].max
      collected = +''
      loop do
        chunk = entry.read(chunk_size)
        assert_kind_of String, chunk
        break if chunk.empty?
        assert chunk.bytesize <= chunk_size
        collected << chunk
      end
      assert_equal entry.size, collected.bytesize
    end
  end

  def test_path_hierarchy_components
  open_traverser([@simple_archive]) do |traverser|
      entry = traverser.detect { |e| e.depth > 0 }
      refute_nil entry

      hierarchy = entry.path_hierarchy
      assert_kind_of Array, hierarchy
      assert_operator hierarchy.length, :>, 1
      assert_equal normalize_path(File.expand_path(@simple_archive)), normalize_path(hierarchy.first)
      assert_equal expected_entry_name(entry), hierarchy.last

      assert_equal normalize_path(entry.path), normalize_path(hierarchy.join('/'))
    end
  end
  
  def test_enumerable_methods
    # map - collect paths inside block
  traverser = new_traverser([@simple_archive])
    paths = []
    traverser.each { |e| paths << e.path }
    assert_kind_of Array, paths
    assert paths.all? { |p| p.is_a?(String) }
    
    # count
  traverser = new_traverser([@simple_archive])
    count = 0
    traverser.each { |e| count += 1 }
    assert count > 0
    
    # select - collect file paths inside block
  traverser = new_traverser([@simple_archive])
    file_paths = []
    traverser.each { |e| file_paths << e.path if e.file? }
    assert file_paths.all? { |p| p.is_a?(String) }
    assert file_paths.size > 0
  end
  
  def test_set_descent
    entries = []
  open_traverser([@simple_archive]) do |traverser|
      traverser.each do |entry|
        entries << entry.path
        # Skip descending into the first archive
        entry.set_descent(false) if entries.size == 1
      end
    end
    
    # Should have fewer entries than default descent
    assert entries.size > 0
  end
  
  def test_entry_to_s
    Archive_r::Traverser.open([@simple_archive]) do |traverser|
      entry = traverser.first
      assert_equal entry.path, entry.to_s
    end
  end
  
  def test_entry_inspect
    Archive_r::Traverser.open([@simple_archive]) do |traverser|
      entry = traverser.first
      inspect_str = entry.inspect
      assert_match(/Archive_r::Entry/, inspect_str)
      assert_match(/path=/, inspect_str)
      assert_match(/size=/, inspect_str)
      assert_match(/depth=/, inspect_str)
    end
  end

  private

  def with_sample_file_entry(min_size: 1, max_size: 4096)
    open_traverser([@simple_archive]) do |traverser|
      entry = traverser.detect do |candidate|
        candidate.file? && candidate.depth.positive? &&
          candidate.size >= min_size && candidate.size <= max_size
      end
      refute_nil entry, 'Sample entry for read tests not found'
      yield entry
    end
  end

  def expected_entry_name(entry)
    hierarchy = entry.path_hierarchy
    return '' if hierarchy.nil? || hierarchy.empty?
    hierarchy.last
  end

  def multi_volume_part?(filename)
    idx = filename.rindex('.part')
    return false unless idx
    suffix = filename[(idx + 5)..]
    suffix && !suffix.empty? && suffix.match?(/\A\d+\z/)
  end

  def multi_volume_base(filename)
    idx = filename.rindex('.part')
    idx ? filename[0...idx] : filename
  end

  def normalize_path(path)
    path.to_s.gsub('\\', '/')
  end
end
