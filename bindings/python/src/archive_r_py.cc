// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/data_stream.h"
#include "archive_r/entry.h"
#include "archive_r/entry_fault.h"
#include "archive_r/multi_volume_stream_base.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"
#include "archive_stack_orchestrator.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>

#ifndef ARCHIVE_R_VERSION
#define ARCHIVE_R_VERSION "0.0.0"
#endif

namespace py = pybind11;
using namespace archive_r;

namespace {
const std::vector<std::string> &standard_formats() {
  static const std::vector<std::string> formats = { "7zip", "ar", "cab", "cpio", "empty", "iso9660", "lha", "rar", "tar", "warc", "xar", "zip" };
  return formats;
}

ArchiveOption to_archive_option(const TraverserOptions &options) {
  ArchiveOption converted;
  converted.passphrases = options.passphrases;
  converted.formats = options.formats;
  converted.metadata_keys.insert(options.metadata_keys.begin(), options.metadata_keys.end());
  return converted;
}

py::object path_entry_to_python(const PathEntry &entry) {
  if (entry.is_single()) {
    return py::str(entry.single_value());
  }
  if (entry.is_multi_volume()) {
    py::list parts;
    for (const auto &part : entry.multi_volume_parts().values) {
      parts.append(py::str(part));
    }
    return std::move(parts);
  }
  return py::none();
}

py::list path_hierarchy_to_python(const PathHierarchy &hierarchy) {
  py::list result;
  for (const auto &component : hierarchy) {
    result.append(path_entry_to_python(component));
  }
  return result;
}

PathEntry python_path_entry_from_object(const py::handle &obj) {
  if (py::isinstance<py::str>(obj)) {
    return PathEntry::single(obj.cast<std::string>());
  }

  py::list sequence;
  try {
    sequence = py::list(py::reinterpret_borrow<py::object>(obj));
  } catch (const py::cast_error &) {
    throw std::invalid_argument("PathEntry must be string or sequence of strings");
  }

  if (sequence.size() == 0) {
    throw std::invalid_argument("PathEntry sequence cannot be empty");
  }

  bool all_strings = true;
  for (py::handle item : sequence) {
    if (!py::isinstance<py::str>(item)) {
      all_strings = false;
      break;
    }
  }

  if (all_strings) {
    std::vector<std::string> parts;
    parts.reserve(static_cast<size_t>(sequence.size()));
    for (py::handle item : sequence) {
      parts.emplace_back(item.cast<std::string>());
    }
    return PathEntry::multi_volume(std::move(parts));
  }

  throw std::invalid_argument("PathEntry sequence must contain only strings");
}

PathHierarchy python_path_hierarchy_from_object(const py::handle &obj) {
  if (py::isinstance<py::str>(obj)) {
    return make_single_path(obj.cast<std::string>());
  }

  py::list sequence;
  try {
    sequence = py::list(py::reinterpret_borrow<py::object>(obj));
  } catch (const py::cast_error &) {
    throw std::invalid_argument("path hierarchy must be string or sequence");
  }

  if (sequence.size() == 0) {
    throw std::invalid_argument("path hierarchy cannot be empty");
  }

  PathHierarchy hierarchy;
  hierarchy.reserve(static_cast<size_t>(sequence.size()));
  for (py::handle component : sequence) {
    hierarchy.emplace_back(python_path_entry_from_object(component));
  }
  return hierarchy;
}

std::vector<PathHierarchy> python_normalize_paths(const py::object &paths_obj) {
  if (py::isinstance<py::str>(paths_obj)) {
    return { make_single_path(paths_obj.cast<std::string>()) };
  }

  py::list path_list;
  try {
    path_list = py::list(paths_obj);
  } catch (const py::cast_error &) {
    throw std::invalid_argument("paths must be a string or a sequence of path hierarchies");
  }

  if (path_list.size() == 0) {
    throw std::invalid_argument("paths cannot be empty");
  }

  std::vector<PathHierarchy> result;
  result.reserve(static_cast<size_t>(path_list.size()));
  for (py::handle item : path_list) {
    result.emplace_back(python_path_hierarchy_from_object(item));
  }

  return result;
}

py::dict entry_fault_to_python(const EntryFault &fault) {
  py::dict result;
  result[py::str("message")] = py::str(fault.message);
  result[py::str("errno")] = py::int_(fault.errno_value);
  result[py::str("hierarchy")] = path_hierarchy_to_python(fault.hierarchy);
  if (!fault.hierarchy.empty()) {
    result[py::str("path")] = py::str(hierarchy_display(fault.hierarchy));
  } else {
    result[py::str("path")] = py::str("");
  }
  return result;
}

FaultCallback make_python_fault_callback(const py::object &callable) {
  if (callable.is_none()) {
    return {};
  }
  if (!PyCallable_Check(callable.ptr())) {
    throw py::type_error("fault callback must be callable");
  }
  py::function func = py::reinterpret_borrow<py::function>(callable);
  return [func = std::move(func)](const EntryFault &fault) {
    py::gil_scoped_acquire gil;
    func(entry_fault_to_python(fault));
  };
}

class PyUserStream : public MultiVolumeStreamBase {
public:
  PyUserStream(PathHierarchy hierarchy, bool supports_seek)
      : MultiVolumeStreamBase(std::move(hierarchy), supports_seek)
      , active_io_(py::none())
      , io_seekable_(false)
      , io_tellable_(false)
      , io_closeable_(false)
      , self_reference_(nullptr) {}
  
    ~PyUserStream() override {
      py::gil_scoped_acquire gil;
      release_active_io();
      if (self_reference_ != nullptr) {
        Py_DECREF(self_reference_);
        self_reference_ = nullptr;
      }
    }

    void retain_python_owner(const py::object &owner) {
      PyObject *new_owner = owner.ptr();
      if (new_owner == self_reference_) {
        return;
      }
      Py_XINCREF(new_owner);
      if (self_reference_ != nullptr) {
        Py_DECREF(self_reference_);
      }
      self_reference_ = new_owner;
    }

protected:
  void open_single_part(const PathHierarchy &single_part) override {
    py::gil_scoped_acquire gil;
    py::object open_io = py::get_override(this, "open_part_io");
    if (open_io) {
      py::object result = open_io(path_hierarchy_to_python(single_part));
      if (result.is_none()) {
        throw py::value_error("open_part_io must return an IO-like object");
      }
      activate_io(std::move(result));
      return;
    }

    py::object override = py::get_override(this, "open_part");
    if (!override) {
      throw py::type_error("Stream subclasses must implement open_part_io() or open_part()");
    }
    override(path_hierarchy_to_python(single_part));
  }

  void close_single_part() override {
    py::gil_scoped_acquire gil;
    release_active_io();
    py::object override = py::get_override(this, "close_part");
    if (override) {
      override();
    }
  }

  ssize_t read_from_single_part(void *buffer, size_t size) override {
    if (size == 0) {
      return 0;
    }
    py::gil_scoped_acquire gil;
    if (!active_io_.is_none()) {
      py::object result = active_io_.attr("read")(py::int_(size));
      if (result.is_none()) {
        return 0;
      }
      py::bytes data = py::reinterpret_borrow<py::bytes>(py::bytes(result));
      Py_ssize_t length = PyBytes_Size(data.ptr());
      if (length < 0) {
        throw py::error_already_set();
      }
      if (length == 0) {
        return 0;
      }
      char *raw = PyBytes_AsString(data.ptr());
      if (!raw) {
        throw py::error_already_set();
      }
      std::memcpy(buffer, raw, static_cast<size_t>(length));
      return static_cast<ssize_t>(length);
    }

    py::object override = py::get_override(this, "read_part");
    if (!override) {
      throw py::type_error("Stream subclasses must implement open_part_io() or read_part(size)");
    }
    py::object result = override(py::int_(size));
    if (result.is_none()) {
      return 0;
    }
    py::bytes data = py::reinterpret_borrow<py::bytes>(py::bytes(result));
    Py_ssize_t length = PyBytes_Size(data.ptr());
    if (length < 0) {
      throw py::error_already_set();
    }
    if (length == 0) {
      return 0;
    }
    char *raw = PyBytes_AsString(data.ptr());
    if (!raw) {
      throw py::error_already_set();
    }
    std::memcpy(buffer, raw, static_cast<size_t>(length));
    return static_cast<ssize_t>(length);
  }

  int64_t seek_within_single_part(int64_t offset, int whence) override {
    py::gil_scoped_acquire gil;
    if (!active_io_.is_none() && io_seekable_) {
      py::object result = active_io_.attr("seek")(py::int_(offset), py::int_(whence));
      return result.cast<int64_t>();
    }
    py::object override = py::get_override(this, "seek_part");
    if (!override) {
      return -1;
    }
    py::object result = override(py::int_(offset), py::int_(whence));
    return result.cast<int64_t>();
  }

  int64_t size_of_single_part(const PathHierarchy &single_part) override {
    py::gil_scoped_acquire gil;
    if (!active_io_.is_none()) {
      if (io_seekable_ && io_tellable_) {
        py::object current = active_io_.attr("tell")();
        active_io_.attr("seek")(py::int_(0), py::int_(SEEK_END));
        py::object end_pos = active_io_.attr("tell")();
        active_io_.attr("seek")(current, py::int_(SEEK_SET));
        return end_pos.cast<int64_t>();
      }
    }

    py::object override = py::get_override(this, "part_size");
    if (!override) {
      return -1;
    }
    py::object result = override(path_hierarchy_to_python(single_part));
    return result.cast<int64_t>();
  }

private:
  void activate_io(py::object io) {
    if (!py::hasattr(io, "read")) {
      throw py::type_error("open_part_io must return an object with read()");
    }
    release_active_io();
    active_io_ = std::move(io);
    io_seekable_ = py::hasattr(active_io_, "seek");
    io_tellable_ = py::hasattr(active_io_, "tell");
    io_closeable_ = py::hasattr(active_io_, "close");
    if (can_seek() && io_seekable_) {
      try {
        active_io_.attr("seek")(py::int_(0), py::int_(SEEK_CUR));
      } catch (const py::error_already_set &) {
        PyErr_Clear();
      }
    }
  }

  void release_active_io() {
    if (active_io_.is_none()) {
      return;
    }
    if (io_closeable_) {
      try {
        active_io_.attr("close")();
      } catch (const py::error_already_set &) {
        PyErr_Clear();
      }
    }
    active_io_ = py::none();
    io_seekable_ = false;
    io_tellable_ = false;
    io_closeable_ = false;
  }

  py::object active_io_;
  bool io_seekable_;
  bool io_tellable_;
  bool io_closeable_;
  PyObject *self_reference_;
};

void register_python_stream_factory(const py::object &callable) {
  if (callable.is_none()) {
    set_root_stream_factory(RootStreamFactory{});
    return;
  }
  if (!PyCallable_Check(callable.ptr())) {
    throw py::type_error("stream factory must be callable");
  }

  py::function func = py::reinterpret_borrow<py::function>(callable);
  RootStreamFactory factory = [func](const PathHierarchy &hierarchy) -> std::shared_ptr<IDataStream> {
    py::gil_scoped_acquire gil;
    py::object result = func(path_hierarchy_to_python(hierarchy));
    if (result.is_none()) {
      return nullptr;
    }
    if (!py::isinstance<PyUserStream>(result)) {
      throw py::type_error("stream factory must return None or an archive_r.Stream instance");
    }
    auto stream = result.cast<std::shared_ptr<PyUserStream>>();
    stream->retain_python_owner(result);
    if (!hierarchies_equal(stream->source_hierarchy(), hierarchy)) {
      throw py::value_error("Stream subclass must be initialized with the hierarchy provided to the factory");
    }
    return stream;
  };

  set_root_stream_factory(factory);
}

// Entry wrapper with cached data and lazy payload recreation
class PyEntry {
public:
  PyEntry(Entry &entry, const ArchiveOption &archive_options)
      : path_(entry.path())
      , name_(entry.name())
      , size_(static_cast<int64_t>(entry.size()))
      , depth_(static_cast<int>(entry.depth()))
      , is_file_(entry.is_file())
      , is_directory_(entry.is_directory())
      , path_hierarchy_py_(path_hierarchy_to_python(entry.path_hierarchy()))
      , path_hierarchy_native_(entry.path_hierarchy())
      , metadata_map_(entry.metadata())
      , archive_options_(archive_options)
      , live_entry_(&entry)
      , descent_enabled_(entry.descent_enabled()) {}

  std::string path() const { return path_; }

  py::object path_hierarchy() const { return path_hierarchy_py_; }

  std::string name() const { return name_; }

  int64_t size() const { return size_; }

  int depth() const { return depth_; }

  bool is_file() const { return is_file_; }

  bool is_directory() const { return is_directory_; }

  void set_descent(bool enabled) {
    if (!live_entry_) {
      throw py::value_error("entry is no longer valid for set_descent");
    }
    live_entry_->set_descent(enabled);
    descent_enabled_ = enabled;
  }

  bool descent_enabled() const { return descent_enabled_; }

  void set_multi_volume_group(const std::string &base_name, std::optional<std::string> order = std::nullopt) {
    if (!live_entry_) {
      throw py::value_error("entry is no longer valid for set_multi_volume_group");
    }

    MultiVolumeGroupOptions options;
    if (order) {
      std::string normalized = *order;
      for (char &ch : normalized) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      }
      if (normalized == "given") {
        options.ordering = PathEntry::Parts::Ordering::Given;
      } else if (normalized == "natural") {
        options.ordering = PathEntry::Parts::Ordering::Natural;
      } else {
        throw std::invalid_argument("order must be 'natural' or 'given'");
      }
    }

    live_entry_->set_multi_volume_group(base_name, options);
  }

  py::bytes read(std::optional<ssize_t> size = std::nullopt) {
    Entry &entry_ref = entry_for_read();

    const ssize_t requested = size.has_value() ? *size : -1;
    if (requested == 0) {
      return py::bytes();
    }

    if (requested > 0) {
      const uint64_t max_chunk = static_cast<uint64_t>(std::numeric_limits<size_t>::max());
      if (static_cast<uint64_t>(requested) > max_chunk) {
        throw py::value_error("Requested size exceeds platform limits");
      }
      const auto as_size = static_cast<size_t>(requested);
      std::vector<uint8_t> buffer(as_size);
      ssize_t bytes_read = entry_ref.read(buffer.data(), buffer.size());
      if (bytes_read < 0) {
        throw py::value_error("Failed to read entry data");
      }
      return py::bytes(reinterpret_cast<const char *>(buffer.data()), static_cast<size_t>(bytes_read));
    }

    std::string aggregate;
    const uint64_t reported_size = static_cast<uint64_t>(size_);
    if (reported_size > 0 && reported_size <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
      aggregate.reserve(static_cast<size_t>(reported_size));
    }

    std::vector<uint8_t> chunk(64 * 1024);
    while (true) {
      ssize_t bytes_read = entry_ref.read(chunk.data(), chunk.size());
      if (bytes_read < 0) {
        throw py::value_error("Failed to read entry data");
      }
      if (bytes_read == 0) {
        break;
      }
      aggregate.append(reinterpret_cast<const char *>(chunk.data()), static_cast<size_t>(bytes_read));
    }

    return py::bytes(aggregate);
  }

  void bind_live_entry(Entry *entry) { live_entry_ = entry; }

  void release_live_entry() { live_entry_ = nullptr; }

  std::string repr() const { return "<Entry path='" + path_ + "' size=" + std::to_string(size_) + " depth=" + std::to_string(depth_) + ">"; }

  py::dict metadata() const {
    py::dict result;
    for (const auto &kv : metadata_map_) {
      result[py::str(kv.first)] = convert_metadata_value(kv.second);
    }
    return result;
  }

  py::object metadata_value(const std::string &key) const {
    const auto it = metadata_map_.find(key);
    if (it == metadata_map_.end()) {
      return py::none();
    }
    return convert_metadata_value(it->second);
  }

private:
  Entry &entry_for_read() {
    if (live_entry_) {
      return *live_entry_;
    }
    ensure_detached_entry();
    if (!detached_entry_) {
      throw py::value_error("entry payload is no longer available");
    }
    return *detached_entry_;
  }

  void ensure_detached_entry() {
    if (detached_entry_) {
      return;
    }
    detached_orchestrator_ = std::make_shared<ArchiveStackOrchestrator>(archive_options_);
    if (!detached_orchestrator_->synchronize_to_hierarchy(path_hierarchy_native_)) {
      detached_orchestrator_.reset();
      throw py::value_error("Failed to reopen entry payload");
    }
    detached_entry_ = Entry::create(path_hierarchy_native_, detached_orchestrator_, descent_enabled_);
  }

  // Cached data
  std::string path_;
  std::string name_;
  int64_t size_;
  int depth_;
  bool is_file_;
  bool is_directory_;
  py::object path_hierarchy_py_;
  PathHierarchy path_hierarchy_native_;
  EntryMetadataMap metadata_map_;
  ArchiveOption archive_options_;

  Entry *live_entry_;
  std::unique_ptr<Entry> detached_entry_;
  std::shared_ptr<ArchiveStackOrchestrator> detached_orchestrator_;
  bool descent_enabled_;

  static py::object convert_metadata_value(const EntryMetadataValue &value) {
    struct Visitor {
      py::object operator()(std::monostate) const { return py::none(); }
      py::object operator()(bool v) const { return py::bool_(v); }
      py::object operator()(int64_t v) const { return py::int_(v); }
      py::object operator()(uint64_t v) const { return py::int_(v); }
      py::object operator()(const std::string &v) const { return py::str(v); }
      py::object operator()(const std::vector<uint8_t> &v) const {
        if (v.empty()) {
          return py::bytes();
        }
        return py::bytes(reinterpret_cast<const char *>(v.data()), v.size());
      }
      py::object operator()(const EntryMetadataTime &v) const {
        py::dict d;
        d["seconds"] = v.seconds;
        d["nanoseconds"] = v.nanoseconds;
        return d;
      }
      py::object operator()(const EntryMetadataDeviceNumbers &v) const {
        py::dict d;
        d["major"] = v.major;
        d["minor"] = v.minor;
        return d;
      }
      py::object operator()(const EntryMetadataFileFlags &v) const {
        py::dict d;
        d["set"] = v.set;
        d["clear"] = v.clear;
        return d;
      }
      py::object operator()(const std::vector<EntryMetadataXattr> &vec) const {
        py::list list;
        for (const auto &item : vec) {
          py::dict d;
          d["name"] = item.name;
          if (item.value.empty()) {
            d["value"] = py::bytes();
          } else {
            d["value"] = py::bytes(reinterpret_cast<const char *>(item.value.data()), item.value.size());
          }
          list.append(std::move(d));
        }
        return list;
      }
      py::object operator()(const std::vector<EntryMetadataSparseChunk> &vec) const {
        py::list list;
        for (const auto &item : vec) {
          py::dict d;
          d["offset"] = item.offset;
          d["length"] = item.length;
          list.append(std::move(d));
        }
        return list;
      }
      py::object operator()(const std::vector<EntryMetadataDigest> &vec) const {
        py::list list;
        for (const auto &item : vec) {
          py::dict d;
          d["algorithm"] = item.algorithm;
          if (item.value.empty()) {
            d["value"] = py::bytes();
          } else {
            d["value"] = py::bytes(reinterpret_cast<const char *>(item.value.data()), item.value.size());
          }
          list.append(std::move(d));
        }
        return list;
      }
    } visitor;

    return std::visit(visitor, value);
  }
};

// Python iterator wrapper for Traverser
class PyTraverser {
public:
  PyTraverser(py::object paths, std::optional<std::vector<std::string>> passphrases = std::nullopt, std::optional<std::vector<std::string>> formats = std::nullopt,
              std::optional<std::vector<std::string>> metadata_keys = std::nullopt, std::optional<bool> descend_archives = std::nullopt)
      : traverser_options_(build_options(passphrases, formats, metadata_keys, descend_archives))
      , archive_options_snapshot_(to_archive_option(traverser_options_))
      , traverser(python_normalize_paths(paths), traverser_options_)
      , it(traverser.end()) {
  }

  // Iterator protocol
  PyTraverser &iter() {
    reset_iterator();
    return *this;
  }

  py::object next() {
    initialize_iterator_if_needed();

    if (_advance_pending) {
      if (_last_entry) {
        _last_entry->release_live_entry();
        _last_entry.reset();
      }
      ++it;
      _advance_pending = false;
    }

    if (it == traverser.end()) {
      _iterator_initialized = false;
      throw py::stop_iteration();
    }
    Entry &entry = *it;

    // Create PyEntry wrapper with entry copy
    auto py_entry = std::make_shared<PyEntry>(entry, archive_options_snapshot_);
    py_entry->bind_live_entry(&entry);
    _last_entry = py_entry;

    // Defer increment until next call so user callbacks execute first
    _advance_pending = true;

    return py::cast(py_entry);
  }

  // Context manager protocol
  PyTraverser &enter() {
    reset_iterator();
    return *this;
  }

  void exit(py::object exc_type, py::object exc_value, py::object traceback) {
    _iterator_initialized = false;
    _advance_pending = false;
    if (_last_entry) {
      _last_entry->release_live_entry();
      _last_entry.reset();
    }
    it = traverser.end();
  }

private:
  static TraverserOptions build_options(const std::optional<std::vector<std::string>> &passphrases, const std::optional<std::vector<std::string>> &formats,
                                        const std::optional<std::vector<std::string>> &metadata_keys, const std::optional<bool> &descend_archives) {
    TraverserOptions options;
    if (passphrases) {
      options.passphrases = *passphrases;
    }
    if (formats) {
      options.formats = *formats;
    } else {
      options.formats = standard_formats();
    }
    if (metadata_keys) {
      options.metadata_keys = *metadata_keys;
    }
    if (descend_archives) {
      options.descend_archives = *descend_archives;
    }
    return options;
  }

  TraverserOptions traverser_options_;
  ArchiveOption archive_options_snapshot_;
  Traverser traverser;
  Traverser::Iterator it;
  bool _iterator_initialized = false;
  bool _advance_pending = false;
  std::shared_ptr<PyEntry> _last_entry;

  void reset_iterator() {
    if (_last_entry) {
      _last_entry->release_live_entry();
      _last_entry.reset();
    }
    it = traverser.begin();
    _iterator_initialized = true;
    _advance_pending = false;
  }

  void initialize_iterator_if_needed() {
    if (!_iterator_initialized) {
      reset_iterator();
    }
  }

};

} // namespace

PYBIND11_MODULE(archive_r, m) {
  m.doc() = "Python bindings for archive_r library";

  const auto &formats = standard_formats();
  py::tuple formats_tuple(formats.size());
  for (size_t i = 0; i < formats.size(); ++i) {
    formats_tuple[i] = py::str(formats[i]);
  }
  m.attr("STANDARD_FORMATS") = formats_tuple;
  m.attr("SAFE_FORMATS") = formats_tuple;

    m.def("register_stream_factory", &register_python_stream_factory, py::arg("factory") = py::none(),
      "Register a callable returning archive_r.Stream instances for custom root streams. Pass None to reset.");

  m.def("on_fault",
        [](const py::object &callback) { register_fault_callback(make_python_fault_callback(callback)); }, py::arg("callback") = py::none(),
        "Register or clear the global EntryFault callback (None clears)");

  py::class_<PyUserStream, std::shared_ptr<PyUserStream>>(m, "Stream")
      .def(py::init([](py::object hierarchy, py::object supports_seek_obj) {
             auto path = python_path_hierarchy_from_object(hierarchy);
             bool supports_seek = supports_seek_obj.is_none() ? true : supports_seek_obj.cast<bool>();
             return std::make_shared<PyUserStream>(std::move(path), supports_seek);
           }),
           py::arg("hierarchy"), py::arg("supports_seek") = py::none(),
           "Base class for custom multi-volume streams returned from register_stream_factory.");

  // Entry class
  py::class_<PyEntry, std::shared_ptr<PyEntry>>(m, "Entry")
      .def_property_readonly("path", &PyEntry::path, "Get full path of the entry")
      .def_property_readonly("path_hierarchy", &PyEntry::path_hierarchy, "Get path hierarchy components for the entry")
      .def_property_readonly("name", &PyEntry::name, "Get name of the entry")
      .def_property_readonly("size", &PyEntry::size, "Get size of the entry in bytes")
      .def_property_readonly("depth", &PyEntry::depth, "Get depth of the entry in archive hierarchy")
      .def_property_readonly("is_file", &PyEntry::is_file, "Check if entry is a file")
      .def_property_readonly("is_directory", &PyEntry::is_directory, "Check if entry is a directory")
      .def_property("descent_enabled", &PyEntry::descent_enabled, &PyEntry::set_descent, "Get or set whether this entry will be descended into")
      .def("set_descent", &PyEntry::set_descent, py::arg("enabled") = true, "Enable or disable descent into this entry")
      .def("set_multi_volume_group", &PyEntry::set_multi_volume_group, py::arg("base_name"), py::arg("order") = py::none(),
           "Mark this entry as part of a multi-volume archive group.\n"
           "Pass order='given' to preserve user-provided part order.")
       .def("read", &PyEntry::read, py::arg("size") = py::none(),
         "Read up to size bytes from the entry (default: read until EOF)")
      .def_property_readonly("metadata", &PyEntry::metadata, "Get captured metadata as a dictionary")
      .def("metadata_value", &PyEntry::metadata_value, py::arg("key"), "Get a metadata value by key or None if unavailable")
      .def("__repr__", &PyEntry::repr);

  // Traverser class
  py::class_<PyTraverser>(m, "Traverser")
      .def(py::init<py::object, std::optional<std::vector<std::string>>, std::optional<std::vector<std::string>>, std::optional<std::vector<std::string>>,
                std::optional<bool>>(),
          py::arg("paths"), py::arg("passphrases") = py::none(), py::arg("formats") = py::none(), py::arg("metadata_keys") = py::none(),
          py::arg("descend_archives") = py::none(),
          "Create a traverser for the given archive paths with optional passphrases, format filters, metadata key selection, and default descent control")
      .def(py::init([](py::list path_hierarchy, std::optional<std::vector<std::string>> passphrases, std::optional<std::vector<std::string>> formats,
                       std::optional<std::vector<std::string>> metadata_keys, std::optional<bool> descend_archives) {
             py::list paths;
             paths.append(path_hierarchy);
             return std::unique_ptr<PyTraverser>(new PyTraverser(paths, std::move(passphrases), std::move(formats), std::move(metadata_keys), descend_archives));
           }),
           py::arg("path_hierarchy"), py::arg("passphrases") = py::none(), py::arg("formats") = py::none(), py::arg("metadata_keys") = py::none(),
           py::arg("descend_archives") = py::none(),
           "Create a traverser for a single PathHierarchy (pass as a list describing the hierarchy).")
      .def("__iter__", &PyTraverser::iter, py::return_value_policy::reference_internal)
      .def("__next__", &PyTraverser::next)
      .def("__enter__", &PyTraverser::enter, py::return_value_policy::reference_internal)
      .def("__exit__", &PyTraverser::exit);

  m.attr("__version__") = ARCHIVE_R_VERSION;
}
