// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/entry_metadata.h"
#include "entry_fault_error.h"
#include <archive.h>
#include <archive_entry.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "archive_r/platform_compat.h"

namespace archive_r {

struct archive_deleter {
  void operator()(struct archive *a) const;
};

using archive_ptr = std::unique_ptr<struct archive, archive_deleter>;
using open_delegate = std::function<int(struct archive *ar)>;

struct ArchiveOption {
  std::vector<std::string> passphrases;   ///< Passphrases for encrypted archives
  std::vector<std::string> formats;       ///< Specific format names to enable (empty = all)
  std::unordered_set<std::string> metadata_keys; ///< Metadata keys to capture (empty = none)
};

archive_ptr new_read_archive_common(const std::vector<std::string> &passphrases, const std::vector<std::string> &format_names, open_delegate archive_open);

struct Archive {
  Archive();
  virtual ~Archive();

  Archive(const Archive &) = delete;
  Archive &operator=(const Archive &) = delete;

  virtual void open_archive() = 0;
  virtual void close_archive();
  virtual void rewind();

  bool skip_to_next_header();
  bool skip_data();
  bool skip_to_entry(const std::string &entryname);
  bool skip_to_eof();

  std::string current_entryname;
  struct archive_entry *current_entry;
  ssize_t read_current(void *buff, size_t len);

  // Get current entry metadata
  uint64_t current_entry_size() const;
  mode_t current_entry_filetype() const;
  EntryMetadataMap current_entry_metadata(const std::unordered_set<std::string> &allowed_keys) const;

  struct archive *_ar;
  bool _at_eof;

protected:
  bool _current_entry_content_ready;

public:
  bool current_entry_content_ready() const { return _current_entry_content_ready; }

private:
  [[noreturn]] void raise_archive_error(const std::string &message);

  bool search_forward_until_eof(const std::string &entryname);
  bool search_until_position(const std::string &entryname, const std::string &stop_position);
};

} // namespace archive_r
