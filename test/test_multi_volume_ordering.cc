// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_stack_orchestrator.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace archive_r;

namespace {

std::vector<PathHierarchy> expand_parts(const PathHierarchy &hierarchy) {
  for (std::size_t i = 0; i < hierarchy.size(); ++i) {
    const PathEntry &entry = hierarchy[i];
    if (entry.is_multi_volume()) {
      PathHierarchy prefix_path;
      prefix_path.insert(prefix_path.end(), hierarchy.begin(), hierarchy.begin() + static_cast<PathHierarchy::difference_type>(i));

      std::vector<PathHierarchy> parts;
      parts.reserve(entry.multi_volume_parts().values.size());
      for (const auto &part : entry.multi_volume_parts().values) {
        PathHierarchy part_hierarchy = prefix_path;
        part_hierarchy.emplace_back(PathEntry::single(part));
        parts.push_back(std::move(part_hierarchy));
      }
      return parts;
    }
  }
  return { hierarchy };
}

std::vector<std::string> extract_tail_names(const PathHierarchy &hierarchy) {
  const auto parts = expand_parts(hierarchy);
  if (parts.empty()) {
    throw std::runtime_error("failed to expand multi-volume hierarchy");
  }
  std::vector<std::string> names;
  names.reserve(parts.size());
  for (const auto &part : parts) {
    if (part.empty()) {
      throw std::runtime_error("empty hierarchy encountered");
    }
    const PathEntry &tail = part.back();
    if (!tail.is_single()) {
      throw std::runtime_error("expected single tail entry");
    }
    names.push_back(tail.single_value());
  }
  return names;
}

bool verify_order(const PathHierarchy &hierarchy, const std::vector<std::string> &expected) {
  try {
    return extract_tail_names(hierarchy) == expected;
  } catch (const std::exception &ex) {
    std::cerr << "verification failure: " << ex.what() << std::endl;
    return false;
  }
}

void register_parts(ArchiveStackOrchestrator &orchestrator, const std::vector<std::string> &parts, const std::string &base_name, PathEntry::Parts::Ordering ordering) {
  for (const auto &part : parts) {
    PathHierarchy path = make_single_path(part);
    orchestrator.mark_entry_as_multi_volume(path, base_name, ordering);
  }
}

} // namespace

int main() {
  const std::vector<std::string> given_order = { "test_data/test_input.tar.gz.part00", "test_data/test_input.tar.gz.part01", "test_data/test_input.tar.gz.part02", "test_data/test_input.tar.gz.part03",
                                                 "test_data/test_input.tar.gz.part04" };

  const std::vector<std::string> shuffled_order = { "test_data/test_input.tar.gz.part02", "test_data/test_input.tar.gz.part04", "test_data/test_input.tar.gz.part00",
                                                    "test_data/test_input.tar.gz.part03", "test_data/test_input.tar.gz.part01" };

  const std::string base_name = "test_input.tar.gz";

  // Scenario 1: order => Given preserves insertion
  ArchiveStackOrchestrator given_orchestrator;
  register_parts(given_orchestrator, given_order, base_name, PathEntry::Parts::Ordering::Given);

  if (!given_orchestrator.descend_pending_multi_volumes()) {
    std::cerr << "failed to activate given-order multi-volume group" << std::endl;
    return 1;
  }

  auto *given_archive = dynamic_cast<const StreamArchive *>(given_orchestrator.current_archive());
  if (!given_archive) {
    std::cerr << "expected StreamArchive after activating given-order group" << std::endl;
    return 1;
  }

  if (!verify_order(given_archive->source_hierarchy(), given_order)) {
    std::cerr << "source hierarchy does not preserve given order" << std::endl;
    return 1;
  }

  // Scenario 2: default (Natural) ordering sorts lexicographically
  ArchiveStackOrchestrator natural_orchestrator;
  register_parts(natural_orchestrator, shuffled_order, base_name, PathEntry::Parts::Ordering::Natural);

  if (!natural_orchestrator.descend_pending_multi_volumes()) {
    std::cerr << "failed to activate natural-order multi-volume group" << std::endl;
    return 1;
  }

  auto *natural_archive = dynamic_cast<const StreamArchive *>(natural_orchestrator.current_archive());
  if (!natural_archive) {
    std::cerr << "expected StreamArchive after activating natural-order group" << std::endl;
    return 1;
  }

  std::vector<std::string> expected_sorted = shuffled_order;
  std::sort(expected_sorted.begin(), expected_sorted.end());

  if (!verify_order(natural_archive->source_hierarchy(), expected_sorted)) {
    std::cerr << "source hierarchy did not sort parts under natural ordering" << std::endl;
    return 1;
  }

  // Scenario 3: single part still produces multi-volume hierarchy
  ArchiveStackOrchestrator single_orchestrator;
  std::vector<std::string> single_part = { given_order.front() };
  register_parts(single_orchestrator, single_part, base_name, PathEntry::Parts::Ordering::Given);

  if (!single_orchestrator.descend_pending_multi_volumes()) {
    std::cerr << "failed to activate single-part multi-volume group" << std::endl;
    return 1;
  }

  auto *single_archive = dynamic_cast<const StreamArchive *>(single_orchestrator.current_archive());
  if (!single_archive) {
    std::cerr << "expected StreamArchive after activating single-part group" << std::endl;
    return 1;
  }

  const PathHierarchy &single_source = single_archive->source_hierarchy();
  if (!pathhierarchy_is_multivolume(single_source)) {
    std::cerr << "single part hierarchy should still be marked as multi-volume" << std::endl;
    return 1;
  }

  if (!verify_order(single_source, single_part)) {
    std::cerr << "single part hierarchy did not preserve original entry" << std::endl;
    return 1;
  }

  std::cout << "Multi-volume ordering test passed" << std::endl;
  return 0;
}
