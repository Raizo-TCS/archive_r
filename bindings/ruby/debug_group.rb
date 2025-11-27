require 'json'
require_relative 'lib/archive_r'

test_data_dir = File.expand_path('../../test_data', __dir__)
multi_volume_archive = File.join(test_data_dir, 'multi_volume_test.tar.gz')

entries = []
Archive_r::Traverser.open([multi_volume_archive]) do |trav|
  trav.each do |entry|
    filename = File.basename(entry.path)
    if filename.include?('.part')
      base = filename.sub(/\.part\d+$/, '')
      entry.set_multi_volume_group(base, order: :given)
    end
    entries << { path: entry.path, depth: entry.depth }
  end
end

puts entries.inspect
