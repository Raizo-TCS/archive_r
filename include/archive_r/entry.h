// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "archive_r/entry_fault.h"
#include "archive_r/entry_metadata.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/platform_compat.h"

namespace archive_r {

class ArchiveStackOrchestrator;

struct MultiVolumeGroupOptions {
  PathEntry::Parts::Ordering ordering = PathEntry::Parts::Ordering::Natural;
};

/**
 * @brief Represents a single entry in an archive traversal
 *
 * Entry objects encapsulate all information about an archive entry including:
 * - Path information (path, path hierarchy)
 * - Metadata (size, type, timestamps)
 * - Content access (read operations)
 * - Multi-volume archive grouping support
 *
 * \par Lifetime and Copying
 * - An Entry& obtained while iterating a Traverser is typically valid until the
 *   iterator advances.
 * - Entry is copyable. Copies retain metadata (name/path/metadata/etc), but do not
 *   retain traverser-managed traversal control state. Calling set_descent() or
 *   set_multi_volume_group() on such copies will report a fault and has no effect.
 *   Prefer calling these control methods on the Entry& inside the iteration loop,
 *   before advancing.
 *
 * \par Reading
 * - read() returns >0 for bytes read, 0 for EOF, -1 for error.
 * - On error, read() dispatches an EntryFault via the registered fault callback
 *   (if any).
 * - After any successful read() (including EOF), descent is disabled until
 *   explicitly re-enabled via set_descent(true).
 */
class Entry {
public:
  /**
   * @brief Get the entry name (last element of the path hierarchy)
   * @return Entry name relative to its containing archive (e.g., "dir/subdir/file.txt" when the
   *         hierarchy is {"outer/archive.zip", "dir/subdir/file.txt"})
   */
  std::string name() const;

  /**
   * @brief Get the entry path as a string
   * @return Joined path including outer archives (e.g., "outer/archive.zip/dir/subdir/file.txt"
   * when the hierarchy is {"outer/archive.zip", "dir/subdir/file.txt"})
   */
  std::string path() const;

  /**
   * @brief Get the entry path as a hierarchy of components
   * @return Vector describing each descent step (e.g., {"outer/archive.zip",
   * "dir/subdir/file.txt"})
   */
  const PathHierarchy &path_hierarchy() const;

  /**
   * @brief Check if entry is a directory
   * @return true if entry represents a directory
   */
  bool is_directory() const;

  /**
   * @brief Check if entry is a regular file
   * @return true if entry represents a regular file
   */
  bool is_file() const;

  /**
   * @brief Get the uncompressed size of the entry
   * @return Size in bytes, or 0 if unknown
   */
  uint64_t size() const;

  /**
   * @brief Get the archive nesting depth
   * @return 0 for top-level archive, 1 for nested archive, etc.
   */
  size_t depth() const;

  /**
   * @brief Read data from the entry
   *
    * Each call uses an internal ArchiveStackOrchestrator so reads remain valid even
    * if the owning iterator advances.
   *
   * @param buffer Buffer to read data into
   * @param length Maximum number of bytes to read
   * @return Number of bytes read, 0 on EOF, -1 on error
   */
  ssize_t read(void *buffer, size_t length);

  /**
   * @brief Enable or disable automatic descent into this entry
   * @param enabled true to descend (default), false to keep traversal at current level
    *
    * This control is only available for entries that are managed by a Traverser.
    * Calling this on an Entry that is not traverser-managed reports a fault.
   */
  void set_descent(bool enabled);

  /**
   * @brief Check if automatic descent is currently enabled
   */
  bool descent_enabled() const;

  /**
   * @brief Register this entry as part of a multi-volume (split) archive
   * @param base_name Base name without the volume suffix (e.g., "archive.tar.gz")
   * @param options Optional configuration (e.g., preserve Given ordering)
   *
   * Register each entry that belongs to the same multi-volume group so that
   * once traversal of the parent archive finishes, the parts are combined
   * automatically. The traverser will then descend into the combined archive
   * and continue processing its contents.
   *
   * Example:
   * @code
   * for (Entry& entry : traverser) {
   *     if (entry.path().find(".part") != std::string::npos) {
   *         std::string base = extract_base_name(entry.path());
   *         entry.set_multi_volume_group(base);
   *     }
   * }
   * @endcode
  *
  * This control is only available for entries that are managed by a Traverser.
  * Calling this on an Entry that is not traverser-managed reports a fault.
   */
  void set_multi_volume_group(const std::string &base_name, const MultiVolumeGroupOptions &options = {});

  /**
   * @brief Get metadata captured for this entry
   * @return Immutable metadata map keyed by libarchive field names
   */
  const EntryMetadataMap &metadata() const;

  /**
   * @brief Look up a metadata value by key
   * @param key Metadata key (e.g., "uid", "mtime")
   * @return Pointer to the stored value, or nullptr if not present
   */
  const EntryMetadataValue *find_metadata(const std::string &key) const;

  static std::unique_ptr<Entry> create(PathHierarchy hierarchy, std::shared_ptr<ArchiveStackOrchestrator> data_source_orchestrator, bool default_descent);

  // Copy/move operations
  Entry(const Entry &);
  Entry &operator=(const Entry &);
  Entry(Entry &&) noexcept;
  Entry &operator=(Entry &&) noexcept;

  ~Entry();

private:
  class Impl;
  std::unique_ptr<Impl> _impl;

  // Private constructor - only friends can create Entry objects
  explicit Entry(Impl *impl);
  Entry(const PathHierarchy &hierarchy, std::shared_ptr<ArchiveStackOrchestrator> data_source_orchestrator, bool default_descent);
};

} // namespace archive_r
