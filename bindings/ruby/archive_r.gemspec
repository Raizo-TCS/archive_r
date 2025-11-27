Gem::Specification.new do |spec|
  spec.name          = "archive_r"
  spec.version       = "0.1.0"
  spec.authors       = ["archive_r Team"]
  spec.email         = [""]
  
  spec.summary       = "Ruby bindings for archive_r library"
  spec.description   = "Fast archive traversal library with support for nested archives and multipart files"
  spec.homepage      = "https://github.com/yourusername/archive_r"
  spec.license       = "MIT"
  
  spec.required_ruby_version = ">= 2.7.0"
  
  spec.files         = Dir["lib/**/*", "ext/**/*", "README.md", "LICENSE"]
  spec.require_paths = ["lib"]
  spec.extensions    = ["ext/archive_r/extconf.rb"]
  
  spec.metadata["homepage_uri"] = spec.homepage
  spec.metadata["source_code_uri"] = spec.homepage
  
  spec.add_development_dependency "rake", "~> 13.0"
  spec.add_development_dependency "minitest", "~> 5.0"
end
