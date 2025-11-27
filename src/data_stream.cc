// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/data_stream.h"

#include <mutex>
#include <utility>

namespace archive_r {

namespace {

RootStreamFactory &factory_storage() {
  static RootStreamFactory factory;
  return factory;
}

std::mutex &factory_mutex() {
  static std::mutex mutex;
  return mutex;
}

} // namespace

void set_root_stream_factory(RootStreamFactory factory) {
  std::lock_guard<std::mutex> lock(factory_mutex());
  factory_storage() = std::move(factory);
}

RootStreamFactory get_root_stream_factory() {
  std::lock_guard<std::mutex> lock(factory_mutex());
  return factory_storage();
}

} // namespace archive_r
