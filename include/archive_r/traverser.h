// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/entry_fault.h"
#include "archive_r/path_hierarchy.h"
#include "entry.h"
#include <memory>
#include <string>
#include <vector>

namespace archive_r {

struct TraverserOptions {
  std::vector<std::string> passphrases;   ///< Passphrases for encrypted archives
  std::vector<std::string> formats;       ///< Specific archive formats to enable (empty = all)
  std::vector<std::string> metadata_keys; ///< Metadata keys to capture for entries
  bool descend_archives = true;           ///< Whether to descend into archives by default
};

/**
 * @brief Iterator-based traversal for archives and directories
 *
 * Traverser provides a unified iterator-based interface for traversing
 * entries within archives and directories, including support for nested
 * archives and automatic descent.
 *
 * Supports multiple archive formats via libarchive (tar, zip, gzip, etc.)
 * and filesystem directories.
 *
 * Uses std::filesystem for directory traversal and ArchiveStackOrchestrator for archives.

 * @see Entry, ArchiveStackOrchestrator
 *
 * Usage:
 *   Traverser traverser({make_single_path("archive.tar.gz")});  // or directory path
 *   for (Entry& entry : traverser) {
 *       // Process entry
 *   }
 *
 * @note Thread Safety
 * Traverser instances are not thread-safe. To use the traverser in a
 * multi-threaded environment, create a separate Traverser instance for each
 * thread. Do not share a single instance across multiple threads.
 */
class Traverser {
public:
  /**
   * @brief Construct traverser for archives or directories
   * @param paths Paths to archive files or directories
   *
   * Provide one or more paths to traverse. Single-path traversal can be
   * achieved by passing a container with one element:
   *   Traverser traverser({make_single_path("archive.tar.gz")});
   */
  explicit Traverser(std::vector<PathHierarchy> paths, TraverserOptions options = {});

  ~Traverser();

  // Non-copyable
  Traverser(const Traverser &) = delete;
  Traverser &operator=(const Traverser &) = delete;

  // ========================================================================
  // Iterator API
  // ========================================================================

  /**
   * @brief Forward iterator for traversing entries
   *
   * Satisfies InputIterator requirements:
   * - Move-only (non-copyable)
   * - Equality comparable
   * - Dereferenceable (returns Entry&)
   * - Incrementable
   */
  class Iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = Entry;
    using difference_type = std::ptrdiff_t;
    using pointer = Entry *;
    using reference = Entry &;

    reference operator*();
    pointer operator->();
    Iterator &operator++();
    bool operator==(const Iterator &other) const;
    bool operator!=(const Iterator &other) const;

    ~Iterator();
    Iterator(const Iterator &) = delete;
    Iterator &operator=(const Iterator &) = delete;
    Iterator(Iterator &&) noexcept;
    Iterator &operator=(Iterator &&) noexcept;

  private:
    friend class Traverser;
    class Impl;
    std::unique_ptr<Impl> _impl;
    explicit Iterator(std::unique_ptr<Impl> impl);
  };

  /**
   * @brief Get iterator to first entry
   * @return Iterator pointing to first entry
   */
  Iterator begin();

  /**
   * @brief Get end iterator
   * @return End iterator (sentinel)
   */
  Iterator end();

private:
  std::vector<PathHierarchy> _initial_paths; ///< Initial paths provided to constructor
  TraverserOptions _options;                 ///< Options controlling archive handling
};

} // namespace archive_r
