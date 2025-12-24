// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/multi_volume_stream_base.h"

#include "archive_r/path_hierarchy_utils.h"

#include <iostream>
#include <memory>

namespace archive_r {
namespace {

struct DummyStream : public MultiVolumeStreamBase {
  explicit DummyStream(PathHierarchy logical_path)
      : MultiVolumeStreamBase(std::move(logical_path), false) {}

  using MultiVolumeStreamBase::seek_within_single_part;
  using MultiVolumeStreamBase::size_of_single_part;

  void rewind() override { MultiVolumeStreamBase::rewind(); }

private:
  void open_single_part(const PathHierarchy &single_part) override { (void)single_part; }
  void close_single_part() override {}
  ssize_t read_from_single_part(void *buffer, size_t size) override {
    (void)buffer;
    (void)size;
    return 0;
  }
};

bool expect(bool ok, const char *msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << std::endl;
    return false;
  }
  return true;
}

} // namespace
} // namespace archive_r

int main() {
  using namespace archive_r;

  const PathHierarchy logical = make_single_path("/virtual/dummy");
  DummyStream stream(logical);

  if (!expect(stream.seek_within_single_part(0, SEEK_SET) == -1, "default seek_within_single_part should return -1")) {
    return 1;
  }
  if (!expect(stream.size_of_single_part(logical) == -1, "default size_of_single_part should return -1")) {
    return 1;
  }

  std::cout << "MultiVolumeStreamBase default seek/size exercised" << std::endl;
  return 0;
}
