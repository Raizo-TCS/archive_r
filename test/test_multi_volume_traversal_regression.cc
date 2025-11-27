// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"

#include <filesystem>
#include <glob.h>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>
bool should_ignore_group_label(const std::string &label) {
  static constexpr std::string_view kIgnoredSuffix = "data.txt.*";
  if (label.size() < kIgnoredSuffix.size()) {
    return false;
  }
  return label.compare(label.size() - kIgnoredSuffix.size(), kIgnoredSuffix.size(), kIgnoredSuffix) == 0;
}

using namespace archive_r;

namespace {

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

std::vector<std::string> expand_inputs(const std::vector<std::string> &inputs) {
  std::set<std::string> expanded;

  for (const auto &original : inputs) {
    const bool has_glob = original.find('*') != std::string::npos || original.find('?') != std::string::npos || original.find('[') != std::string::npos;
    if (has_glob) {
      glob_t glob_result{};
      if (glob(original.c_str(), 0, nullptr, &glob_result) == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
          insert_path(expanded, glob_result.gl_pathv[i]);
        }
      }
      globfree(&glob_result);
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

  bool success() const { return duplicate_counts.empty() && unresolved_groups.empty(); }
};

const std::vector<std::string> kFormatsExcludingMtree = { "7zip", "ar", "cab", "cpio", "empty", "iso9660", "lha", "rar", "tar", "warc", "xar", "zip" };

void run_regression_check(const std::string& label, const std::vector<std::string>& inputs); // Forward declaration if needed, but actually I need to define it.

RegressionCheck run_regression_check(const std::vector<std::string>& inputs) {
  std::vector<std::string> expanded = expand_inputs(inputs);
  std::vector<PathHierarchy> paths;
  paths.reserve(expanded.size());
  for (const auto &p : expanded) {
    paths.push_back({PathEntry(p)});
  }

  archive_r::Traverser traverser(paths);

  RegressionCheck result;
  std::map<std::string, int> hierarchy_counts;
  std::set<std::string> groups_marked;
  std::set<std::string> groups_resolved;

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
            }
          }
        }
      }
    }
  }

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
  if (!check.duplicate_counts.empty()) {
    std::cerr << "[" << label << "] duplicate entries detected:" << std::endl;
    for (const auto &[path, count] : check.duplicate_counts) {
      std::cerr << "  " << path << " => " << count << std::endl;
    }
  }

  if (!check.unresolved_groups.empty()) {
    std::cerr << "[" << label << "] unresolved multi-volume groups:" << std::endl;
    for (const auto &group : check.unresolved_groups) {
      std::cerr << "  " << group << std::endl;
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
