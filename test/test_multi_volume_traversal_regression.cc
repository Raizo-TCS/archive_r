// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"

#include <filesystem>
#if !defined(_WIN32)
#  include <glob.h>
#endif
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <cstdlib>
bool should_ignore_group_label(const std::string &label) {
  static constexpr std::string_view kIgnoredSuffix = "data.txt.*";
  if (label.size() < kIgnoredSuffix.size()) {
    return false;
  }
  return label.compare(label.size() - kIgnoredSuffix.size(), kIgnoredSuffix.size(), kIgnoredSuffix) == 0;
}

using namespace archive_r;

namespace {

thread_local std::vector<archive_r::EntryFault> *g_fault_sink = nullptr;
thread_local bool g_fault_debug_enabled = false;

std::string detect_multi_volume_base(const std::string &name) {
  static const std::regex part_pattern(R"((.+)\.part\d+$)", std::regex::icase);
  static const std::regex numeric_pattern(R"((.+)\.\d{2,}$)", std::regex::icase);

  std::smatch match;
  if (std::regex_match(name, match, part_pattern)) {
    return match[1].str();
  }
  if (std::regex_match(name, match, numeric_pattern)) {
    return match[1].str();
  }
  return {};
}

std::string escape_regex(const std::string &input) {
  std::string escaped;
  escaped.reserve(input.size() * 2);
  for (char ch : input) {
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_') {
      escaped.push_back(ch);
    } else {
      escaped.push_back('\\');
      escaped.push_back(ch);
    }
  }
  return escaped;
}

void insert_path(std::set<std::string> &sink, const std::string &path) {
  if (path.empty()) {
    return;
  }
  std::filesystem::path normalized = std::filesystem::path(path).lexically_normal();
  sink.insert(normalized.string());
}

#if defined(_WIN32)
bool contains_glob_wildcard(const std::string &segment) {
  return segment.find_first_of("*?[") != std::string::npos;
}

std::string glob_segment_to_regex(const std::string &segment) {
  std::string regex = "^";
  bool in_class = false;
  for (size_t i = 0; i < segment.size(); ++i) {
    const char ch = segment[i];
    switch (ch) {
    case '*':
      regex += ".*";
      break;
    case '?':
      regex += '.';
      break;
    case '[':
      in_class = true;
      regex += '[';
      break;
    case ']':
      if (in_class) {
        in_class = false;
        regex += ']';
      } else {
        regex += R"(\])";
      }
      break;
    case '\\':
      regex += R"(\\\\)";
      break;
    default:
      if (!in_class && std::string(".^$+(){}|\\").find(ch) != std::string::npos) {
        regex.push_back('\\');
      }
      regex.push_back(ch);
      break;
    }
  }
  regex += '$';
  return regex;
}

// Minimal glob implementation for Windows CI environments lacking POSIX glob.h
std::vector<std::string> portable_glob(const std::string &pattern) {
  using namespace std::filesystem;

  path pattern_path(pattern);
  path root = pattern_path.root_path();
  path relative = pattern_path.relative_path();

  std::vector<std::string> segments;
  for (const auto &part : relative) {
    segments.push_back(part.string());
  }

  std::vector<path> current;
  if (!root.empty()) {
    current.push_back(root);
  } else {
    current.emplace_back();
  }

  if (segments.empty()) {
    path resolved = root.empty() ? pattern_path : root;
    if (exists(resolved)) {
      return {resolved.lexically_normal().string()};
    }
    return {};
  }

  for (size_t idx = 0; idx < segments.size(); ++idx) {
    const bool last_segment = idx + 1 == segments.size();
    const std::string &segment = segments[idx];
    const bool has_wildcard = contains_glob_wildcard(segment);
    std::vector<path> next;

    for (const auto &base : current) {
      path search_dir = base.empty() ? path(".") : base;

      if (has_wildcard) {
        if (!exists(search_dir) || !is_directory(search_dir)) {
          continue;
        }

        std::regex matcher(glob_segment_to_regex(segment));
        for (const auto &entry : directory_iterator(search_dir)) {
          const std::string name = entry.path().filename().string();
          if (!std::regex_match(name, matcher)) {
            continue;
          }
          if (!last_segment && !entry.is_directory()) {
            continue;
          }
          next.push_back(entry.path());
        }
        continue;
      }

      path candidate = base.empty() ? path(segment) : base / segment;
      if (!exists(candidate)) {
        continue;
      }
      if (!last_segment && !is_directory(candidate)) {
        continue;
      }
      next.push_back(candidate);
    }

    current = std::move(next);
    if (current.empty()) {
      break;
    }
  }

  std::vector<std::string> results;
  for (const auto &match : current) {
    if (match.empty()) {
      continue;
    }
    results.push_back(match.lexically_normal().string());
  }
  return results;
}
#endif // defined(_WIN32)

std::vector<std::string> expand_inputs(const std::vector<std::string> &inputs) {
  std::set<std::string> expanded;

  for (const auto &original : inputs) {
    const bool has_glob = original.find('*') != std::string::npos || original.find('?') != std::string::npos || original.find('[') != std::string::npos;
    if (has_glob) {
#if defined(_WIN32)
      for (const auto &expanded_path : portable_glob(original)) {
        insert_path(expanded, expanded_path);
      }
#else
      glob_t glob_result{};
      if (glob(original.c_str(), 0, nullptr, &glob_result) == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
          insert_path(expanded, glob_result.gl_pathv[i]);
        }
      }
      globfree(&glob_result);
#endif
      continue;
    }

    insert_path(expanded, original);

    std::filesystem::path fs_path(original);
    if (!std::filesystem::exists(fs_path)) {
      continue;
    }

    std::string filename = fs_path.filename().string();
    std::string base = detect_multi_volume_base(filename);
    if (base.empty()) {
      continue;
    }

    std::filesystem::path dir = fs_path.parent_path();
    if (dir.empty()) {
      dir = ".";
    }

    std::regex part_regex("^" + escape_regex(base) + R"((\.part\d+|\.\d{2,})$)", std::regex::icase);
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const std::string candidate = entry.path().filename().string();
      if (std::regex_match(candidate, part_regex)) {
        insert_path(expanded, (dir / candidate).string());
      }
    }
  }

  return { expanded.begin(), expanded.end() };
}

struct RegressionCheck {
  std::map<std::string, int> duplicate_counts;
  std::set<std::string> unresolved_groups;

  std::vector<EntryFault> faults;
  std::map<std::string, int> group_mark_counts;
  std::map<std::string, int> group_resolved_counts;
  std::map<std::string, std::string> group_first_marked_hierarchy;
  std::map<std::string, std::string> group_first_resolved_hierarchy;

  bool success() const { return duplicate_counts.empty() && unresolved_groups.empty(); }
};

const std::vector<std::string> kFormatsExcludingMtree = { "7zip", "ar", "cab", "cpio", "empty", "iso9660", "lha", "rar", "tar", "warc", "xar", "zip" };

void run_regression_check(const std::string& label, const std::vector<std::string>& inputs); // Forward declaration if needed, but actually I need to define it.

RegressionCheck run_regression_check(const std::vector<std::string>& inputs) {
  const bool debug_enabled = [] {
    const char *env = std::getenv("ARCHIVE_R_TEST_DEBUG");
    return env && *env && std::string(env) != "0";
  }();

  static bool callback_registered = false;
  if (!callback_registered) {
    archive_r::register_fault_callback([](const archive_r::EntryFault &fault) {
      if (g_fault_sink) {
        g_fault_sink->push_back(fault);
      }
      if (g_fault_debug_enabled) {
        std::cerr << "[fault] " << fault.message;
        if (!fault.hierarchy.empty()) {
          std::cerr << " @ " << archive_r::hierarchy_display(fault.hierarchy);
        }
        if (fault.errno_value != 0) {
          std::cerr << " (errno=" << fault.errno_value << ")";
        }
        std::cerr << std::endl;
      }
    });
    callback_registered = true;
  }

  std::vector<std::string> expanded = expand_inputs(inputs);
  std::vector<PathHierarchy> paths;
  paths.reserve(expanded.size());
  for (const auto &p : expanded) {
    paths.push_back({PathEntry(p)});
  }

  archive_r::Traverser traverser(paths);

  RegressionCheck result;
  std::vector<archive_r::EntryFault> faults;
  std::map<std::string, int> hierarchy_counts;
  std::set<std::string> groups_marked;
  std::set<std::string> groups_resolved;

  g_fault_sink = &faults;
  g_fault_debug_enabled = debug_enabled;

  for (auto &entry : traverser) {
    std::string hierarchy = entry.path();
    hierarchy_counts[hierarchy]++;

    const auto &hier = entry.path_hierarchy();
    if (!hier.empty()) {
      const auto &last = hier.back();
      if (last.is_single()) {
        std::string name = last.single_value();
        std::string base = detect_multi_volume_base(std::filesystem::path(name).filename().string());
        if (!base.empty()) {
          groups_marked.insert(base);
          result.group_mark_counts[base]++;
          if (result.group_first_marked_hierarchy.find(base) == result.group_first_marked_hierarchy.end()) {
            result.group_first_marked_hierarchy.emplace(base, archive_r::hierarchy_display(entry.path_hierarchy()));
          }
          if (debug_enabled) {
            std::cerr << "[debug] mark multi-volume base='" << base << "' entry='" << hierarchy << "'" << std::endl;
          }
          entry.set_multi_volume_group(base);
        }
      }

      for (const auto &component : hier) {
        if (component.is_multi_volume()) {
          const auto &parts = component.multi_volume_parts().values;
          if (!parts.empty()) {
            std::string base = detect_multi_volume_base(std::filesystem::path(parts[0]).filename().string());
            if (!base.empty()) {
              groups_resolved.insert(base);
              result.group_resolved_counts[base]++;
              if (result.group_first_resolved_hierarchy.find(base) == result.group_first_resolved_hierarchy.end()) {
                result.group_first_resolved_hierarchy.emplace(base, archive_r::hierarchy_display(entry.path_hierarchy()));
              }
              if (debug_enabled) {
                std::cerr << "[debug] resolved multi-volume base='" << base << "' via entry='" << hierarchy << "'" << std::endl;
              }
            }
          }
        }
      }
    }
  }

  g_fault_sink = nullptr;
  g_fault_debug_enabled = false;

  result.faults = std::move(faults);

  for (const auto &[hierarchy, count] : hierarchy_counts) {
    if (count > 1) {
      if (hierarchy.find("deep_nested.txt") != std::string::npos) {
        continue;
      }
      result.duplicate_counts.emplace(hierarchy, count);
    }
  }

  for (const auto &key : groups_marked) {
    if (should_ignore_group_label(key)) {
      continue;
    }
    if (groups_resolved.find(key) == groups_resolved.end()) {
      result.unresolved_groups.insert(key);
    }
  }

  return result;
}

void report_failure(const std::string &label, const RegressionCheck &check) {
  if (!check.faults.empty()) {
    std::cerr << "[" << label << "] faults observed: " << check.faults.size() << std::endl;

    const size_t max_faults = 50;
    const size_t limit = std::min(check.faults.size(), max_faults);
    for (size_t i = 0; i < limit; i++) {
      const auto &fault = check.faults[i];
      std::cerr << "  [fault] " << fault.message;
      if (!fault.hierarchy.empty()) {
        std::cerr << " @ " << archive_r::hierarchy_display(fault.hierarchy);
      }
      if (fault.errno_value != 0) {
        std::cerr << " (errno=" << fault.errno_value << ")";
      }
      std::cerr << std::endl;
    }
    if (check.faults.size() > max_faults) {
      std::cerr << "  ... (" << (check.faults.size() - max_faults) << " more faults; truncated)" << std::endl;
    }
  }

  if (!check.duplicate_counts.empty()) {
    std::cerr << "[" << label << "] duplicate entries detected:" << std::endl;
    for (const auto &[path, count] : check.duplicate_counts) {
      std::cerr << "  " << path << " => " << count << std::endl;
    }
  }

  if (!check.unresolved_groups.empty()) {
    std::cerr << "[" << label << "] unresolved multi-volume groups:" << std::endl;
    for (const auto &group : check.unresolved_groups) {
      const int marked = [&] {
        auto it = check.group_mark_counts.find(group);
        return it == check.group_mark_counts.end() ? 0 : it->second;
      }();
      const int resolved = [&] {
        auto it = check.group_resolved_counts.find(group);
        return it == check.group_resolved_counts.end() ? 0 : it->second;
      }();
      std::cerr << "  " << group << " (marked=" << marked << ", resolved=" << resolved << ")" << std::endl;

      auto it_mark = check.group_first_marked_hierarchy.find(group);
      if (it_mark != check.group_first_marked_hierarchy.end()) {
        std::cerr << "    first_marked_hierarchy: " << it_mark->second << std::endl;
      }

      auto it_res = check.group_first_resolved_hierarchy.find(group);
      if (it_res != check.group_first_resolved_hierarchy.end()) {
        std::cerr << "    first_resolved_hierarchy: " << it_res->second << std::endl;
      }
    }
  }
}

} // namespace

int main() {
  const std::vector<std::string> single_target = { "test_data/stress_test_ultimate.tar.gz" };
  const std::vector<std::string> glob_target = { "test_data/stress_test_ultimate.tar.gz.*" };
  const std::vector<std::string> dir_target = { "test_data/" };

  bool success = true;

  const RegressionCheck single_check = run_regression_check(single_target);
  if (!single_check.success()) {
    report_failure("single", single_check);
    success = false;
  }

  const RegressionCheck glob_check = run_regression_check(glob_target);
  if (!glob_check.success()) {
    report_failure("glob", glob_check);
    success = false;
  }

  const RegressionCheck dir_check = run_regression_check(dir_target);
  if (!dir_check.success()) {
    report_failure("dir", dir_check);
    success = false;
  }

  if (!success) {
    std::cerr << "Multi-volume traversal regression detected" << std::endl;
    return 1;
  }

  std::cout << "Multi-volume traversal regression test passed" << std::endl;
  return 0;
}
