require 'json'
require_relative 'lib/archive_r'

test_data_dir = File.expand_path('../../test_data', __dir__)
multi_volume_archive = File.join(test_data_dir, 'multi_volume_test.tar.gz')

parts = []
Archive_r::Traverser.open([multi_volume_archive]) do |trav|
  trav.each do |entry|
    filename = File.basename(entry.path)
    next unless filename.include?('.part')

    formatted_hierarchy = entry.path_hierarchy.map do |component|
      if component.is_a?(Array)
        { multi_volume: component }
      else
        { single: component }
      end
    end

    info = {
      path: entry.path,
      name: entry.name,
      depth: entry.depth,
      hierarchy: formatted_hierarchy
    }
    parts << info
  end
end

puts JSON.pretty_generate(parts)
