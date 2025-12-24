// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/path_hierarchy_utils.h"

#include "archive_stack_orchestrator.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <string>

using namespace archive_r;

namespace {

struct OomControl {
  bool enabled = false;
  std::size_t remaining_allocations_until_throw = 0;
};

thread_local OomControl g_oom;

bool should_throw_now() {
  if (!g_oom.enabled) {
    return false;
  }
  if (g_oom.remaining_allocations_until_throw == 0) {
    return false;
  }
  g_oom.remaining_allocations_until_throw--;
  return g_oom.remaining_allocations_until_throw == 0;
}

struct ScopedOom {
  bool prev_enabled;
  std::size_t prev_remaining;

  explicit ScopedOom(std::size_t fail_on_nth_allocation)
      : prev_enabled(g_oom.enabled), prev_remaining(g_oom.remaining_allocations_until_throw) {
    g_oom.enabled = true;
    g_oom.remaining_allocations_until_throw = fail_on_nth_allocation;
  }

  ~ScopedOom() {
    g_oom.enabled = prev_enabled;
    g_oom.remaining_allocations_until_throw = prev_remaining;
  }

  ScopedOom(const ScopedOom &) = delete;
  ScopedOom &operator=(const ScopedOom &) = delete;
};

void *alloc_unaligned(std::size_t size) {
  // operator new(0) must return non-null.
  if (size == 0) {
    size = 1;
  }

  while (true) {
    if (should_throw_now()) {
      throw std::bad_alloc();
    }

    if (void *p = std::malloc(size)) {
      return p;
    }

    std::new_handler h = std::get_new_handler();
    if (!h) {
      throw std::bad_alloc();
    }
    h();
  }
}

void *alloc_aligned(std::size_t alignment, std::size_t size) {
  if (size == 0) {
    size = 1;
  }

  while (true) {
    if (should_throw_now()) {
      throw std::bad_alloc();
    }

    void *p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(size, alignment);
    if (p) {
      return p;
    }
#else
    // posix_memalign alignment must be a power of two and multiple of sizeof(void*).
    const int rc = ::posix_memalign(&p, alignment, size);
    if (rc == 0 && p) {
      return p;
    }
#endif

    std::new_handler h = std::get_new_handler();
    if (!h) {
      throw std::bad_alloc();
    }
    h();
  }
}

bool expect(bool cond, const std::string &msg) {
  if (!cond) {
    std::cerr << msg << std::endl;
    return false;
  }
  return true;
}

bool exercise_entry_make_shared_throw_edge() {
  // Prepare everything with OOM injection OFF.
  auto e = Entry::create(make_single_path("test_data/no_uid.zip"), nullptr, true);

  // Narrow injection window: ONLY around the call we want to exercise.
  char buf[16] = {};
  try {
    ScopedOom guard(/*fail_on_nth_allocation=*/1);
    (void)e->read(buf, sizeof(buf));
  } catch (const std::bad_alloc &) {
    return true; // expected
  }

  // If we reached here, allocation did not fail as expected.
  return false;
}

bool exercise_entry_copy_ctor_throw_edge() {
  // Build an Entry that has non-empty metadata so copying it should allocate.
  ArchiveOption opts;
  opts.metadata_keys.insert("uid");
  opts.metadata_keys.insert("gid");
  auto orchestrator = std::make_shared<ArchiveStackOrchestrator>(opts);

  auto e = Entry::create(make_single_path("test_data/no_uid.zip"), orchestrator, true);
  if (!e) {
    return false;
  }

  // Sanity: metadata should be populated when requested.
  if (!e->find_metadata("uid") && !e->find_metadata("gid")) {
    return false;
  }

  // Ensure the non-throw path is exercised too.
  {
    Entry copied_ok(*e);
    (void)copied_ok.depth();
  }

  // Narrow injection window around copy construction.
  // fail_on_nth_allocation=3 aims to let:
  //  1) the Impl object allocation succeed,
  //  2) PathHierarchy vector allocation succeed,
  //  3) then throw during later member copies (e.g., metadata map copy).
  try {
    ScopedOom guard(/*fail_on_nth_allocation=*/3);
    Entry copied(*e);
    (void)copied.depth();
  } catch (const std::bad_alloc &) {
    return true; // expected
  }

  return false;
}

bool exercise_entry_depth_empty_hierarchy_branch() {
  PathHierarchy empty;
  auto e = Entry::create(std::move(empty), nullptr, true);
  if (!e) {
    return false;
  }
  if (e->depth() != 0) {
    return false;
  }
  // Cover the empty-hierarchy branch in Entry::Impl::name().
  if (!e->name().empty()) {
    return false;
  }
  // Exercise path() as well (should be empty).
  if (!e->path().empty()) {
    return false;
  }
  return true;
}

bool exercise_entry_self_assignment_branch() {
  auto a_ptr = Entry::create(make_single_path("test_data/no_uid.zip"), nullptr, true);
  auto b_ptr = Entry::create(make_single_path("test_data/no_uid.zip"), nullptr, true);
  if (!a_ptr || !b_ptr) {
    return false;
  }

  Entry &a = *a_ptr;
  Entry &b = *b_ptr;

  // Non-self assignment branch.
  a = b;
  (void)a.depth();

  // Self-assignment branch.
  a = a;
  (void)a.depth();

  return true;
}

} // namespace

// -----------------------------------------------------------------------------
// Test-only global operator new/delete override
// -----------------------------------------------------------------------------

void *operator new(std::size_t size) { return alloc_unaligned(size); }
void *operator new[](std::size_t size) { return alloc_unaligned(size); }

void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }

// sized delete
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }

// aligned new/delete (C++17)
void *operator new(std::size_t size, std::align_val_t alignment) {
  return alloc_aligned(static_cast<std::size_t>(alignment), size);
}
void *operator new[](std::size_t size, std::align_val_t alignment) {
  return alloc_aligned(static_cast<std::size_t>(alignment), size);
}

void operator delete(void *p, std::align_val_t) noexcept {
#if defined(_WIN32)
  _aligned_free(p);
#else
  std::free(p);
#endif
}
void operator delete[](void *p, std::align_val_t) noexcept {
#if defined(_WIN32)
  _aligned_free(p);
#else
  std::free(p);
#endif
}

void operator delete(void *p, std::size_t, std::align_val_t) noexcept {
#if defined(_WIN32)
  _aligned_free(p);
#else
  std::free(p);
#endif
}
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept {
#if defined(_WIN32)
  _aligned_free(p);
#else
  std::free(p);
#endif
}

int main() {
  bool ok = true;

  ok = expect(exercise_entry_make_shared_throw_edge(), "Expected std::bad_alloc during Entry::read() under OOM injection") && ok;
  ok = expect(exercise_entry_copy_ctor_throw_edge(), "Expected std::bad_alloc during Entry copy construction under OOM injection") && ok;
  ok = expect(exercise_entry_depth_empty_hierarchy_branch(), "Expected Entry::depth() == 0 for empty hierarchy") && ok;
  ok = expect(exercise_entry_self_assignment_branch(), "Expected Entry self-assignment and non-self assignment to work") && ok;

  if (!ok) {
    return 1;
  }

  std::cout << "OOM operator new injection test passed" << std::endl;
  return 0;
}
