#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ProbeConfig {
  std::filesystem::path root;
  std::size_t head = 50;
  std::size_t tail = 50;
  bool include_directories = true;
};

void print_usage(const char *argv0) {
  std::cerr << "Usage: " << (argv0 ? argv0 : "fs_iter_order_probe")
            << " <root> [--head N] [--tail N] [--no-dirs]\n";
}

bool parse_size(std::string_view value, std::size_t &out) {
  if (value.empty()) {
    return false;
  }
  std::size_t result = 0;
  for (char ch : value) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    const std::size_t digit = static_cast<std::size_t>(ch - '0');
    result = result * 10 + digit;
  }
  out = result;
  return true;
}

std::string type_prefix(const std::filesystem::directory_entry &entry) {
  std::error_code ec;
  if (entry.is_directory(ec)) {
    return "D";
  }
  if (entry.is_regular_file(ec)) {
    return "F";
  }
  if (entry.is_symlink(ec)) {
    return "L";
  }
  return "O";
}

uint64_t fnv1a64_append(uint64_t hash, std::string_view data) {
  static constexpr uint64_t kOffsetBasis = 14695981039346656037ull;
  static constexpr uint64_t kPrime = 1099511628211ull;
  if (hash == 0) {
    hash = kOffsetBasis;
  }
  for (unsigned char ch : data) {
    hash ^= static_cast<uint64_t>(ch);
    hash *= kPrime;
  }
  return hash;
}

std::string to_hex_u64(uint64_t value) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0') << std::setw(16) << value;
  return oss.str();
}

} // namespace

int main(int argc, char **argv) {
  ProbeConfig cfg;

  if (argc < 2) {
    print_usage(argv[0]);
    return 2;
  }

  cfg.root = argv[1];

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--head") {
      if (i + 1 >= argc) {
        std::cerr << "Error: --head requires N\n";
        return 2;
      }
      std::size_t value = 0;
      if (!parse_size(argv[++i], value)) {
        std::cerr << "Error: invalid --head value\n";
        return 2;
      }
      cfg.head = value;
      continue;
    }
    if (arg == "--tail") {
      if (i + 1 >= argc) {
        std::cerr << "Error: --tail requires N\n";
        return 2;
      }
      std::size_t value = 0;
      if (!parse_size(argv[++i], value)) {
        std::cerr << "Error: invalid --tail value\n";
        return 2;
      }
      cfg.tail = value;
      continue;
    }
    if (arg == "--no-dirs") {
      cfg.include_directories = false;
      continue;
    }

    std::cerr << "Error: unknown arg: " << arg << "\n";
    print_usage(argv[0]);
    return 2;
  }

  std::error_code ec;
  if (!std::filesystem::exists(cfg.root, ec) || ec) {
    std::cerr << "Error: root does not exist: " << cfg.root.string() << "\n";
    return 2;
  }

  std::vector<std::string> items;
  items.reserve(4096);
  uint64_t digest = 0;

  const auto options = std::filesystem::directory_options::skip_permission_denied;
  for (std::filesystem::recursive_directory_iterator it(cfg.root, options, ec), end; it != end; it.increment(ec)) {
    if (ec) {
      ec.clear();
      continue;
    }

    const std::filesystem::directory_entry entry = *it;

    if (!cfg.include_directories) {
      std::error_code is_dir_ec;
      if (entry.is_directory(is_dir_ec) && !is_dir_ec) {
        continue;
      }
    }

    std::filesystem::path rel = entry.path();
    std::error_code rel_ec;
    rel = rel.lexically_relative(cfg.root);
    if (rel.empty()) {
      // Fallback: keep absolute if relative failed.
      rel = entry.path();
    }

    const std::string line = type_prefix(entry) + std::string(" ") + rel.generic_string();
    items.push_back(line);
    digest = fnv1a64_append(digest, line);
    digest = fnv1a64_append(digest, "\n");
  }

  std::cout << "root: " << cfg.root.lexically_normal().generic_string() << "\n";
  std::cout << "count: " << items.size() << "\n";
  std::cout << "fnv1a64: 0x" << to_hex_u64(digest) << "\n";

  const std::size_t head_n = std::min(cfg.head, items.size());
  const std::size_t tail_n = std::min(cfg.tail, items.size());

  std::cout << "--- head(" << head_n << ") ---\n";
  for (std::size_t i = 0; i < head_n; ++i) {
    std::cout << items[i] << "\n";
  }

  std::cout << "--- tail(" << tail_n << ") ---\n";
  for (std::size_t i = items.size() - tail_n; i < items.size(); ++i) {
    std::cout << items[i] << "\n";
  }

  return 0;
}
