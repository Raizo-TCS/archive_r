require 'fileutils'

binding_root = __dir__
project_root = File.expand_path('../..', binding_root)
license_source = File.join(project_root, 'LICENSE.txt')
license_target = File.join(binding_root, 'LICENSE.txt')

if File.exist?(license_source)
  # Ensure the gem bundles the root LICENSE verbatim to keep notices in sync
  unless File.exist?(license_target) && FileUtils.identical?(license_source, license_target)
    FileUtils.cp(license_source, license_target)
  end
else
  warn "[archive_r] WARNING: LICENSE.txt not found at #{license_source}"
end

Gem::Specification.new do |spec|
  spec.name          = "archive_r_ruby"
  spec.version       = "0.1.7"
  spec.authors       = ["raizo.tcs"]
  spec.email         = ["raizo.tcs@users.noreply.github.com"]
  
  spec.summary       = "Ruby bindings for archive_r: libarchive-based streaming traversal for recursive nested archives (no temp files, no large in-memory buffers)"
  spec.description   = "Ruby bindings for archive_r, a libarchive-based library for processing many archive formats. It streams entry data directly from the source to recursively read nested archives without extracting to temporary files or loading large in-memory buffers."
  spec.homepage      = "https://github.com/raizo-tcs/archive_r"
  spec.license       = "MIT"
  
  spec.required_ruby_version = ">= 2.7.0"
  
  spec.files         = Dir["lib/**/*", "ext/**/*", "README.md", "LICENSE.txt"]
  spec.require_paths = ["lib"]
  spec.extensions    = ["ext/archive_r/extconf.rb"]
  
  spec.metadata["homepage_uri"] = spec.homepage
  spec.metadata["source_code_uri"] = spec.homepage
  spec.metadata["bug_tracker_uri"] = "https://github.com/raizo-tcs/archive_r/issues"
  spec.metadata["changelog_uri"] = "https://github.com/raizo-tcs/archive_r/releases"
  
  spec.add_development_dependency "rake", "~> 13.0"
  spec.add_development_dependency "minitest", "~> 5.0"
end
