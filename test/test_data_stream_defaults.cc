// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/data_stream.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace archive_r;

namespace {

class MinimalStream final : public IDataStream {
public:
  explicit MinimalStream(PathHierarchy source)
    : _source(std::move(source)) {}

  ssize_t read(void *buffer, size_t size) override {
    (void)buffer;
    (void)size;
    return 0;
  }

  void rewind() override {}

  bool at_end() const override { return true; }

  PathHierarchy source_hierarchy() const override { return _source; }

private:
  PathHierarchy _source;
};

bool expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << std::endl;
    return false;
  }
  return true;
}

} // namespace

int main() {
  MinimalStream stream(make_single_path("dummy"));

  if (!expect(stream.seek(0, 0) == -1, "IDataStream::seek default should return -1")) {
    return 1;
  }
  if (!expect(stream.tell() == -1, "IDataStream::tell default should return -1")) {
    return 1;
  }
  if (!expect(!stream.can_seek(), "IDataStream::can_seek default should be false")) {
    return 1;
  }

  std::cout << "IDataStream default method tests passed" << std::endl;
  return 0;
}
