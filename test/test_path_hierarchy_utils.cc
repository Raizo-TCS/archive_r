// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/path_hierarchy.h"
#include "archive_r/path_hierarchy_utils.h"

#include <iostream>
#include <string>
#include <vector>

using namespace archive_r;

namespace {

bool expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << std::endl;
    return false;
  }
  return true;
}

bool test_component_at() {
  PathEntry single = PathEntry::single("file.txt");
  const std::string *v0 = path_entry_component_at(single, 0);
  const std::string *v1 = path_entry_component_at(single, 1);
  if (!expect(v0 && *v0 == "file.txt", "component_at(single,0) failed")) {
    return false;
  }
  if (!expect(v1 == nullptr, "component_at(single,1) should be nullptr")) {
    return false;
  }

  PathEntry default_entry;
  const std::string *i0 = path_entry_component_at(default_entry, 0);
  if (!expect(i0 && i0->empty(), "component_at(default,0) should be empty string")) {
    return false;
  }
  const std::string *i1 = path_entry_component_at(default_entry, 1);
  if (!expect(i1 == nullptr, "component_at(default,1) should be nullptr")) {
    return false;
  }

  PathEntry mv = PathEntry::multi_volume({"a.part1", "a.part2"});
  const std::string *m0 = path_entry_component_at(mv, 0);
  const std::string *m1 = path_entry_component_at(mv, 1);
  const std::string *m2 = path_entry_component_at(mv, 2);
  if (!expect(m0 && *m0 == "a.part1", "component_at(mv,0) failed")) {
    return false;
  }
  if (!expect(m1 && *m1 == "a.part2", "component_at(mv,1) failed")) {
    return false;
  }
  if (!expect(m2 == nullptr, "component_at(mv,2) should be nullptr")) {
    return false;
  }

  return true;
}

bool test_volume_helpers() {
  if (!expect(pathhierarchy_volume_size({}) == 0, "volume_size(empty) should be 0")) {
    return false;
  }

  PathHierarchy single_h = make_single_path("root.zip");
  if (!expect(pathhierarchy_volume_size(single_h) == 1, "volume_size(single) should be 1")) {
    return false;
  }
  if (!expect(pathhierarchy_volume_entry_name(single_h, 0) == "root.zip", "volume_entry_name(single,0) mismatch")) {
    return false;
  }
  if (!expect(pathhierarchy_volume_entry_name(single_h, 1).empty(), "volume_entry_name(single,1) should be empty")) {
    return false;
  }

  PathHierarchy mv_h;
  mv_h.push_back(PathEntry::single("outer"));
  mv_h.push_back(PathEntry::multi_volume({"x.part001", "x.part002", "x.part003"}));

  if (!expect(pathhierarchy_is_multivolume(mv_h), "is_multivolume should be true")) {
    return false;
  }
  if (!expect(pathhierarchy_volume_size(mv_h) == 3, "volume_size(mv) should be 3")) {
    return false;
  }
  if (!expect(pathhierarchy_volume_entry_name(mv_h, 2) == "x.part003", "volume_entry_name(mv,2) mismatch")) {
    return false;
  }
  if (!expect(pathhierarchy_volume_entry_name(mv_h, 3).empty(), "volume_entry_name(mv,3) should be empty")) {
    return false;
  }

  PathHierarchy selected = pathhierarchy_select_single_part(mv_h, 1);
  if (!expect(selected.size() == 2, "select_single_part should keep depth")) {
    return false;
  }
  if (!expect(selected.back().is_single() && selected.back().single_value() == "x.part002", "select_single_part mismatch")) {
    return false;
  }

  PathHierarchy invalid_h;
  invalid_h.push_back(PathEntry{});
  if (!expect(pathhierarchy_volume_size(invalid_h) == 1, "volume_size(invalid tail) should be 1")) {
    return false;
  }
  if (!expect(pathhierarchy_volume_entry_name(invalid_h, 0).empty(), "volume_entry_name(invalid tail,0) should be empty")) {
    return false;
  }

  return true;
}

bool test_flatten_and_display() {
  std::string out;
  PathEntry single = PathEntry::single("hello");
  if (!expect(flatten_entry_to_string(single, out) && out == "hello", "flatten(single) failed")) {
    return false;
  }
  PathEntry mv = PathEntry::multi_volume({"a", "b"});
  out.clear();
  if (!expect(!flatten_entry_to_string(mv, out), "flatten(mv) should be false")) {
    return false;
  }

  PathEntry default_entry;
  out.clear();
  if (!expect(flatten_entry_to_string(default_entry, out) && out.empty(), "flatten(default) should yield empty string")) {
    return false;
  }
  out.clear();
  if (!expect(entry_name_from_component(default_entry, out) && out.empty(), "entry_name_from_component(default) should yield empty string")) {
    return false;
  }
  if (!expect(path_entry_display(default_entry).empty(), "path_entry_display(default) should be empty")) {
    return false;
  }
  out.clear();
  if (!expect(entry_name_from_component(mv, out) && out == "a", "entry_name_from_component(mv) should be first part")) {
    return false;
  }
  if (!expect(path_entry_display(mv) == "[a|b]", "path_entry_display(mv) mismatch")) {
    return false;
  }

  PathHierarchy h;
  h.push_back(PathEntry::single("root"));
  h.push_back(mv);
  if (!expect(hierarchy_display(h) == "root/[a|b]", "hierarchy_display mismatch")) {
    return false;
  }

  return true;
}

bool test_merge_multi_volume_sources() {
  // Success: only last element differs, same prefix.
  PathHierarchy h1;
  h1.push_back(PathEntry::single("outer.tar.gz"));
  h1.push_back(PathEntry::single("inner.part001"));

  PathHierarchy h2;
  h2.push_back(PathEntry::single("outer.tar.gz"));
  h2.push_back(PathEntry::single("inner.part002"));

  PathHierarchy merged = merge_multi_volume_sources({h1, h2});
  if (!expect(merged.size() == 2, "merge result size mismatch")) {
    return false;
  }
  if (!expect(merged.front().is_single() && merged.front().single_value() == "outer.tar.gz", "merge prefix mismatch")) {
    return false;
  }
  if (!expect(merged.back().is_multi_volume(), "merge tail should be multi-volume")) {
    return false;
  }
  if (!expect(merged.back().multi_volume_parts().values.size() == 2, "merge parts size mismatch")) {
    return false;
  }

  // Failure cases: empty, mismatched sizes.
  if (!expect(merge_multi_volume_sources({}).empty(), "merge(empty) should be empty")) {
    return false;
  }

  PathHierarchy short_h = {PathEntry::single("outer.tar.gz")};
  if (!expect(merge_multi_volume_sources({h1, short_h}).empty(), "merge(mismatched sizes) should be empty")) {
    return false;
  }

  // Failure: first hierarchy is empty (reference.empty()).
  PathHierarchy empty_h;
  if (!expect(merge_multi_volume_sources({empty_h, h1}).empty(), "merge(empty reference) should be empty")) {
    return false;
  }

  // Failure: all entries equal but hierarchy sizes differ (difference_found == false path).
  PathHierarchy same_prefix_short;
  same_prefix_short.push_back(PathEntry::single("same"));

  PathHierarchy same_prefix_long;
  same_prefix_long.push_back(PathEntry::single("same"));
  same_prefix_long.push_back(PathEntry::single("extra"));

  if (!expect(merge_multi_volume_sources({same_prefix_short, same_prefix_long}).empty(), "merge(equal prefix, different sizes) should be empty")) {
    return false;
  }

  // Failure: determine succeeds but required_size mismatch is caught in collect_multi_volume_parts.
  PathHierarchy longer_tail;
  longer_tail.push_back(PathEntry::single("outer.tar.gz"));
  longer_tail.push_back(PathEntry::single("inner.part002"));
  longer_tail.push_back(PathEntry::single("unexpected_suffix"));
  if (!expect(merge_multi_volume_sources({h1, longer_tail}).empty(), "merge(required_size mismatch) should be empty")) {
    return false;
  }

  // Failure: multi-volume part component must be single.
  PathHierarchy non_single_part;
  non_single_part.push_back(PathEntry::single("outer.tar.gz"));
  non_single_part.push_back(PathEntry::multi_volume({"inner.part002", "inner.part003"}));
  if (!expect(merge_multi_volume_sources({h1, non_single_part}).empty(), "merge(non-single part) should be empty")) {
    return false;
  }

  // Failure: differing suffix (third element differs).
  PathHierarchy h3;
  h3.push_back(PathEntry::single("outer.tar.gz"));
  h3.push_back(PathEntry::single("inner.part001"));
  h3.push_back(PathEntry::single("suffix-a"));

  PathHierarchy h4;
  h4.push_back(PathEntry::single("outer.tar.gz"));
  h4.push_back(PathEntry::single("inner.part002"));
  h4.push_back(PathEntry::single("suffix-b"));

  if (!expect(merge_multi_volume_sources({h3, h4}).empty(), "merge(different suffix) should be empty")) {
    return false;
  }

  return true;
}

bool test_sort_hierarchies() {
  PathHierarchy a = make_single_path("a");
  PathHierarchy b = make_single_path("b");
  PathHierarchy mv;
  mv.push_back(PathEntry::multi_volume({"x1", "x2"}));

  std::vector<PathHierarchy> list = {b, mv, a};
  sort_hierarchies(list);

  if (!expect(list.size() == 3, "sort size mismatch")) {
    return false;
  }
  if (!expect(hierarchies_equal(list[0], a), "sort[0] should be 'a'")) {
    return false;
  }
  if (!expect(hierarchies_equal(list[1], b), "sort[1] should be 'b'")) {
    return false;
  }
  if (!expect(hierarchies_equal(list[2], mv), "sort[2] should be multi-volume")) {
    return false;
  }

  return true;
}

bool test_compare_entries_multivolume_size_mismatch() {
  // Compare multi-volume entries where the common prefix is identical but the
  // number of parts differs. This should take the lsize!=rsize branch.
  PathEntry short_mv = PathEntry::multi_volume({"a"});
  PathEntry long_mv = PathEntry::multi_volume({"a", "b"});

  bool ok = true;
  ok &= expect(compare_entries(short_mv, long_mv) < 0, "compare_entries(short_mv,long_mv) should be < 0");
  ok &= expect(compare_entries(long_mv, short_mv) > 0, "compare_entries(long_mv,short_mv) should be > 0");
  return ok;
}

bool test_compare_entries_single_ordering_branches() {
  PathEntry a = PathEntry::single("a");
  PathEntry b = PathEntry::single("b");
  PathEntry a2 = PathEntry::single("a");

  bool ok = true;
  ok &= expect(compare_entries(a, b) < 0, "compare_entries(a,b) should be < 0");
  ok &= expect(compare_entries(b, a) > 0, "compare_entries(b,a) should be > 0");
  ok &= expect(compare_entries(a, a2) == 0, "compare_entries(a,a) should be == 0");
  return ok;
}

bool test_compare_hierarchies_size_mismatch_branches() {
  PathHierarchy short_h;
  short_h.push_back(PathEntry::single("same"));

  PathHierarchy long_h;
  long_h.push_back(PathEntry::single("same"));
  long_h.push_back(PathEntry::single("extra"));

  bool ok = true;
  ok &= expect(compare_hierarchies(short_h, long_h) < 0, "compare_hierarchies(short,long) should be < 0");
  ok &= expect(compare_hierarchies(long_h, short_h) > 0, "compare_hierarchies(long,short) should be > 0");

  PathHierarchy different_first = make_single_path("a");
  PathHierarchy different_second = make_single_path("b");
  ok &= expect(compare_hierarchies(different_first, different_second) < 0, "compare_hierarchies(a,b) should be < 0");
  ok &= expect(compare_hierarchies(different_second, different_first) > 0, "compare_hierarchies(b,a) should be > 0");
  return ok;
}

bool test_pathhierarchy_prefix_until_bounds() {
  bool ok = true;

  // empty input
  ok &= expect(pathhierarchy_prefix_until({}, 0).empty(), "prefix_until(empty,0) should be empty");

  PathHierarchy h;
  h.push_back(PathEntry::single("a"));
  h.push_back(PathEntry::single("b"));

  // inclusive_index out of range
  ok &= expect(pathhierarchy_prefix_until(h, h.size()).empty(), "prefix_until(out of range) should be empty");

  // inclusive_index in range
  PathHierarchy p0 = pathhierarchy_prefix_until(h, 0);
  ok &= expect(p0.size() == 1, "prefix_until(h,0) size mismatch");
  ok &= expect(p0[0].is_single(), "prefix_until(h,0) entry type mismatch");
  ok &= expect(p0[0].single_value() == "a", "prefix_until(h,0) value mismatch");

  PathHierarchy p1 = pathhierarchy_prefix_until(h, 1);
  ok &= expect(p1.size() == 2, "prefix_until(h,1) size mismatch");
  ok &= expect(p1[1].is_single(), "prefix_until(h,1) entry type mismatch");
  ok &= expect(p1[1].single_value() == "b", "prefix_until(h,1) value mismatch");

  return ok;
}

} // namespace

int main() {
  bool ok = true;
  ok &= test_component_at();
  ok &= test_volume_helpers();
  ok &= test_flatten_and_display();
  ok &= test_merge_multi_volume_sources();
  ok &= test_sort_hierarchies();
  ok &= test_compare_entries_multivolume_size_mismatch();
  ok &= test_compare_entries_single_ordering_branches();
  ok &= test_compare_hierarchies_size_mismatch_branches();
  ok &= test_pathhierarchy_prefix_until_bounds();

  if (!ok) {
    return 1;
  }

  std::cout << "Path hierarchy utils tests passed" << std::endl;
  return 0;
}
