// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"
#include "archive_stack_orchestrator.h"
#include "entry_read_helpers.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace archive_r;

namespace {

std::vector<uint8_t> read_file_bytes(const std::string &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open reference file: " + path);
  }
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool read_fully(ArchiveStackOrchestrator &orchestrator, std::vector<uint8_t> &buffer, size_t expected) {
  size_t total = 0;
  while (total < expected) {
    const ssize_t chunk = orchestrator.read_head(buffer.data() + total, buffer.size() - total);
    if (chunk < 0) {
      return false;
    }
    if (chunk == 0) {
      break;
    }
    total += static_cast<size_t>(chunk);
  }
  if (total != expected) {
    std::cerr << "    expected bytes: " << expected << ", actually read: " << total;
    std::cerr << std::endl;
    buffer.resize(total);
    return false;
  }
  return true;
}

bool test_read_root_archive_file() {
  std::cout << "[1] root archive file read..." << std::endl;

  const std::string archive_path = "test_data/deeply_nested.tar.gz";
  PathHierarchy hierarchy = make_single_path(archive_path);
  ArchiveStackOrchestrator orchestrator;

  const auto expected = read_file_bytes(archive_path);
  std::vector<uint8_t> buffer(expected.size());

  if (!orchestrator.synchronize_to_hierarchy(hierarchy)) {
    std::cerr << "  failed: synchronize_to_hierarchy" << std::endl;
    return false;
  }

  if (!read_fully(orchestrator, buffer, expected.size())) {
    std::cerr << "  failed: short read" << std::endl;
    return false;
  }

  if (buffer != expected) {
    std::cerr << "  failed: data mismatch" << std::endl;
    return false;
  }

  std::vector<uint8_t> scratch(16);
  const ssize_t eof_check = orchestrator.read_head(scratch.data(), scratch.size());
  if (eof_check != 0) {
    std::cerr << "  failed: expected EOF (0) but got " << eof_check << std::endl;
    return false;
  }

  std::cout << "  ok" << std::endl;
  return true;
}

bool test_read_root_multi_volume() {
  std::cout << "[2] root multi-volume read..." << std::endl;

  const std::vector<std::string> parts = { "test_data/test_input.tar.gz.part00", "test_data/test_input.tar.gz.part01", "test_data/test_input.tar.gz.part02", "test_data/test_input.tar.gz.part03",
                                           "test_data/test_input.tar.gz.part04" };

  PathHierarchy multi_entry;
  append_multi_volume(multi_entry, parts);

  ArchiveStackOrchestrator orchestrator;

  if (!orchestrator.synchronize_to_hierarchy(multi_entry)) {
    std::cerr << "  failed: synchronize_to_hierarchy" << std::endl;
    return false;
  }

  std::vector<uint8_t> expected;
  for (const auto &part : parts) {
    const auto chunk = read_file_bytes(part);
    expected.insert(expected.end(), chunk.begin(), chunk.end());
  }

  std::vector<uint8_t> buffer(expected.size());
  if (!read_fully(orchestrator, buffer, expected.size())) {
    std::cerr << "  failed: short read" << std::endl;
    return false;
  }

  if (buffer != expected) {
    std::cerr << "  failed: data mismatch" << std::endl;
    return false;
  }

  std::vector<uint8_t> scratch(16);
  const ssize_t eof_check = orchestrator.read_head(scratch.data(), scratch.size());
  if (eof_check != 0) {
    std::cerr << "  failed: expected EOF (0) but got " << eof_check << std::endl;
    return false;
  }

  std::cout << "  ok" << std::endl;
  return true;
}

bool test_read_nested_entry() {
  std::cout << "[3] nested archive entry read..." << std::endl;

  PathHierarchy root_multi;
  append_multi_volume(root_multi, { "test_data/test_input.tar.gz.part00", "test_data/test_input.tar.gz.part01", "test_data/test_input.tar.gz.part02", "test_data/test_input.tar.gz.part03",
                                    "test_data/test_input.tar.gz.part04" });

  Traverser traverser({ root_multi });
  PathHierarchy target_hierarchy;
  std::vector<uint8_t> expected;

  for (Entry &entry : traverser) {
    if (!entry.is_file()) {
      continue;
    }
    if (entry.name() == "test_input_content.txt") {
      target_hierarchy = entry.path_hierarchy();
      expected = archive_r::test_helpers::read_entry_fully(entry);
      break;
    }
  }

  if (target_hierarchy.empty()) {
    std::cerr << "  failed: target entry not found" << std::endl;
    return false;
  }

  ArchiveStackOrchestrator orchestrator;

  std::vector<uint8_t> buffer(expected.size());
  if (!orchestrator.synchronize_to_hierarchy(target_hierarchy)) {
    std::cerr << "  failed: synchronize_to_hierarchy" << std::endl;
    return false;
  }

  if (!read_fully(orchestrator, buffer, expected.size())) {
    std::cerr << "  failed: short read" << std::endl;
    return false;
  }

  if (buffer != expected) {
    std::cerr << "  failed: data mismatch" << std::endl;
    return false;
  }

  std::vector<uint8_t> scratch(16);
  const ssize_t eof_check = orchestrator.read_head(scratch.data(), scratch.size());
  if (eof_check != 0) {
    std::cerr << "  failed: expected EOF (0) but got " << eof_check << std::endl;
    return false;
  }

  std::cout << "  ok" << std::endl;
  return true;
}

bool test_empty_path_error() {
  std::cout << "[4] empty path error handling..." << std::endl;

  PathHierarchy hierarchy = make_single_path("test_data/deeply_nested.tar.gz");
  ArchiveStackOrchestrator orchestrator;

  PathHierarchy empty;
  if (orchestrator.synchronize_to_hierarchy(empty)) {
    std::cerr << "  failed: synchronize_to_hierarchy unexpectedly succeeded" << std::endl;
    return false;
  }

  std::cout << "  ok" << std::endl;
  return true;
}

bool test_nonexistent_path_reports_error() {
  std::cout << "[5] nonexistent path reports error..." << std::endl;

  PathHierarchy hierarchy = make_single_path("test_data/deeply_nested.tar.gz");
  ArchiveStackOrchestrator orchestrator;

  PathHierarchy missing = hierarchy;
  append_single(missing, "missing.txt");

  if (orchestrator.synchronize_to_hierarchy(missing)) {
    std::cerr << "  failed: synchronize_to_hierarchy unexpectedly succeeded" << std::endl;
    return false;
  }

  std::cout << "  ok" << std::endl;
  return true;
}

bool test_random_access_rewind() {
  std::cout << "[6] random access entry order read..." << std::endl;

  const std::string archive_path = "test_data/nested_with_multi_volume.tar.gz";
  const PathHierarchy root = make_single_path(archive_path);
  Traverser traverser({ root });

  struct Sample {
    PathHierarchy hierarchy;
    std::vector<uint8_t> expected;
  };

  std::vector<Sample> samples;
  for (Entry &entry : traverser) {
    if (!entry.is_file() || entry.depth() != 1) {
      if (entry.depth() >= 1) {
        entry.set_descent(false);
      }
      continue;
    }

    samples.push_back(Sample{ entry.path_hierarchy(), archive_r::test_helpers::read_entry_fully(entry) });
    entry.set_descent(false);

    if (samples.size() == 3) {
      break;
    }
  }

  if (samples.size() < 3) {
    std::cerr << "  failed: insufficient file entries" << std::endl;
    return false;
  }

  ArchiveStackOrchestrator orchestrator;
  const std::vector<size_t> order = { 2, 0, 1 };

  for (size_t idx : order) {
    const auto &sample = samples[idx];
    if (!orchestrator.synchronize_to_hierarchy(sample.hierarchy)) {
      std::cerr << "  failed: synchronize_to_hierarchy for entry index " << idx << " (" << hierarchy_display(sample.hierarchy) << ")" << std::endl;
      return false;
    }

    std::vector<uint8_t> buffer(sample.expected.size());
    if (!read_fully(orchestrator, buffer, sample.expected.size())) {
      std::cerr << "  failed: short read for entry index " << idx << " (" << hierarchy_display(sample.hierarchy) << ")" << std::endl;
      return false;
    }

    if (buffer != sample.expected) {
      std::cerr << "  failed: data mismatch for entry index " << idx << " (" << hierarchy_display(sample.hierarchy) << ")" << std::endl;
      return false;
    }

    std::vector<uint8_t> scratch(8);
    const ssize_t eof_check = orchestrator.read_head(scratch.data(), scratch.size());
    if (eof_check != 0) {
      std::cerr << "  failed: expected EOF (0) but got " << eof_check << " for entry index " << idx << " (" << hierarchy_display(sample.hierarchy) << ")" << std::endl;
      return false;
    }
  }

  std::cout << "  ok" << std::endl;
  return true;
}

} // namespace

int main() {
  std::cout << "=== ArchiveStackOrchestrator::read_head verification ===" << std::endl;

  const bool ok_root = test_read_root_archive_file();
  const bool ok_multi = test_read_root_multi_volume();
  const bool ok_nested = test_read_nested_entry();
  const bool ok_empty = test_empty_path_error();
  const bool ok_missing = test_nonexistent_path_reports_error();
  const bool ok_rewind = test_random_access_rewind();

  const bool all_ok = ok_root && ok_multi && ok_nested && ok_empty && ok_missing && ok_rewind;
  if (!all_ok) {
    std::cerr << "\nArchiveStackOrchestrator::read_head verification FAILED" << std::endl;
    return 1;
  }

  std::cout << "\n✓ All ArchiveStackOrchestrator::read_head tests passed" << std::endl;
  return 0;
}
