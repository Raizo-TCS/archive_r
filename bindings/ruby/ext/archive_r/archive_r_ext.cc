// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/data_stream.h"
#include "archive_r/entry.h"
#include "archive_r/entry_fault.h"
#include "archive_r/multi_volume_stream_base.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <ruby.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <limits>
#include <optional>

using namespace archive_r;

// Ruby module and class references
static VALUE mArchive_r;
static VALUE cTraverser;
static VALUE cEntry;
static ID rb_id_read_method;
static ID rb_id_seek_method;
static ID rb_id_tell_method;
static ID rb_id_eof_method;
static ID rb_id_close_method;
static ID rb_id_size_method;
static ID rb_id_call_method;
static ID rb_id_open_part_method;
static ID rb_id_close_part_method;
static ID rb_id_read_part_method;
static ID rb_id_seek_part_method;
static ID rb_id_part_size_method;
static ID rb_id_open_part_io_method;
static ID rb_id_close_part_io_method;
struct RubyCallbackHolder;
static std::shared_ptr<RubyCallbackHolder> g_stream_factory_callback;

// Helper: Convert Ruby string to C++ string
static VALUE cStream;
static PathHierarchy rb_value_to_path_hierarchy(VALUE value);
static std::string rb_string_to_cpp(VALUE rb_str) {
  Check_Type(rb_str, T_STRING);
  return std::string(RSTRING_PTR(rb_str), RSTRING_LEN(rb_str));
}

static void archive_r_cleanup(VALUE) {
  register_fault_callback(FaultCallback{});
  set_root_stream_factory(RootStreamFactory{});
  g_stream_factory_callback.reset();
}

// Helper: Convert C++ string to Ruby string
static VALUE cpp_string_to_rb(const std::string &str) { return rb_str_new(str.c_str(), str.length()); }

static VALUE path_entry_to_rb(const PathEntry &entry) {
  if (entry.is_single()) {
    return cpp_string_to_rb(entry.single_value());
  }
  if (entry.is_multi_volume()) {
    const auto &parts = entry.multi_volume_parts().values;
    VALUE array = rb_ary_new_capa(parts.size());
    for (const auto &part : parts) {
      rb_ary_push(array, cpp_string_to_rb(part));
    }
    return array;
  }
  return Qnil;
}

static VALUE path_hierarchy_to_rb(const PathHierarchy &hierarchy) {
  VALUE array = rb_ary_new_capa(hierarchy.size());
  for (const auto &component : hierarchy) {
    rb_ary_push(array, path_entry_to_rb(component));
  }
  return array;
}

struct RubyCallbackHolder {
  explicit RubyCallbackHolder(VALUE proc)
      : proc_value(proc) {
    rb_gc_register_address(&proc_value);
  }

  ~RubyCallbackHolder() { rb_gc_unregister_address(&proc_value); }

  VALUE proc_value;
};

class RubyUserStream : public MultiVolumeStreamBase {
public:
  RubyUserStream(VALUE ruby_stream, PathHierarchy hierarchy, std::optional<bool> seekable_override)
      : MultiVolumeStreamBase(std::move(hierarchy), determine_seek_support(ruby_stream, seekable_override))
      , _ruby_stream(ruby_stream)
      , _has_close(rb_respond_to(ruby_stream, rb_id_close_part_method))
      , _has_seek(rb_respond_to(ruby_stream, rb_id_seek_part_method))
      , _has_size(rb_respond_to(ruby_stream, rb_id_part_size_method))
      , _has_open_part_io(rb_respond_to(ruby_stream, rb_id_open_part_io_method))
      , _has_close_part_io(rb_respond_to(ruby_stream, rb_id_close_part_io_method))
      , _active_io(Qnil)
      , _active_io_seekable(false)
      , _active_io_tellable(false)
      , _active_io_has_close(false)
      , _active_io_has_size(false) {
    ensure_required_methods(ruby_stream);
    rb_gc_register_address(&_ruby_stream);
  }

  ~RubyUserStream() override {
    release_active_io_resources(false);
    rb_gc_unregister_address(&_ruby_stream);
  }

protected:
  void open_single_part(const PathHierarchy &single_part) override {
    if (_has_open_part_io) {
      VALUE arg = path_hierarchy_to_rb(single_part);
      VALUE io = rb_funcall(_ruby_stream, rb_id_open_part_io_method, 1, arg);
      if (NIL_P(io)) {
        rb_raise(rb_eRuntimeError, "open_part_io must return an IO-like object");
      }
      activate_io(io);
      return;
    }

    VALUE arg = path_hierarchy_to_rb(single_part);
    rb_funcall(_ruby_stream, rb_id_open_part_method, 1, arg);
  }

  void close_single_part() override {
    if (_has_open_part_io) {
      release_active_io_resources(true);
      if (_has_close_part_io) {
        rb_funcall(_ruby_stream, rb_id_close_part_io_method, 0);
      }
      return;
    }

    if (_has_close) {
      rb_funcall(_ruby_stream, rb_id_close_part_method, 0);
    }
  }

  ssize_t read_from_single_part(void *buffer, size_t size) override {
    if (size == 0) {
      return 0;
    }

    if (_has_open_part_io) {
      if (_active_io == Qnil) {
        rb_raise(rb_eRuntimeError, "open_part_io must return an IO before reading");
      }
      VALUE result = rb_funcall(_active_io, rb_id_read_method, 1, SIZET2NUM(size));
      if (NIL_P(result)) {
        return 0;
      }
      Check_Type(result, T_STRING);
      const ssize_t length = static_cast<ssize_t>(RSTRING_LEN(result));
      if (length <= 0) {
        return 0;
      }
      std::memcpy(buffer, RSTRING_PTR(result), static_cast<size_t>(length));
      return length;
    }

    VALUE result = rb_funcall(_ruby_stream, rb_id_read_part_method, 1, SIZET2NUM(size));
    if (NIL_P(result)) {
      return 0;
    }
    Check_Type(result, T_STRING);
    const ssize_t length = static_cast<ssize_t>(RSTRING_LEN(result));
    if (length <= 0) {
      return 0;
    }
    std::memcpy(buffer, RSTRING_PTR(result), static_cast<size_t>(length));
    return length;
  }

  int64_t seek_within_single_part(int64_t offset, int whence) override {
    if (_has_open_part_io && _active_io != Qnil && _active_io_seekable) {
      VALUE result = rb_funcall(_active_io, rb_id_seek_method, 2, LL2NUM(offset), INT2NUM(whence));
      return NUM2LL(result);
    }

    if (!_has_seek) {
      return -1;
    }
    VALUE result = rb_funcall(_ruby_stream, rb_id_seek_part_method, 2, LL2NUM(offset), INT2NUM(whence));
    return NUM2LL(result);
  }

  int64_t size_of_single_part(const PathHierarchy &single_part) override {
    if (_has_open_part_io && _active_io != Qnil) {
      if (_active_io_has_size) {
        VALUE result = rb_funcall(_active_io, rb_id_size_method, 0);
        return NUM2LL(result);
      }
      if (_active_io_seekable && _active_io_tellable) {
        VALUE current = rb_funcall(_active_io, rb_id_tell_method, 0);
        rb_funcall(_active_io, rb_id_seek_method, 2, LL2NUM(0), INT2NUM(SEEK_END));
        VALUE end_pos = rb_funcall(_active_io, rb_id_tell_method, 0);
        rb_funcall(_active_io, rb_id_seek_method, 2, current, INT2NUM(SEEK_SET));
        return NUM2LL(end_pos);
      }
    }

    if (!_has_size) {
      return -1;
    }
    VALUE arg = path_hierarchy_to_rb(single_part);
    VALUE result = rb_funcall(_ruby_stream, rb_id_part_size_method, 1, arg);
    return NUM2LL(result);
  }

private:
  static bool determine_seek_support(VALUE ruby_stream, const std::optional<bool> &override_flag) {
    if (override_flag.has_value()) {
      return *override_flag;
    }
    if (rb_respond_to(ruby_stream, rb_id_seek_part_method)) {
      return true;
    }
    if (rb_respond_to(ruby_stream, rb_id_open_part_io_method)) {
      return true;
    }
    return false;
  }

  static void ensure_required_methods(VALUE ruby_stream) {
    if (!rb_respond_to(ruby_stream, rb_id_open_part_method) && !rb_respond_to(ruby_stream, rb_id_open_part_io_method)) {
      rb_raise(rb_eNotImpError, "Stream subclasses must implement #open_part_io or #open_part");
    }
    if (!rb_respond_to(ruby_stream, rb_id_read_part_method) && !rb_respond_to(ruby_stream, rb_id_open_part_io_method)) {
      rb_raise(rb_eNotImpError, "Stream subclasses must implement #open_part_io or #read_part");
    }
  }

  void activate_io(VALUE io) {
    if (!rb_respond_to(io, rb_id_read_method)) {
      rb_raise(rb_eTypeError, "open_part_io must return an object responding to #read");
    }
    release_active_io_resources(true);
    _active_io = io;
    rb_gc_register_address(&_active_io);
    _active_io_seekable = rb_respond_to(io, rb_id_seek_method);
    _active_io_tellable = rb_respond_to(io, rb_id_tell_method);
    _active_io_has_close = rb_respond_to(io, rb_id_close_method);
    _active_io_has_size = rb_respond_to(io, rb_id_size_method);
  }

  void release_active_io_resources(bool close_io) {
    if (_active_io == Qnil) {
      return;
    }
    if (close_io && _active_io_has_close) {
      rb_funcall(_active_io, rb_id_close_method, 0);
    }
    rb_gc_unregister_address(&_active_io);
    _active_io = Qnil;
    _active_io_seekable = false;
    _active_io_tellable = false;
    _active_io_has_close = false;
    _active_io_has_size = false;
  }

  VALUE _ruby_stream;
  bool _has_close;
  bool _has_seek;
  bool _has_size;
  bool _has_open_part_io;
  bool _has_close_part_io;
  VALUE _active_io;
  bool _active_io_seekable;
  bool _active_io_tellable;
  bool _active_io_has_close;
  bool _active_io_has_size;
};

struct RubyStreamWrapper {
  std::shared_ptr<RubyUserStream> stream;
};

static void stream_wrapper_free(void *ptr) {
  auto *wrapper = static_cast<RubyStreamWrapper *>(ptr);
  delete wrapper;
}

static size_t stream_wrapper_memsize(const void *ptr) {
  return sizeof(RubyStreamWrapper);
}

static const rb_data_type_t stream_wrapper_type = {
  "ArchiveR::Stream",
  {
    nullptr,
    stream_wrapper_free,
    stream_wrapper_memsize,
  },
  nullptr,
  nullptr,
  RUBY_TYPED_FREE_IMMEDIATELY,
};

static RubyStreamWrapper *get_stream_wrapper(VALUE self) {
  RubyStreamWrapper *wrapper = nullptr;
  TypedData_Get_Struct(self, RubyStreamWrapper, &stream_wrapper_type, wrapper);
  if (!wrapper) {
    rb_raise(rb_eRuntimeError, "invalid Stream wrapper");
  }
  return wrapper;
}

static VALUE stream_allocate(VALUE klass) {
  auto *wrapper = new RubyStreamWrapper();
  wrapper->stream.reset();
  return TypedData_Wrap_Struct(klass, &stream_wrapper_type, wrapper);
}

static VALUE stream_initialize(int argc, VALUE *argv, VALUE self) {
  RubyStreamWrapper *wrapper = get_stream_wrapper(self);
  if (wrapper->stream) {
    rb_raise(rb_eRuntimeError, "Stream already initialized");
  }
  VALUE hierarchy_value = Qnil;
  VALUE opts = Qnil;
  rb_scan_args(argc, argv, "11", &hierarchy_value, &opts);
  PathHierarchy hierarchy = rb_value_to_path_hierarchy(hierarchy_value);
  std::optional<bool> seekable_override;
  if (!NIL_P(opts)) {
    Check_Type(opts, T_HASH);
    static ID id_seekable = rb_intern("seekable");
    VALUE seekable_value = rb_hash_aref(opts, ID2SYM(id_seekable));
    if (!NIL_P(seekable_value)) {
      seekable_override = RTEST(seekable_value);
    }
  }
  wrapper->stream = std::make_shared<RubyUserStream>(self, std::move(hierarchy), seekable_override);
  return self;
}

static VALUE entry_fault_to_rb(const EntryFault &fault) {
  VALUE hash = rb_hash_new();
  static ID id_message = rb_intern("message");
  static ID id_errno = rb_intern("errno");
  static ID id_hierarchy = rb_intern("hierarchy");
  static ID id_path = rb_intern("path");

  rb_hash_aset(hash, ID2SYM(id_message), cpp_string_to_rb(fault.message));
  rb_hash_aset(hash, ID2SYM(id_errno), INT2NUM(fault.errno_value));
  rb_hash_aset(hash, ID2SYM(id_hierarchy), path_hierarchy_to_rb(fault.hierarchy));
  std::string path_string = fault.hierarchy.empty() ? std::string() : hierarchy_display(fault.hierarchy);
  rb_hash_aset(hash, ID2SYM(id_path), cpp_string_to_rb(path_string));
  return hash;
}

static FaultCallback make_ruby_fault_callback(VALUE callable) {
  if (NIL_P(callable)) {
    return {};
  }

  static ID id_call = rb_intern("call");
  if (!rb_respond_to(callable, id_call)) {
    rb_raise(rb_eTypeError, "fault callback must respond to #call");
  }

  auto holder = std::make_shared<RubyCallbackHolder>(callable);

  return [holder](const EntryFault &fault) {
    struct InvokePayload {
      std::shared_ptr<RubyCallbackHolder> holder;
      VALUE fault_hash;
    } payload{ holder, entry_fault_to_rb(fault) };

    auto invoke = [](VALUE data) -> VALUE {
      auto *info = reinterpret_cast<InvokePayload *>(data);
      static ID id_call_inner = rb_intern("call");
      return rb_funcall(info->holder->proc_value, id_call_inner, 1, info->fault_hash);
    };

    int state = 0;
    rb_protect(invoke, reinterpret_cast<VALUE>(&payload), &state);
    if (state != 0) {
      rb_jump_tag(state);
    }
    return;
  };
}

// Helper: Convert Ruby hash options into TraverserOptions
static void populate_traverser_options(VALUE opts, TraverserOptions &options) {
  if (NIL_P(opts)) {
    return;
  }

  Check_Type(opts, T_HASH);

  static ID id_passphrases = rb_intern("passphrases");
  static ID id_formats = rb_intern("formats");
  static ID id_metadata_keys = rb_intern("metadata_keys");
  static ID id_descend_archives = rb_intern("descend_archives");

  VALUE passphrases_val = rb_hash_aref(opts, ID2SYM(id_passphrases));
  if (!NIL_P(passphrases_val)) {
    Check_Type(passphrases_val, T_ARRAY);
    long len = RARRAY_LEN(passphrases_val);
    options.passphrases.reserve(len);
    for (long i = 0; i < len; ++i) {
      VALUE item = rb_ary_entry(passphrases_val, i);
      options.passphrases.push_back(rb_string_to_cpp(StringValue(item)));
    }
  }

  VALUE formats_val = rb_hash_aref(opts, ID2SYM(id_formats));
  if (!NIL_P(formats_val)) {
    Check_Type(formats_val, T_ARRAY);
    long len = RARRAY_LEN(formats_val);
    options.formats.reserve(len);
    for (long i = 0; i < len; ++i) {
      VALUE item = rb_ary_entry(formats_val, i);
      options.formats.push_back(rb_string_to_cpp(StringValue(item)));
    }
  }

  VALUE metadata_val = rb_hash_aref(opts, ID2SYM(id_metadata_keys));
  if (!NIL_P(metadata_val)) {
    Check_Type(metadata_val, T_ARRAY);
    long len = RARRAY_LEN(metadata_val);
    options.metadata_keys.reserve(len);
    for (long i = 0; i < len; ++i) {
      VALUE item = rb_ary_entry(metadata_val, i);
      options.metadata_keys.push_back(rb_string_to_cpp(StringValue(item)));
    }
  }

  VALUE descend_val = rb_hash_aref(opts, ID2SYM(id_descend_archives));
  if (!NIL_P(descend_val)) {
    options.descend_archives = RTEST(descend_val);
  }
}

static PathEntry rb_value_to_path_entry(VALUE value);

static PathHierarchy rb_value_to_path_hierarchy(VALUE value) {
  if (RB_TYPE_P(value, T_STRING)) {
    return make_single_path(rb_string_to_cpp(value));
  }

  VALUE array = rb_check_array_type(value);
  if (NIL_P(array)) {
    rb_raise(rb_eTypeError, "path hierarchy must be a String or Array");
  }

  const long length = RARRAY_LEN(array);
  if (length == 0) {
    rb_raise(rb_eArgError, "path hierarchy cannot be empty");
  }

  PathHierarchy hierarchy;
  hierarchy.reserve(static_cast<size_t>(length));
  for (long i = 0; i < length; ++i) {
    VALUE component = rb_ary_entry(array, i);
    hierarchy.emplace_back(rb_value_to_path_entry(component));
  }

  return hierarchy;
}

static PathEntry rb_value_to_path_entry(VALUE value) {
  if (RB_TYPE_P(value, T_STRING)) {
    return PathEntry::single(rb_string_to_cpp(value));
  }

  VALUE array = rb_check_array_type(value);
  if (NIL_P(array)) {
    rb_raise(rb_eTypeError, "PathEntry must be String or Array");
  }

  const long length = RARRAY_LEN(array);
  if (length == 0) {
    rb_raise(rb_eArgError, "PathEntry array cannot be empty");
  }

  bool all_strings = true;
  for (long i = 0; i < length; ++i) {
    VALUE element = rb_ary_entry(array, i);
    if (!RB_TYPE_P(element, T_STRING)) {
      all_strings = false;
      break;
    }
  }

  if (all_strings) {
    std::vector<std::string> parts;
    parts.reserve(static_cast<size_t>(length));
    for (long i = 0; i < length; ++i) {
      parts.emplace_back(rb_string_to_cpp(rb_ary_entry(array, i)));
    }
    return PathEntry::multi_volume(std::move(parts));
  }

  rb_raise(rb_eTypeError, "PathEntry array must contain only Strings");
}

// Helper: Convert Ruby path argument into vector of PathHierarchy
static std::vector<PathHierarchy> rb_paths_to_hierarchies(VALUE paths) {
  if (RB_TYPE_P(paths, T_STRING)) {
    std::vector<PathHierarchy> result;
    result.emplace_back(make_single_path(rb_string_to_cpp(paths)));
    return result;
  }

  VALUE array = rb_check_array_type(paths);
  if (NIL_P(array)) {
    rb_raise(rb_eTypeError, "paths must be a String or an Array");
  }

  const long length = RARRAY_LEN(array);
  if (length == 0) {
    rb_raise(rb_eArgError, "paths cannot be empty");
  }

  std::vector<PathHierarchy> result;
  result.reserve(static_cast<size_t>(length));
  for (long i = 0; i < length; ++i) {
    VALUE item = rb_ary_entry(array, i);
    result.emplace_back(rb_value_to_path_hierarchy(item));
  }

  return result;
}

static VALUE invoke_ruby_stream_factory(const std::shared_ptr<RubyCallbackHolder> &holder, const PathHierarchy &hierarchy) {
  struct FactoryPayload {
    std::shared_ptr<RubyCallbackHolder> holder;
    VALUE arg;
  } payload{ holder, path_hierarchy_to_rb(hierarchy) };

  auto invoke = [](VALUE data) -> VALUE {
    auto *info = reinterpret_cast<FactoryPayload *>(data);
    return rb_funcall(info->holder->proc_value, rb_id_call_method, 1, info->arg);
  };

  int state = 0;
  VALUE result = rb_protect(invoke, reinterpret_cast<VALUE>(&payload), &state);
  if (state != 0) {
    rb_jump_tag(state);
  }
  return result;
}

static std::shared_ptr<IDataStream> stream_from_ruby_value(VALUE value, const PathHierarchy &requested) {
  if (NIL_P(value)) {
    return nullptr;
  }

  if (rb_obj_is_kind_of(value, cStream)) {
    RubyStreamWrapper *wrapper = get_stream_wrapper(value);
    if (!wrapper->stream) {
      rb_raise(rb_eRuntimeError, "Stream instance is not initialized");
    }
    if (!hierarchies_equal(wrapper->stream->source_hierarchy(), requested)) {
      rb_raise(rb_eArgError, "Stream instance must be created with the same hierarchy passed to the factory");
    }
    return wrapper->stream;
  }

  rb_raise(rb_eTypeError, "stream factory must return nil or an Archive_r::Stream instance");
}

// Helper: Convert EntryMetadataValue to Ruby object
static VALUE metadata_value_to_rb(const EntryMetadataValue &value) {
  struct Visitor {
    VALUE
    operator()(std::monostate) const { return Qnil; }
    VALUE
    operator()(bool v) const { return v ? Qtrue : Qfalse; }
    VALUE
    operator()(int64_t v) const { return LL2NUM(v); }
    VALUE
    operator()(uint64_t v) const { return ULL2NUM(v); }
    VALUE
    operator()(const std::string & v) const { return cpp_string_to_rb(v); }
    VALUE
    operator()(const std::vector<uint8_t> &v) const {
      if (v.empty()) {
        return rb_str_new(nullptr, 0);
      }
      return rb_str_new(reinterpret_cast<const char *>(v.data()), v.size());
    }
    VALUE
    operator()(const EntryMetadataTime & v) const {
      VALUE hash = rb_hash_new();
      rb_hash_aset(hash, ID2SYM(rb_intern("seconds")), LL2NUM(v.seconds));
      rb_hash_aset(hash, ID2SYM(rb_intern("nanoseconds")), INT2NUM(v.nanoseconds));
      return hash;
    }
    VALUE
    operator()(const EntryMetadataDeviceNumbers & v) const {
      VALUE hash = rb_hash_new();
      rb_hash_aset(hash, ID2SYM(rb_intern("major")), ULL2NUM(v.major));
      rb_hash_aset(hash, ID2SYM(rb_intern("minor")), ULL2NUM(v.minor));
      return hash;
    }
    VALUE
    operator()(const EntryMetadataFileFlags & v) const {
      VALUE hash = rb_hash_new();
      rb_hash_aset(hash, ID2SYM(rb_intern("set")), ULL2NUM(v.set));
      rb_hash_aset(hash, ID2SYM(rb_intern("clear")), ULL2NUM(v.clear));
      return hash;
    }
    VALUE
    operator()(const std::vector<EntryMetadataXattr> &vec) const {
      VALUE array = rb_ary_new_capa(vec.size());
      for (const auto &item : vec) {
        VALUE hash = rb_hash_new();
        rb_hash_aset(hash, ID2SYM(rb_intern("name")), cpp_string_to_rb(item.name));
        if (item.value.empty()) {
          rb_hash_aset(hash, ID2SYM(rb_intern("value")), rb_str_new(nullptr, 0));
        } else {
          rb_hash_aset(hash, ID2SYM(rb_intern("value")), rb_str_new(reinterpret_cast<const char *>(item.value.data()), item.value.size()));
        }
        rb_ary_push(array, hash);
      }
      return array;
    }
    VALUE
    operator()(const std::vector<EntryMetadataSparseChunk> &vec) const {
      VALUE array = rb_ary_new_capa(vec.size());
      for (const auto &item : vec) {
        VALUE hash = rb_hash_new();
        rb_hash_aset(hash, ID2SYM(rb_intern("offset")), LL2NUM(item.offset));
        rb_hash_aset(hash, ID2SYM(rb_intern("length")), LL2NUM(item.length));
        rb_ary_push(array, hash);
      }
      return array;
    }
    VALUE
    operator()(const std::vector<EntryMetadataDigest> &vec) const {
      VALUE array = rb_ary_new_capa(vec.size());
      for (const auto &item : vec) {
        VALUE hash = rb_hash_new();
        rb_hash_aset(hash, ID2SYM(rb_intern("algorithm")), cpp_string_to_rb(item.algorithm));
        if (item.value.empty()) {
          rb_hash_aset(hash, ID2SYM(rb_intern("value")), rb_str_new(nullptr, 0));
        } else {
          rb_hash_aset(hash, ID2SYM(rb_intern("value")), rb_str_new(reinterpret_cast<const char *>(item.value.data()), item.value.size()));
        }
        rb_ary_push(array, hash);
      }
      return array;
    }
  } visitor;

  return std::visit(visitor, value);
}

static VALUE archive_r_register_stream_factory(int argc, VALUE *argv, VALUE self) {
  VALUE callable = Qnil;
  rb_scan_args(argc, argv, "01", &callable);

  if (!NIL_P(callable) && rb_block_given_p()) {
    rb_raise(rb_eArgError, "provide callable argument or block, not both");
  }

  if (NIL_P(callable) && rb_block_given_p()) {
    callable = rb_block_proc();
  }

  if (NIL_P(callable)) {
    set_root_stream_factory(RootStreamFactory{});
    g_stream_factory_callback.reset();
    return Qnil;
  }

  if (!rb_respond_to(callable, rb_id_call_method)) {
    rb_raise(rb_eTypeError, "stream factory must respond to #call");
  }

  auto holder = std::make_shared<RubyCallbackHolder>(callable);

  RootStreamFactory factory = [holder](const PathHierarchy &hierarchy) -> std::shared_ptr<IDataStream> {
    VALUE result = invoke_ruby_stream_factory(holder, hierarchy);
    return stream_from_ruby_value(result, hierarchy);
  };

  set_root_stream_factory(factory);
  g_stream_factory_callback = holder;
  return Qnil;
}

static VALUE archive_r_on_fault(int argc, VALUE *argv, VALUE self) {
  VALUE callback = Qnil;
  rb_scan_args(argc, argv, "01", &callback);

  if (!NIL_P(callback) && rb_block_given_p()) {
    rb_raise(rb_eArgError, "provide callable argument or block, not both");
  }

  if (NIL_P(callback) && rb_block_given_p()) {
    callback = rb_block_proc();
  }

  FaultCallback cb = make_ruby_fault_callback(callback);
  register_fault_callback(std::move(cb));
  return self;
}

//=============================================================================
// Entry class
//=============================================================================

// EntryWrapper: references an Entry owned by Traverser iterator
struct EntryWrapper {
  Entry *entry_ref;
  std::unique_ptr<Entry> entry_copy;

  EntryWrapper(Entry *ref, std::unique_ptr<Entry> copy)
      : entry_ref(ref)
      , entry_copy(std::move(copy)) {}
};

// Free function for EntryWrapper (wrapper only, Entry owned elsewhere)
static void entry_free(void *ptr) { delete static_cast<EntryWrapper *>(ptr); }

// Wrap Entry pointer in EntryWrapper (does not copy Entry)
static VALUE entry_wrap(Entry &entry) {
  std::unique_ptr<Entry> copy(new Entry(entry));
  EntryWrapper *wrapper = new EntryWrapper(&entry, std::move(copy));
  return Data_Wrap_Struct(cEntry, nullptr, entry_free, wrapper);
}

static VALUE entry_invalidate(VALUE entry_obj) {
  EntryWrapper *wrapper;
  Data_Get_Struct(entry_obj, EntryWrapper, wrapper);
  if (wrapper) {
    wrapper->entry_ref = nullptr;
  }
  return Qnil;
}

static EntryWrapper *entry_get_wrapper(VALUE self) {
  EntryWrapper *wrapper;
  Data_Get_Struct(self, EntryWrapper, wrapper);
  if (!wrapper) {
    rb_raise(rb_eRuntimeError, "Invalid Entry handle");
  }
  return wrapper;
}

// Helper to fetch Entry for read operations, falling back to preserved copy
static Entry *entry_for_read(VALUE self) {
  EntryWrapper *wrapper = entry_get_wrapper(self);
  if (wrapper->entry_ref) {
    return wrapper->entry_ref;
  }
  if (wrapper->entry_copy) {
    return wrapper->entry_copy.get();
  }
  rb_raise(rb_eRuntimeError, "Entry data is no longer available");
  return nullptr;
}

// Helper to fetch live Entry pointer required for mutating operations
static Entry *entry_for_live(VALUE self) {
  EntryWrapper *wrapper = entry_get_wrapper(self);
  if (!wrapper->entry_ref) {
    rb_raise(rb_eRuntimeError, "Entry is no longer valid");
  }
  return wrapper->entry_ref;
}

// Entry#path -> String
static VALUE entry_path(VALUE self) {
  Entry *entry = entry_for_read(self);
  const std::string entry_path_str = entry->path();
  return cpp_string_to_rb(entry->path());
}

// Entry#path_hierarchy -> Array
static VALUE entry_path_hierarchy(VALUE self) {
  Entry *entry = entry_for_read(self);
  return path_hierarchy_to_rb(entry->path_hierarchy());
}

// Entry#name -> String
static VALUE entry_name(VALUE self) {
  Entry *entry = entry_for_read(self);
  return cpp_string_to_rb(entry->name());
}

// Entry#size -> Integer
static VALUE entry_size(VALUE self) {
  Entry *entry = entry_for_read(self);
  return LONG2NUM(entry->size());
}

// Entry#file? -> Boolean
static VALUE entry_is_file(VALUE self) {
  Entry *entry = entry_for_read(self);
  return entry->is_file() ? Qtrue : Qfalse;
}

// Entry#directory? -> Boolean
static VALUE entry_is_directory(VALUE self) {
  Entry *entry = entry_for_read(self);
  return entry->is_directory() ? Qtrue : Qfalse;
}

// Entry#depth -> Integer
static VALUE entry_depth(VALUE self) {
  Entry *entry = entry_for_read(self);
  return INT2NUM(entry->depth());
}

// Entry#set_descent(enabled) -> self
static VALUE entry_set_descent(VALUE self, VALUE enabled) {
  Entry *entry = entry_for_live(self);
  entry->set_descent(RTEST(enabled));
  return self;
}

// Entry#set_multi_volume_group(base_name, order: :natural) -> nil
static VALUE entry_set_multi_volume_group(int argc, VALUE *argv, VALUE self) {
  VALUE base_name_val;
  VALUE options_val = Qnil;

  rb_scan_args(argc, argv, "11", &base_name_val, &options_val);

  MultiVolumeGroupOptions options;
  if (!NIL_P(options_val)) {
    VALUE hash = rb_check_hash_type(options_val);
    if (NIL_P(hash)) {
      rb_raise(rb_eTypeError, "options must be a Hash");
    }

    static ID id_order = rb_intern("order");
    VALUE order_val = rb_hash_aref(hash, ID2SYM(id_order));
    if (!NIL_P(order_val)) {
      std::string order_str;
      if (SYMBOL_P(order_val)) {
        order_str = rb_id2name(SYM2ID(order_val));
      } else {
        order_str = rb_string_to_cpp(StringValue(order_val));
      }
      for (char &ch : order_str) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      }
      if (order_str == "given") {
        options.ordering = PathEntry::Parts::Ordering::Given;
      } else if (order_str == "natural") {
        options.ordering = PathEntry::Parts::Ordering::Natural;
      } else {
        rb_raise(rb_eArgError, "order must be :natural or :given");
      }
    }
  }

  Entry *entry = entry_for_live(self);
  try {
    entry->set_multi_volume_group(rb_string_to_cpp(StringValue(base_name_val)), options);
  } catch (const std::exception &e) {
    rb_raise(rb_eRuntimeError, "Failed to set multi-volume group: %s", e.what());
  }
  return Qnil;
}

// Entry#read(length = nil) -> String
static VALUE entry_read(int argc, VALUE *argv, VALUE self) {
  VALUE length_val = Qnil;
  rb_scan_args(argc, argv, "01", &length_val);

  bool bounded_read = false;
  size_t requested_size = 0;
  if (!NIL_P(length_val)) {
    long long length_long = NUM2LL(length_val);
    if (length_long == 0) {
      return rb_str_new("", 0);
    } else if (length_long > 0) {
      const auto max_allowed = std::numeric_limits<size_t>::max();
      if (static_cast<unsigned long long>(length_long) > max_allowed) {
        rb_raise(rb_eRangeError, "requested length exceeds platform limits");
      }
      requested_size = static_cast<size_t>(length_long);
      bounded_read = true;
    }
    // Negative values fall through to the streaming path (full read)
  }

  Entry *entry = entry_for_read(self);
  const std::string entry_path_str = entry->path();

  try {
    if (bounded_read) {
      std::vector<uint8_t> buffer(requested_size);
      const ssize_t bytes_read = entry->read(buffer.data(), buffer.size());
      if (bytes_read < 0) {
        rb_raise(rb_eRuntimeError, "Failed to read entry payload at %s", entry_path_str.c_str());
      }
      return rb_str_new(reinterpret_cast<const char *>(buffer.data()), static_cast<long>(bytes_read));
    }

    std::string aggregate;
    const uint64_t reported_size = entry->size();
    if (reported_size > 0 && reported_size <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
      aggregate.reserve(static_cast<size_t>(reported_size));
    }

    std::vector<uint8_t> chunk(64 * 1024);
    while (true) {
      const ssize_t bytes_read = entry->read(chunk.data(), chunk.size());
      if (bytes_read < 0) {
        rb_raise(rb_eRuntimeError, "Failed to read entry payload at %s", entry_path_str.c_str());
      }
      if (bytes_read == 0) {
        break;
      }
      aggregate.append(reinterpret_cast<const char *>(chunk.data()), static_cast<size_t>(bytes_read));
    }

    return rb_str_new(aggregate.data(), static_cast<long>(aggregate.size()));
  } catch (const std::exception &e) {
    rb_raise(rb_eRuntimeError, "Failed to read entry at %s: %s", entry_path_str.c_str(), e.what());
    return Qnil;
  }
}

// Entry#metadata -> Hash
static VALUE entry_metadata(VALUE self) {
  Entry *entry = entry_for_read(self);
  VALUE hash = rb_hash_new();
  const EntryMetadataMap &metadata = entry->metadata();
  for (const auto &kv : metadata) {
    rb_hash_aset(hash, cpp_string_to_rb(kv.first), metadata_value_to_rb(kv.second));
  }
  return hash;
}

// Entry#metadata_value(key) -> Object or nil
static VALUE entry_metadata_value(VALUE self, VALUE key) {
  Entry *entry = entry_for_read(self);
  std::string key_str = rb_string_to_cpp(StringValue(key));
  const EntryMetadataValue *value = entry->find_metadata(key_str);
  if (!value) {
    return Qnil;
  }
  return metadata_value_to_rb(*value);
}

//=============================================================================
// Traverser class
//=============================================================================

// Free function for Traverser
static void traverser_free(void *ptr) { delete static_cast<Traverser *>(ptr); }

// Wrap Traverser pointer
static VALUE traverser_wrap(Traverser *traverser) { return Data_Wrap_Struct(cTraverser, nullptr, traverser_free, traverser); }

// Get Traverser pointer from Ruby object
static Traverser *traverser_unwrap(VALUE self) {
  Traverser *traverser;
  Data_Get_Struct(self, Traverser, traverser);
  return traverser;
}

// Traverser allocation
static VALUE traverser_allocate(VALUE klass) { return Data_Wrap_Struct(klass, nullptr, traverser_free, nullptr); }

// Traverser.new(paths, passphrases: [], formats: [], metadata_keys: []) -> Traverser
static VALUE traverser_initialize(int argc, VALUE *argv, VALUE self) {
  VALUE paths;
  VALUE opts = Qnil;
  rb_scan_args(argc, argv, "11", &paths, &opts);

  try {
    std::vector<PathHierarchy> path_list = rb_paths_to_hierarchies(paths);
    TraverserOptions options;
    populate_traverser_options(opts, options);
    Traverser *traverser = new Traverser(std::move(path_list), options);
    DATA_PTR(self) = traverser;
    return self;
  } catch (const std::exception &e) {
    rb_raise(rb_eRuntimeError, "Failed to open archive: %s", e.what());
    return Qnil;
  }
}

// Traverser#each { |entry| ... } -> Enumerator
static VALUE yield_entry(VALUE entry_obj) {
  rb_yield(entry_obj);
  return Qnil;
}

static VALUE traverser_each(VALUE self) {
  Traverser *traverser = traverser_unwrap(self);

  // If no block given, return Enumerator
  if (!rb_block_given_p()) {
    return rb_funcall(self, rb_intern("to_enum"), 1, ID2SYM(rb_intern("each")));
  }

  try {
    for (auto it = traverser->begin(); it != traverser->end(); ++it) {
      Entry &entry = *it;
      VALUE rb_entry = entry_wrap(entry);
      rb_ensure(yield_entry, rb_entry, entry_invalidate, rb_entry);
    }
  } catch (const std::exception &e) {
    rb_raise(rb_eRuntimeError, "Error during traversal: %s", e.what());
  }

  return self;
}

// Helper for Traverser.open cleanup
static VALUE traverser_close_helper(VALUE arg) {
  // Nothing to do - Traverser cleanup is automatic
  return Qnil;
}

// Traverser.open(path, opts = {}) { |traverser| ... } -> result of block
static VALUE traverser_s_open(int argc, VALUE *argv, VALUE klass) {
  VALUE traverser = rb_class_new_instance(argc, argv, klass);

  if (rb_block_given_p()) {
    return rb_ensure(rb_yield, traverser, traverser_close_helper, traverser);
  }

  return traverser;
}

//=============================================================================
// Module initialization
//=============================================================================

extern "C" void Init_archive_r() {
  // Define module Archive_r
  mArchive_r = rb_define_module("Archive_r");
  cStream = rb_define_class_under(mArchive_r, "Stream", rb_cObject);
  rb_define_alloc_func(cStream, stream_allocate);
  rb_define_method(cStream, "initialize", RUBY_METHOD_FUNC(stream_initialize), -1);

  rb_id_read_method = rb_intern("read");
  rb_id_seek_method = rb_intern("seek");
  rb_id_tell_method = rb_intern("tell");
  rb_id_eof_method = rb_intern("eof?");
  rb_id_close_method = rb_intern("close");
  rb_id_size_method = rb_intern("size");
  rb_id_call_method = rb_intern("call");
  rb_id_open_part_method = rb_intern("open_part");
  rb_id_close_part_method = rb_intern("close_part");
  rb_id_read_part_method = rb_intern("read_part");
  rb_id_seek_part_method = rb_intern("seek_part");
  rb_id_part_size_method = rb_intern("part_size");
  rb_id_open_part_io_method = rb_intern("open_part_io");
  rb_id_close_part_io_method = rb_intern("close_part_io");

  // Define Entry class
  cEntry = rb_define_class_under(mArchive_r, "Entry", rb_cObject);
  rb_undef_alloc_func(cEntry);

  rb_define_method(cEntry, "path", RUBY_METHOD_FUNC(entry_path), 0);
  rb_define_method(cEntry, "path_hierarchy", RUBY_METHOD_FUNC(entry_path_hierarchy), 0);
  rb_define_method(cEntry, "name", RUBY_METHOD_FUNC(entry_name), 0);
  rb_define_method(cEntry, "size", RUBY_METHOD_FUNC(entry_size), 0);
  rb_define_method(cEntry, "file?", RUBY_METHOD_FUNC(entry_is_file), 0);
  rb_define_method(cEntry, "directory?", RUBY_METHOD_FUNC(entry_is_directory), 0);
  rb_define_method(cEntry, "depth", RUBY_METHOD_FUNC(entry_depth), 0);
  rb_define_method(cEntry, "set_descent", RUBY_METHOD_FUNC(entry_set_descent), 1);
  rb_define_method(cEntry, "set_multi_volume_group", RUBY_METHOD_FUNC(entry_set_multi_volume_group), -1);
  rb_define_method(cEntry, "read", RUBY_METHOD_FUNC(entry_read), -1);
  rb_define_method(cEntry, "metadata", RUBY_METHOD_FUNC(entry_metadata), 0);
  rb_define_method(cEntry, "metadata_value", RUBY_METHOD_FUNC(entry_metadata_value), 1);

  // Define Traverser class
  cTraverser = rb_define_class_under(mArchive_r, "Traverser", rb_cObject);

  rb_define_alloc_func(cTraverser, traverser_allocate);
  rb_define_method(cTraverser, "initialize", RUBY_METHOD_FUNC(traverser_initialize), -1);
  rb_define_method(cTraverser, "each", RUBY_METHOD_FUNC(traverser_each), 0);
  rb_define_singleton_method(cTraverser, "open", RUBY_METHOD_FUNC(traverser_s_open), -1);

  // Make Traverser enumerable
  rb_include_module(cTraverser, rb_mEnumerable);

  rb_define_module_function(mArchive_r, "on_fault", RUBY_METHOD_FUNC(archive_r_on_fault), -1);
  rb_define_module_function(mArchive_r, "register_stream_factory", RUBY_METHOD_FUNC(archive_r_register_stream_factory), -1);

  rb_set_end_proc(archive_r_cleanup, Qnil);
}
