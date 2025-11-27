// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/traverser.h"
#include <cstring>
#include <iostream>
#include <optional>
#include <vector>

using namespace archive_r;

/**
 * @brief Test Entry::read() with independent readers
 *
 * This test verifies that Entry::read() works correctly in three scenarios:
 * 1. Reading current entry during traverse
 * 2. Reading different entry during traverse (should not interfere)
 * 3. Reading entry after traverse completion
 */

int main() {
  std::cout << "\n=== Testing Entry::read() with Independent Readers ===\n" << std::endl;

  const std::string test_archive = "test_data/stress_test_ultimate.tar.gz";
  std::cout << "Using deeply nested archive: " << test_archive << "\n" << std::endl;

  // Phase 0: Verify read() toggles descent state to false
  {
    std::cout << "Phase 0: Verifying Entry::read() disables descent...\n" << std::endl;
    Traverser traverser({ make_single_path(test_archive) });
    bool verified = false;

    for (Entry &entry : traverser) {
      if (!entry.is_file() || entry.depth() == 0) {
        continue;
      }
      if (!entry.descent_enabled()) {
        continue;
      }

      std::vector<char> buffer(64, 0);
      const ssize_t bytes_read = entry.read(buffer.data(), buffer.size());
      if (bytes_read <= 0) {
        continue;
      }

      if (entry.descent_enabled()) {
        std::cerr << "  [FAIL] descent flag remained enabled after read" << std::endl;
        return 1;
      }

      std::cout << "  [OK] descent disabled automatically after read (bytes=" << bytes_read << ")" << std::endl;
      verified = true;
      break;
    }

    if (!verified) {
      std::cerr << "  [FAIL] Could not find suitable entry to verify descent flag" << std::endl;
      return 1;
    }
  }

  // Target files for testing (depth 7, deeply nested)
  const std::string target_file1 = "deep_content_1.txt"; // 8000 bytes
  const std::string target_file2 = "deep_content_2.txt"; // 12000 bytes

  // Phase 1: Locate deeply nested test entries
  std::cout << "Phase 1: Locating deeply nested test entries...\n" << std::endl;

  {
    Traverser traverser({ make_single_path(test_archive) });
    int count = 0;
    int deep_files = 0;

    for (Entry &entry : traverser) {
      count++;

      // Show deeply nested files (depth >= 6)
      if (entry.is_file() && entry.depth() >= 6) {
        deep_files++;
        if (deep_files <= 5) { // Show first 5 deep files
          std::cout << "  [depth=" << entry.depth() << "] " << entry.name() << " (" << entry.size() << " bytes)" << std::endl;
        }
      }
    }

    std::cout << "Total entries: " << count << std::endl;
    std::cout << "Deep nested files (depth>=6): " << deep_files << "\n" << std::endl;

    if (deep_files < 2) {
      std::cerr << "ERROR: Not enough deep files for testing" << std::endl;
      return 1;
    }
  }

  // Phase 2: Read current entry during traverse (deeply nested)
  std::cout << "Phase 2: Reading CURRENT deeply nested entry during traverse...\n" << std::endl;

  {
    Traverser traverser({ make_single_path(test_archive) });
    bool found_and_read = false;

    for (Entry &entry : traverser) {
      if (!entry.is_file())
        continue;
      if (entry.name() != target_file1)
        continue; // Find our target file

      std::cout << "  Found target: " << entry.name() << std::endl;
      std::cout << "  Depth: " << entry.depth() << std::endl;
      std::cout << "  Size: " << entry.size() << " bytes" << std::endl;
      std::cout << "  Reading current entry..." << std::endl;

      char buffer[10000];
      ssize_t bytes_read = entry.read(buffer, sizeof(buffer) - 1);

      if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        std::cout << "  ✓ Successfully read " << bytes_read << " bytes from deeply nested file" << std::endl;
        found_and_read = true;
        break;
      } else {
        std::cerr << "  ✗ Failed to read current entry (error)" << std::endl;
        return 1;
      }
    }

    if (!found_and_read) {
      std::cerr << "ERROR: Could not find or read target file: " << target_file1 << std::endl;
      return 1;
    }
  }

  // Phase 3: Read DIFFERENT entry during traverse (both deeply nested)
  std::cout << "\nPhase 3: Reading DIFFERENT deeply nested entry during traverse...\n" << std::endl;

  {
    Traverser traverser({ make_single_path(test_archive) });
    std::optional<Entry> saved_entry; // Save by value, not pointer!
    std::string saved_entry_path;
    bool found_both = false;

    for (Entry &entry : traverser) {
      if (!entry.is_file())
        continue;

      // Save first target file
      if (entry.name() == target_file1 && !saved_entry) {
        saved_entry = entry; // Copy the Entry
        saved_entry_path = entry.path();
        std::cout << "  Saved first file: " << entry.name() << " (depth " << entry.depth() << ")" << std::endl;
      }

      // At second target file, try to read the saved first file
      if (entry.name() == target_file2 && saved_entry) {
        std::cout << "  Now at second file: " << entry.name() << " (depth " << entry.depth() << ")" << std::endl;
        std::cout << "  Attempting to read DIFFERENT entry (first file)..." << std::endl;

        char buffer1[10000];
        ssize_t bytes_read1 = saved_entry->read(buffer1, sizeof(buffer1) - 1);

        if (bytes_read1 > 0) {
          std::cout << "  ✓ Successfully read different entry: " << bytes_read1 << " bytes" << std::endl;

          // Verify current entry is still accessible
          std::cout << "  Verifying current entry is still accessible..." << std::endl;
          char buffer2[15000];
          ssize_t bytes_read2 = entry.read(buffer2, sizeof(buffer2) - 1);

          if (bytes_read2 > 0) {
            std::cout << "  ✓ Current entry still accessible: " << bytes_read2 << " bytes" << std::endl;
            found_both = true;
          } else {
            std::cerr << "  ✗ Current entry became inaccessible!" << std::endl;
            return 1;
          }
        } else {
          std::cerr << "  ✗ Failed to read different entry" << std::endl;
          return 1;
        }
        break;
      }
    }

    if (!found_both) {
      std::cerr << "ERROR: Could not find both target files" << std::endl;
      return 1;
    }
  }

  // Phase 4: Read entry AFTER traverse completion (deeply nested)
  std::cout << "\nPhase 4: Reading deeply nested entry AFTER traverse completion...\n" << std::endl;

  {
    Traverser traverser({ make_single_path(test_archive) });
    std::optional<Entry> saved_entry; // Save by value, not pointer!
    std::string saved_path;

    // Traverse completely and save a deeply nested entry
    for (Entry &entry : traverser) {
      if (!entry.is_file())
        continue;

      if (entry.name() == target_file1) {
        saved_entry = entry; // Copy the Entry
        saved_path = entry.path();
        std::cout << "  Saved entry: " << entry.name() << " (depth " << entry.depth() << ")" << std::endl;
      }
    }

    std::cout << "  Traverse completed" << std::endl;

    if (saved_entry) {
      std::cout << "  Attempting to read saved entry after traverse..." << std::endl;

      char buffer[10000];
      ssize_t bytes_read = saved_entry->read(buffer, sizeof(buffer) - 1);

      if (bytes_read > 0) {
        std::cout << "  ✓ Successfully read after traverse: " << bytes_read << " bytes" << std::endl;
      } else {
        std::cerr << "  ✗ Failed to read after traverse" << std::endl;
        return 1;
      }
    } else {
      std::cerr << "ERROR: Could not save entry" << std::endl;
      return 1;
    }
  }

  std::cout << "\n=== All Tests Passed! ===" << std::endl;
  std::cout << "✓ Reading current deeply nested entry during traverse" << std::endl;
  std::cout << "✓ Reading different deeply nested entry during traverse (non-interfering)" << std::endl;
  std::cout << "✓ Reading deeply nested entry after traverse completion" << std::endl;

  std::cout << "\nPhase 5: Reading filesystem file outside of archives...\n" << std::endl;

  {
    const std::string filesystem_file = "test_data/file1.txt";
    const std::string expected_content = "Sample file 1\n";

    Traverser traverser({ make_single_path(filesystem_file) });
    bool read_success = false;

    for (Entry &entry : traverser) {
      if (!entry.is_file()) {
        std::cerr << "  ✗ Unexpected non-file entry when traversing filesystem path" << std::endl;
        return 1;
      }

      std::vector<char> buffer(static_cast<size_t>(entry.size()));
      if (buffer.empty()) {
        buffer.resize(expected_content.size());
      }

      const ssize_t bytes_read = entry.read(buffer.data(), buffer.size());
      if (bytes_read < 0) {
        std::cerr << "  ✗ Failed to read filesystem entry payload" << std::endl;
        return 1;
      }

      const std::string actual(buffer.data(), buffer.data() + bytes_read);
      if (actual != expected_content) {
        std::cerr << "  ✗ Filesystem entry content mismatch" << std::endl;
        return 1;
      }

      std::cout << "  ✓ Successfully read filesystem file (" << bytes_read << " bytes)" << std::endl;
      read_success = true;
    }

    if (!read_success) {
      std::cerr << "  ✗ Traversal produced no entries for filesystem file" << std::endl;
      return 1;
    }
  }

  return 0;
}
