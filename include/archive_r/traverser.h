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
 * \par Inputs
 * - The input list must not be empty, and each PathHierarchy must not be empty.
 *   Violations throw std::invalid_argument.
 * - For the common single-root case, prefer make_single_path("...") or
 *   Traverser(const std::string&, ...).
 *
 * \par How Roots Are Interpreted
 * - If the root hierarchy is exactly one single path and it refers to a directory,
 *   Traverser enumerates it using std::filesystem::recursive_directory_iterator.
 * - Otherwise, Traverser attempts archive traversal using libarchive.
 *
 * \par Error Model (Exceptions vs Faults)
 * - Invalid arguments are reported via exceptions (std::invalid_argument).
 * - Recoverable data / I/O errors during archive traversal are reported via the
 *   global fault callback (EntryFault) and traversal continues.
 * - Directory traversal uses std::filesystem iterators; filesystem exceptions
 *   (e.g. std::filesystem::filesystem_error) may be thrown and are not converted
 *   to faults.
 *
 * \par Iterator Semantics
 * - Traverser::Iterator is an input iterator (single-pass).
 * - Dereferencing the end iterator throws std::logic_error.
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
  *
  * @throws std::invalid_argument if paths is empty or contains an empty hierarchy
   */
  explicit Traverser(std::vector<PathHierarchy> paths, TraverserOptions options = {});

  /**
   * @brief Construct traverser for a single hierarchy
   */
  explicit Traverser(PathHierarchy path, TraverserOptions options = {});

  /**
   * @brief Construct traverser for a single archive or directory path
   */
  explicit Traverser(const std::string &path, TraverserOptions options = {});

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
