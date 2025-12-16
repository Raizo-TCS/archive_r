# archive_r Ruby Binding

Ruby bindings for archive_r, a libarchive-based library for processing many archive formats.
It streams entry data directly from the source to recursively read nested archives without extracting to temporary files or loading large in-memory buffers.

Ruby bindings expose the archive_r traverser API with a natural, block-friendly interface. This document consolidates the Ruby-specific instructions that previously lived in the repository root README.

## Requirements

- Ruby 3.2 (same toolchain as the development environment)
- libarchive 3.x (shared library and headers)
- Build tools: `rake`, `minitest`, a C++17 compiler, and `make`

## Installing the Gem

### Recommended: build.sh workflow

1. From the repository root run:
   ```bash
   ./build.sh --with-ruby
   ```
   Ruby packaging is enabled by default, so the command above builds the core library, compiles the binding, and creates `build/bindings/ruby/archive_r-<version>.gem`.
2. Install the freshly built gem into your desired GEM_HOME:
   ```bash
   gem install --local build/bindings/ruby/archive_r-*.gem
   ```
3. When developing inside this repository the tests automatically install the gem into `build/ruby_gem_home` and set `ARCHIVE_R_CORE_ROOT=build` so the native extension links against the just-built static library.

### Working inside bindings/ruby

If you prefer to build from the `bindings/ruby` directory:

```bash
cd bindings/ruby
bundle install
bundle exec rake compile   # builds ext/archive_r
bundle exec rake test      # runs test/test_traverser.rb
bundle exec rake build     # creates archive_r-<version>.gem locally
```

The `rake test` task compiles the extension, installs it into `lib/`, and executes the Minitest suite.

## Running the repository test suite

From the repository root run `./bindings/ruby/run_binding_tests.sh`. The script prepares a clean GEM_HOME (`build/ruby_gem_home`), installs the gem produced in `build/bindings/ruby`, runs `bindings/ruby/test/test_traverser.rb`, and saves the install log to `build/logs/ruby_gem_install.log`. CI invokes this script after the core tests.

## Usage Example

```ruby
require 'archive_r'

SAFE_FORMATS = Archive_r::STANDARD_FORMATS

def search_in_entry(entry, keyword)
  overlap = ''
  loop do
    chunk = entry.read(8192)
    break if chunk.nil? || chunk.empty?

    search_text = overlap + chunk
    return true if search_text.include?(keyword)

    overlap = if chunk.length >= keyword.length - 1
      chunk[-(keyword.length - 1)..-1]
    else
      chunk
    end
  end
  false
end

Archive_r.traverse('test.zip', formats: SAFE_FORMATS) do |entry|
  puts "#{entry.path} (depth=#{entry.depth})"
  next unless entry.file? && entry.path.end_with?('.txt')
  puts "  Found keyword" if search_in_entry(entry, 'search_keyword')
end

Archive_r.traverse('protected.zip', passphrases: ['password123'], formats: SAFE_FORMATS) do |entry|
  puts entry.path
end
```

## Thread Safety

The Ruby bindings follow the same thread-safety rules as the C++ core library:

**Safe:**
- Each thread creates and uses its own `Traverser` instance
- Concurrent traversal of different archives from different threads

**Unsafe:**
- Sharing a single `Traverser` instance across multiple threads
- Calling `Archive_r.register_stream_factory` or `Archive_r.on_fault` from multiple threads (these modify process-wide global state)

**Summary:** Create one `Traverser` per thread. Do not call global configuration methods (`register_stream_factory`, `on_fault`) concurrently.

## Environment Notes

- Set `ARCHIVE_R_CORE_ROOT` to the repository `build/` directory (or another archive_r build) if the gem needs to link against a pre-built static library instead of compiling the vendored core sources.
- When installing into a sandbox (e.g., CI), configure `GEM_HOME`/`GEM_PATH` before invoking `gem install` so that the extension picks up the correct core library.

## Further Help

- Issue tracker: <https://github.com/raizo-tcs/archive_r/issues>
- Releases / changelog: <https://github.com/raizo-tcs/archive_r/releases>
- For C++/Python usage and overall project information, see the repository root `README.md`.
