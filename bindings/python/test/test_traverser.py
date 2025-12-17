# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

import io
import os
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path


def _use_local_source() -> bool:
    value = os.environ.get("ARCHIVE_R_TEST_USE_LOCAL_SOURCE", "1").lower()
    return value not in ("0", "false", "no")


if _use_local_source():
    # Allow tests to import from source tree unless explicitly disabled.
    sys.path.insert(0, str(Path(__file__).parent.parent))

_DLL_SEARCH_HANDLES: list[object] = []


def _configure_windows_runtime():
    if os.name != "nt":
        return

    add_dll_directory = getattr(os, "add_dll_directory", None)
    candidate_dirs = []

    libarchive_root = os.environ.get("LIBARCHIVE_ROOT")
    if libarchive_root:
        candidate_dirs.append(Path(libarchive_root) / "bin")

    runtime_dirs = os.environ.get("LIBARCHIVE_RUNTIME_DIRS")
    if runtime_dirs:
        for entry in runtime_dirs.split(os.pathsep):
            entry = entry.strip()
            if entry:
                candidate_dirs.append(Path(entry))

    # Fall back to PATH entries so locally installed libarchive is discovered.
    if not candidate_dirs:
        for entry in os.environ.get("PATH", "").split(os.pathsep):
            entry = entry.strip()
            if entry:
                candidate_dirs.append(Path(entry))

    for directory in candidate_dirs:
        try:
            resolved = directory.resolve(strict=False)
        except OSError:
            continue
        if not resolved.is_dir():
            continue
        if add_dll_directory:
            handle = add_dll_directory(str(resolved))
            if handle is not None:
                _DLL_SEARCH_HANDLES.append(handle)
        else:
            os.environ["PATH"] = f"{resolved}{os.pathsep}{os.environ.get('PATH', '')}"


_configure_windows_runtime()

import archive_r

class TestTraverser(unittest.TestCase):
    DEFAULT_FORMATS = tuple(list(archive_r.STANDARD_FORMATS) + ["mtree"])

    class PayloadStream(archive_r.Stream):
        def __init__(self, hierarchy, payload: bytes, *, supports_seek: bool | None = None):
            if supports_seek is None:
                super().__init__(hierarchy)
            else:
                super().__init__(hierarchy, supports_seek=supports_seek)
            self._payload = payload

        def open_part_io(self, _part_hierarchy):
            return io.BytesIO(self._payload)

    @classmethod
    def setUpClass(cls):
        # Assuming test archives are available in ../../../test_data/
        test_data_dir = Path(__file__).parent.parent.parent.parent / 'test_data'
        cls.simple_archive = str(test_data_dir / 'deeply_nested.tar.gz')
        cls.multi_volume_archive = str(test_data_dir / 'multi_volume_test.tar.gz')
        cls.no_uid_archive = str(test_data_dir / 'no_uid.zip')
        cls.directory_path = str(test_data_dir / 'directory_test')
        cls.broken_archive = str(test_data_dir / 'broken_nested.tar')
        cls.stress_archive = str(test_data_dir / 'stress_test_ultimate.tar.gz')
        cls.multi_volume_parts = sorted(str(path.resolve()) for path in test_data_dir.glob('test_input.tar.gz.part*'))
        
        if not os.path.exists(cls.simple_archive):
            raise FileNotFoundError(f"Test archive not found: {cls.simple_archive}")
        if not os.path.exists(cls.multi_volume_archive):
            raise FileNotFoundError(f"Multi-volume archive not found: {cls.multi_volume_archive}")
        if not os.path.exists(cls.no_uid_archive):
            raise FileNotFoundError(f"ZIP archive not found: {cls.no_uid_archive}")
        if not os.path.isdir(cls.directory_path):
            raise FileNotFoundError(f"Test directory not found: {cls.directory_path}")
        if not os.path.exists(cls.broken_archive):
            raise FileNotFoundError(f"Broken archive not found: {cls.broken_archive}")
        if not os.path.exists(cls.stress_archive):
            raise FileNotFoundError(f"Stress archive not found: {cls.stress_archive}")
        if not cls.multi_volume_parts:
            raise FileNotFoundError('Multi-volume parts test_input.tar.gz.part* not found')

    def _normalized_options(self, **kwargs):
        options = dict(kwargs)
        options.setdefault("formats", list(self.DEFAULT_FORMATS))
        return options

    def create_traverser(self, paths, **kwargs):
        return archive_r.Traverser(paths, **self._normalized_options(**kwargs))

    def tearDown(self):
        archive_r.register_stream_factory(None)
        archive_r.on_fault(None)

    def _collect_paths(self, path):
        return [entry.path for entry in self.create_traverser([path])]

    @staticmethod
    def _is_multi_volume_part(filename: str) -> bool:
        suffix_index = filename.rfind('.part')
        if suffix_index == -1:
            return False
        suffix = filename[suffix_index + 5:]
        return suffix.isdigit() and len(suffix) > 0

    @staticmethod
    def _multi_volume_base(filename: str) -> str:
        suffix_index = filename.rfind('.part')
        return filename[:suffix_index] if suffix_index != -1 else filename

    @staticmethod
    def _expected_entry_name(entry: "archive_r.Entry") -> str:  # type: ignore[name-defined]
        hierarchy = entry.path_hierarchy
        if not hierarchy:
            return ""
        return hierarchy[-1]
    
    def test_traverser_creation(self):
        """Test that Traverser can be created"""
        traverser = self.create_traverser([self.simple_archive])
        self.assertIsInstance(traverser, archive_r.Traverser)

    def test_traverser_creation_with_path_hierarchy_keyword(self):
        """Test that Traverser can be created from a single PathHierarchy via keyword"""
        traverser = archive_r.Traverser(path_hierarchy=[self.simple_archive], **self._normalized_options())
        self.assertIsInstance(traverser, archive_r.Traverser)
    
    def test_iterator_protocol(self):
        """Test that Traverser implements iterator protocol"""
        traverser = self.create_traverser([self.simple_archive])
        entry_count = 0
        for entry in traverser:
            self.assertIsInstance(entry, archive_r.Entry)
            entry_count += 1
        
        self.assertGreater(entry_count, 0)
    
    def test_context_manager(self):
        """Test that Traverser works as context manager"""
        entry_count = 0
        with self.create_traverser([self.simple_archive]) as traverser:
            for entry in traverser:
                entry_count += 1
        
        self.assertGreater(entry_count, 0)

    def test_root_entry_exposed(self):
        """Root file should appear as the first entry with depth 0"""
        with self.create_traverser([self.simple_archive]) as traverser:
            first_entry = next(iter(traverser))

        self.assertEqual(first_entry.depth, 0)
        self.assertEqual(Path(first_entry.path).name, Path(self.simple_archive).name)

    def test_empty_archive_traversal_yields_only_root(self):
        """Empty archives should enumerate only the root entry"""
        with tempfile.TemporaryDirectory() as tmpdir:
            empty_tar = Path(tmpdir) / "empty.tar"
            with tarfile.open(empty_tar, "w"):
                pass
            entries = list(self.create_traverser([str(empty_tar)]))

        self.assertEqual(1, len(entries))
        root_entry = entries[0]
        self.assertEqual(0, root_entry.depth)
        self.assertEqual(root_entry.path_hierarchy[0], str(empty_tar))

    def test_invalid_path_hierarchy_rejected(self):
        """Empty path hierarchy input should raise ValueError"""
        with self.assertRaises(ValueError):
            self.create_traverser([[]])
    
    def test_entry_properties(self):
        """Test Entry properties"""
        with self.create_traverser([self.simple_archive]) as traverser:
            for entry in traverser:
                if entry.depth == 0:
                    continue
                # Check property types
                self.assertIsInstance(entry.path, str)
                self.assertIsInstance(entry.name, str)
                self.assertIsInstance(entry.size, int)
                self.assertIsInstance(entry.depth, int)
                self.assertIsInstance(entry.is_file, bool)
                self.assertIsInstance(entry.is_directory, bool)
                
                # Basic validation
                self.assertGreater(len(entry.path), 0)
                expected_name = self._expected_entry_name(entry)
                self.assertEqual(entry.name, expected_name)
                self.assertGreater(len(expected_name), 0)
                self.assertGreaterEqual(entry.size, 0)
                self.assertGreater(entry.depth, 0)

                hierarchy = entry.path_hierarchy
                self.assertIsInstance(hierarchy, list)
                self.assertGreater(len(hierarchy), 0)
                self.assertTrue(all(isinstance(component, str) for component in hierarchy))
                self.assertEqual("/".join(hierarchy), entry.path)
                
                # Test first entry only
                break

    def test_entry_read_full_payload(self):
        """Entry.read() without size should return the entire file payload"""
        with self.create_traverser([self.simple_archive]) as traverser:
            target = None
            for entry in traverser:
                if entry.depth >= 1 and entry.is_file and 0 < entry.size <= 4096:
                    target = entry
                    break

            self.assertIsNotNone(target, "No suitable file entry found for read() test")
            payload = target.read()
            self.assertIsInstance(payload, bytes)
            self.assertEqual(target.size, len(payload))
            self.assertEqual(b"", target.read())

    def test_entry_read_with_size_argument(self):
        """Entry.read(size) should stream in bounded chunks"""
        with self.create_traverser([self.simple_archive]) as traverser:
            target = None
            for entry in traverser:
                if entry.depth >= 1 and entry.is_file and entry.size >= 64:
                    target = entry
                    break

            self.assertIsNotNone(target, "No suitable file entry found for chunked read() test")
            chunk_size = max(1, min(target.size // 4, 64 * 1024))
            collected = bytearray()
            while True:
                chunk = target.read(chunk_size)
                self.assertIsInstance(chunk, bytes)
                if not chunk:
                    break
                self.assertLessEqual(len(chunk), chunk_size)
                collected.extend(chunk)
            self.assertEqual(target.size, len(collected))

    def test_entry_multiple_reads_exhaust_payload(self):
        """Repeated read() calls should eventually return EOF"""
        with self.create_traverser([self.simple_archive]) as traverser:
            target = None
            for entry in traverser:
                if entry.depth >= 1 and entry.is_file and entry.size >= 64:
                    target = entry
                    break

            self.assertIsNotNone(target, "No suitable file entry found for repeated read test")
            chunk1 = target.read(16)
            chunk2 = target.read(16)
            remainder = target.read()
            self.assertGreater(len(chunk1), 0)
            self.assertGreater(len(chunk2), 0)
            self.assertEqual(target.size, len(chunk1) + len(chunk2) + len(remainder))
            self.assertEqual(b"", target.read())

    def test_path_hierarchy_component_values(self):
        """Ensure path_hierarchy captures absolute archive and relative entry components"""
        archive_abs = os.path.abspath(self.simple_archive)
        with self.create_traverser([self.simple_archive]) as traverser:
            entry = next(e for e in traverser if e.depth >= 1)

        hierarchy = entry.path_hierarchy
        self.assertIsInstance(hierarchy, list)
        self.assertGreater(len(hierarchy), 1)
        self.assertEqual(archive_abs, hierarchy[0])
        self.assertEqual(entry.name, self._expected_entry_name(entry))

        self.assertEqual(entry.path, "/".join(hierarchy))
    
    def test_set_descent(self):
        """Test per-entry descent control"""
        entries_with_skip = []
        with self.create_traverser([self.simple_archive]) as traverser:
            for i, entry in enumerate(traverser):
                entries_with_skip.append(entry.path)
                if i == 0:
                    entry.set_descent(False)
        
        self.assertGreater(len(entries_with_skip), 0)
    
    def test_entry_repr(self):
        """Test Entry repr"""
        with self.create_traverser([self.simple_archive]) as traverser:
            entry = next(iter(traverser))
            repr_str = repr(entry)
            self.assertIn('Entry', repr_str)
            self.assertIn('path=', repr_str)
            self.assertIn('size=', repr_str)
            self.assertIn('depth=', repr_str)
    
    def test_list_comprehension(self):
        """Test using list comprehension"""
        paths = [entry.path for entry in self.create_traverser([self.simple_archive])]
        self.assertGreater(len(paths), 0)
        self.assertTrue(all(isinstance(p, str) for p in paths))
    
    def test_filter_files(self):
        """Test filtering files"""
        # Collect file information during iteration, not Entry objects
        files = []
        for e in self.create_traverser([self.simple_archive]):
            if e.is_file:
                files.append({'path': e.path, 'is_file': e.is_file})
        
        self.assertGreater(len(files), 0)
        self.assertTrue(all(f['is_file'] for f in files))

    def test_traverser_with_options(self):
        """Ensure optional passphrases and formats arguments are accepted"""
        traverser = self.create_traverser(
            [self.simple_archive],
            passphrases=["unused-passphrase"],
            formats=["tar"],
        )
        entries = [entry.path for entry in traverser]
        self.assertGreater(len(entries), 0)
        self.assertTrue(all(isinstance(p, str) for p in entries))

    def test_metadata_selection(self):
        """Verify metadata selection and retrieval semantics"""
        traverser = self.create_traverser(
            [self.simple_archive],
            metadata_keys=["pathname", "size"],
        )
        entry = next(e for e in traverser if e.depth >= 1)

        metadata = entry.metadata
        expected_name = self._expected_entry_name(entry)
        self.assertIn("pathname", metadata)
        self.assertEqual(metadata["pathname"], expected_name)
        self.assertEqual(entry.name, expected_name)
        self.assertIn("size", metadata)
        self.assertIsInstance(metadata["size"], int)

        missing = entry.metadata_value("uid")
        self.assertIsNone(missing)

    def test_metadata_missing_value(self):
        """Ensure requested metadata absent in archive yields None"""
        traverser = self.create_traverser(
            [self.no_uid_archive],
            metadata_keys=["pathname", "uid"],
        )
        entry = next(e for e in traverser if e.depth >= 1)

        metadata = entry.metadata
        expected_name = self._expected_entry_name(entry)
        self.assertIn("pathname", metadata)
        self.assertEqual(metadata["pathname"], expected_name)
        self.assertEqual(entry.name, expected_name)
        self.assertNotIn("uid", metadata)
        self.assertIsNone(entry.metadata_value("uid"))

    def test_stream_factory_with_bytes_io(self):
        expected = self._collect_paths(self.simple_archive)
        payload = Path(self.simple_archive).read_bytes()
        calls = {"count": 0}

        def factory(hierarchy):
            calls["count"] += 1
            if hierarchy[0] == os.path.abspath(self.simple_archive):
                return self.PayloadStream(hierarchy, payload)
            return None

        archive_r.register_stream_factory(factory)
        actual = self._collect_paths(self.simple_archive)
        self.assertEqual(expected, actual)
        self.assertEqual(1, calls["count"])

    def test_stream_factory_path_override(self):
        virtual = self.simple_archive + ".virtual"
        expected = [
            path.replace(self.simple_archive, virtual, 1)
            for path in self._collect_paths(self.simple_archive)
        ]

        def factory(hierarchy):
            if hierarchy[0] == os.path.abspath(virtual):
                return self.PayloadStream(hierarchy, Path(self.simple_archive).read_bytes())
            return None

        archive_r.register_stream_factory(factory)
        actual = self._collect_paths(virtual)
        self.assertEqual(expected, actual)

    def test_stream_factory_with_custom_stream_without_seek(self):
        expected = self._collect_paths(self.simple_archive)
        payload = Path(self.simple_archive).read_bytes()
        calls = {"count": 0}

        def factory(hierarchy):
            absolute = os.path.abspath(self.simple_archive)
            if hierarchy[0] != absolute:
                return None
            calls["count"] += 1
            return self.PayloadStream(hierarchy, payload)

        archive_r.register_stream_factory(factory)
        actual = self._collect_paths(self.simple_archive)
        self.assertEqual(expected, actual)
        self.assertEqual(1, calls["count"])

    def test_stream_factory_multi_volume_custom_stream(self):
        class RecordingStream(archive_r.Stream):
            def __init__(self, hierarchy):
                super().__init__(hierarchy, supports_seek=True)
                self.requests = []

            def open_part_io(self, part_hierarchy):
                head = part_hierarchy[0]
                self.requests.append(head)
                return open(head, 'rb')

        parts_hierarchy = [[part for part in self.multi_volume_parts]]
        archive_r.register_stream_factory(None)
        expected = self._collect_paths(parts_hierarchy)

        streams = []

        def factory(hierarchy):
            head = hierarchy[0]
            self.assertIsInstance(head, list)
            stream = RecordingStream(hierarchy)
            streams.append(stream)
            return stream

        try:
            archive_r.register_stream_factory(factory)
            actual = self._collect_paths(parts_hierarchy)
            self.assertEqual(expected, actual)
            self.assertTrue(streams)
            self.assertEqual(self.multi_volume_parts, streams[0].requests)
        finally:
            archive_r.register_stream_factory(None)

    def test_stream_factory_requires_callable(self):
        """register_stream_factory should reject non-callables"""
        with self.assertRaises(TypeError):
            archive_r.register_stream_factory("invalid")

    def test_stream_factory_seekable_without_rewind(self):
        """Streams with seek/tell but no rewind should be accepted"""
        expected = self._collect_paths(self.simple_archive)
        payload = Path(self.simple_archive).read_bytes()
        streams = []

        class TrackingBuffer(io.BytesIO):
            def __init__(self, owner, data: bytes):
                super().__init__(data)
                self._owner = owner

            def seek(self, offset: int, whence: int = io.SEEK_SET) -> int:
                self._owner.seek_calls += 1
                return super().seek(offset, whence)

        class SeekableOnlyStream(archive_r.Stream):
            def __init__(self, hierarchy, data: bytes):
                super().__init__(hierarchy, supports_seek=True)
                self._data = data
                self.seek_calls = 0

            def open_part_io(self, _hierarchy):
                return TrackingBuffer(self, self._data)

        def factory(hierarchy):
            if hierarchy[0] == os.path.abspath(self.simple_archive):
                stream = SeekableOnlyStream(hierarchy, payload)
                streams.append(stream)
                return stream
            return None

        archive_r.register_stream_factory(factory)
        actual = self._collect_paths(self.simple_archive)
        self.assertEqual(expected, actual)
        self.assertTrue(streams)
        self.assertTrue(any(stream.seek_calls >= 1 for stream in streams))

    def test_stream_factory_rejects_non_file_like_results(self):
        """Stream factory results must be file-like (provide read()) or None"""

        def factory(_hierarchy):
            return object()

        try:
            archive_r.register_stream_factory(factory)
            with self.assertRaises(TypeError):
                self._collect_paths(self.simple_archive)
        finally:
            archive_r.register_stream_factory(None)

    def test_multi_volume_grouping(self):
        """Verify multi-volume archives can be grouped and traversed"""
        part_paths = []
        for entry in self.create_traverser([self.multi_volume_archive]):
            filename = Path(entry.path).name
            if self._is_multi_volume_part(filename):
                part_paths.append(entry.path)

        self.assertGreater(len(part_paths), 0)

        entries_inside = []
        for entry in self.create_traverser([self.multi_volume_archive]):
            filename = Path(entry.path).name
            if self._is_multi_volume_part(filename):
                entry.set_multi_volume_group(
                    self._multi_volume_base(filename),
                    order="given",
                )
            if entry.depth > 1:
                entries_inside.append(entry.path)

        self.assertGreater(len(entries_inside), 0)

    def test_multi_root_traversal(self):
        """Ensure multiple roots are fully traversed"""
        paths = [self.simple_archive, self.directory_path]
        entries = list(self.create_traverser(paths))
        self.assertEqual(len(entries), 21)

        counts = {self.simple_archive: 0, self.directory_path: 0}
        
        # Normalize paths for comparison to handle potential separator differences (e.g. Windows vs MSYS2)
        simple_archive_norm = os.path.normpath(self.simple_archive)
        directory_path_norm = os.path.normpath(self.directory_path)

        for entry in entries:
            root_component = os.path.normpath(entry.path_hierarchy[0])
            
            # Check simple_archive
            if root_component == simple_archive_norm or root_component.startswith(simple_archive_norm + os.sep):
                counts[self.simple_archive] += 1
            # Check directory_path
            elif root_component == directory_path_norm or root_component.startswith(directory_path_norm + os.sep):
                counts[self.directory_path] += 1
            else:
                self.fail(f"Unexpected root component: {root_component} (expected matches for {simple_archive_norm} or {directory_path_norm})")

        self.assertEqual(counts[self.simple_archive], 11)
        self.assertEqual(counts[self.directory_path], 10)

    def test_large_archive_entry_count(self):
        """Stress archive should enumerate hundreds of entries without error"""
        entries = list(self.create_traverser([self.stress_archive]))
        self.assertGreaterEqual(len(entries), 300)
        self.assertGreater(len(entries), 0)
        max_depth = max(entry.depth for entry in entries)
        self.assertGreaterEqual(max_depth, 8)

    def test_fault_callback_receives_nested_fault(self):
        """Verify faults propagate through archive_r.on_fault"""
        captured = []

        def handler(fault):
            captured.append(fault)

        archive_r.on_fault(handler)
        saw_ok = False
        try:
            for entry in self.create_traverser([self.broken_archive]):
                if entry.name == 'ok.txt':
                    saw_ok = True
        finally:
            archive_r.on_fault(None)

        self.assertTrue(saw_ok, 'Expected to enumerate ok.txt even when faults occur')
        self.assertTrue(captured, 'Fault callback did not receive any faults')
        self.assertTrue(any('corrupt_inner.tar' in fault.get('path', '') for fault in captured))

if __name__ == '__main__':
    unittest.main()
