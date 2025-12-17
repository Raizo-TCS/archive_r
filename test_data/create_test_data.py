#!/usr/bin/env python3
"""
Archive test data generator
Creates comprehensive test archives with proper multipart files
"""

import sys
import shutil
import tarfile
import tempfile
from pathlib import Path
from typing import List, Optional

class TestDataGenerator:
    def __init__(self, output_dir: Path):
        self.output_dir = Path(output_dir)
        self.work_dir = None
        
    def __enter__(self):
        self.work_dir = Path(tempfile.mkdtemp(prefix="test_data_"))
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.work_dir and self.work_dir.exists():
            shutil.rmtree(self.work_dir)
    
    def create_text_file(self, path: Path, content: str):
        """Create a text file with specified content"""
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        print(f"  Created: {path.name} ({len(content)} bytes)")
    
    def create_binary_file(self, path: Path, size: int):
        """Create a binary file with random data"""
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, 'wb') as f:
            # Use predictable data for reproducibility
            data = bytes([(i * 7 + 13) % 256 for i in range(size)])
            f.write(data)
        print(f"  Created: {path.name} ({size} bytes)")
    
    def create_tar_gz(self, archive_path: Path, source_dir: Path):
        """Create a tar.gz archive from a directory"""
        with tarfile.open(archive_path, 'w:gz') as tar:
            for item in sorted(source_dir.rglob('*')):
                if item.is_file():
                    arcname = item.relative_to(source_dir)
                    tar.add(item, arcname=str(arcname))
        print(f"  Created archive: {archive_path.name} ({archive_path.stat().st_size} bytes)")
    
    def create_multipart(self, base_path: Path, source_dir: Path, 
                        part_size: int, num_parts: int, 
                        min_parts: Optional[int] = None) -> List[Path]:
        """
        Create multipart archive files
        Returns list of part file paths
        
        If min_parts is specified, will ensure at least that many parts are created
        by padding the data if necessary.
        """
        # First create the tar.gz archive
        temp_tar = base_path.parent / f"{base_path.name}.temp.tar.gz"
        self.create_tar_gz(temp_tar, source_dir)
        
        # Read the archive
        with open(temp_tar, 'rb') as f:
            data = f.read()
        
        archive_size = len(data)
        print(f"  Archive size: {archive_size} bytes")
        
        # Calculate actual number of parts needed
        actual_parts = (archive_size + part_size - 1) // part_size
        
        # If min_parts is specified and we have fewer parts, pad the data
        if min_parts and actual_parts < min_parts:
            required_size = min_parts * part_size
            padding_needed = required_size - archive_size
            # Add padding at the end (will be ignored by tar)
            data = data + b'\x00' * padding_needed
            print(f"  Padded with {padding_needed} bytes to create {min_parts} parts")
            num_parts = min_parts
        elif actual_parts != num_parts:
            print(f"  Note: Archive needs {actual_parts} parts (requested {num_parts}), using {actual_parts}")
            num_parts = actual_parts
        
        # Split into parts (all parts same size except possibly the last one)
        part_files = []
        for i in range(num_parts):
            part_num = i + 1  # Parts are 1-indexed: .part001, .part002, etc.
            part_path = base_path.parent / f"{base_path.name}.part{part_num:03d}"
            
            start = i * part_size
            end = min(start + part_size, len(data))
            
            with open(part_path, 'wb') as f:
                f.write(data[start:end])
            
            part_files.append(part_path)
            print(f"  Created part: {part_path.name} ({end - start} bytes)")
        
        # Clean up temp file
        temp_tar.unlink()
        
        return part_files
    
    def generate_simple_multipart_test(self):
        """
        Generate: multipart_test.tar.gz
        Contains: archive.tar.gz split into 2 parts
        Inside: file1.txt, file2.txt
        """
        print("\n=== Creating multipart_test.tar.gz ===")
        
        work = self.work_dir / "multipart_test"
        work.mkdir()
        
        # Create content for the multipart archive
        content_dir = work / "content"
        content_dir.mkdir()
        self.create_text_file(content_dir / "file1.txt", "content1\n" * 10)
        self.create_text_file(content_dir / "file2.txt", "content2\n" * 10)
        
        # Create multipart archive (2 parts of 100 bytes each)
        parts = self.create_multipart(
            work / "archive.tar.gz",
            content_dir,
            part_size=100,
            num_parts=2,
            min_parts=2  # Ensure at least 2 parts
        )
        
        # Package everything into the final archive
        final = self.output_dir / "multipart_test.tar.gz"
        with tarfile.open(final, 'w:gz') as tar:
            for part in parts:
                tar.add(part, arcname=part.name)
        
        print(f"✓ Created: {final}")
        return final
    
    def generate_nested_with_multipart_test(self):
        """
        Generate: nested_with_multipart.tar.gz
        Contains: inner.tar.gz (regular archive) + data.txt multipart (3 parts)
        """
        print("\n=== Creating nested_with_multipart.tar.gz ===")
        
        work = self.work_dir / "nested_with_multipart"
        work.mkdir()
        
        # Create regular nested archive
        inner_content = work / "inner_content"
        inner_content.mkdir()
        self.create_text_file(inner_content / "file1.txt", "Hello World\n")
        
        inner_archive = work / "inner.tar.gz"
        self.create_tar_gz(inner_archive, inner_content)
        
        # Create multipart content
        multipart_content = work / "multipart_content"
        multipart_content.mkdir()
        self.create_text_file(multipart_content / "data.txt", "X" * 100)
        
        # Create multipart (3 parts)
        parts = self.create_multipart(
            work / "data.txt",
            multipart_content,
            part_size=50,
            num_parts=3,
            min_parts=3
        )
        
        # Package final archive
        final = self.output_dir / "nested_with_multipart.tar.gz"
        with tarfile.open(final, 'w:gz') as tar:
            tar.add(inner_archive, arcname="inner.tar.gz")
            for part in parts:
                tar.add(part, arcname=part.name)
        
        print(f"✓ Created: {final}")
        return final
    
    def generate_deeply_nested_test(self):
        """
        Generate: deeply_nested.tar.gz
        5 levels deep: level1 -> level2 -> level3 -> deep.txt
        Also includes multipart at level2
        """
        print("\n=== Creating deeply_nested.tar.gz ===")
        
        work = self.work_dir / "deeply_nested"
        work.mkdir()
        
        # Level 3 content
        level3_content = work / "level3_content"
        level3_content.mkdir()
        self.create_text_file(level3_content / "deep.txt", "deep content\n")
        level3_archive = work / "level3.tar.gz"
        self.create_tar_gz(level3_archive, level3_content)
        
        # Level 2: includes level3 + multipart
        level2_content = work / "level2_content"
        level2_content.mkdir()
        shutil.copy(level3_archive, level2_content / "level3.tar.gz")
        
        # Create multipart at level2
        multipart_content = work / "multipart_at_level2"
        multipart_content.mkdir()
        self.create_text_file(multipart_content / "archive_data.txt", "Y" * 150)
        parts = self.create_multipart(
            level2_content / "archive.tar.gz",
            multipart_content,
            part_size=100,
            num_parts=2,
            min_parts=2
        )
        
        level2_archive = work / "level2.tar.gz"
        self.create_tar_gz(level2_archive, level2_content)
        
        # Level 1: includes level2 + root file
        level1_content = work / "level1_content"
        level1_content.mkdir()
        shutil.copy(level2_archive, level1_content / "level2.tar.gz")
        self.create_text_file(level1_content / "root.txt", "root data\n")
        
        level1_archive = work / "level1.tar.gz"
        self.create_tar_gz(level1_archive, level1_content)
        
        # Final archive
        final_content = work / "final_content"
        final_content.mkdir()
        shutil.copy(level1_archive, final_content / "level1.tar.gz")
        self.create_text_file(final_content / "root.txt", "top level\n")
        
        final = self.output_dir / "deeply_nested.tar.gz"
        self.create_tar_gz(final, final_content)
        
        print(f"✓ Created: {final}")
        return final
    
    def generate_deeply_nested_multipart_test(self):
        """
        Generate: deeply_nested_multipart.tar.gz
        Contains: level1.tar multipart (7 parts)
        Inside level1: level3.tar.gz multipart
        """
        print("\n=== Creating deeply_nested_multipart.tar.gz ===")
        
        work = self.work_dir / "deeply_nested_multipart"
        work.mkdir()
        
        # Level 3 content - also multipart
        level3_content = work / "level3_content"
        level3_content.mkdir()
        self.create_binary_file(level3_content / "deep_file.dat", 5000)
        
        level3_parts_dir = work / "level3_parts"
        level3_parts_dir.mkdir()
        level3_parts = self.create_multipart(
            level3_parts_dir / "level3.tar.gz",
            level3_content,
            part_size=2000,
            num_parts=3,
            min_parts=3
        )
        
        # Level 1 content - includes level3 parts
        level1_content = work / "level1_content"
        level1_content.mkdir()
        for part in level3_parts:
            shutil.copy(part, level1_content / part.name)
        
        # Create level1 as multipart (7 parts of 10KB each)
        parts = self.create_multipart(
            work / "level1.tar",
            level1_content,
            part_size=10240,
            num_parts=7,
            min_parts=7
        )
        
        # Final archive
        final = self.output_dir / "deeply_nested_multipart.tar.gz"
        with tarfile.open(final, 'w:gz') as tar:
            for part in parts:
                tar.add(part, arcname=part.name)
        
        print(f"✓ Created: {final}")
        return final
    
    def generate_stress_test_ultimate(self):
        """
        Generate: stress_test_ultimate.tar.gz
        10-level deep nested structure mimicking original bash script:
        - Level 10: Deepest files
        - Level 9: Mix of multipart and regular archives
        - Level 8: Large multipart (100KB parts)
        - Level 7: Many archives (12+) at same level
        - Level 6: Small multipart (8KB parts)
        - Level 5: Nested multipart archives
        - Level 4: Deep directory structures
        - Level 3: Alternating patterns
        - Level 2: Multiple multipart + deep paths
        - Level 1: Final composition
        """
        print("\n=== Creating stress_test_ultimate.tar.gz (10-level structure) ===")
        
        work = self.work_dir / "stress_ultimate"
        work.mkdir()
        
        # ========================================
        # Level 10: Deepest level - Simple files
        # ========================================
        print("  Level 10: Deepest files")
        level10_content = work / "level10_content"
        level10_content.mkdir()
        self.create_text_file(level10_content / "deep_file_001.txt", "Deep content 1\n" * 50)
        self.create_text_file(level10_content / "deep_file_002.txt", "Deep content 2\n" * 100)
        self.create_text_file(level10_content / "deep_file_003.txt", "Deep content 3\n" * 25)
        
        level10_archive = work / "level10_archive.tar.gz"
        self.create_tar_gz(level10_archive, level10_content)
        
        # ========================================
        # Level 9: Mix of multipart and regular archives
        # ========================================
        print("  Level 9: Multiple archives in same directory")
        level9_content = work / "level9_content"
        level9_content.mkdir()
        
        # Small regular archive
        small_content = work / "level9_small"
        small_content.mkdir()
        self.create_text_file(small_content / "l9_file_a.txt", "Level 9 A\n" * 30)
        self.create_text_file(small_content / "l9_file_b.txt", "Level 9 B\n" * 45)
        small_archive = level9_content / "small_archive.tar.gz"
        self.create_tar_gz(small_archive, small_content)
        
        # Multipart at level 9
        multi9_content = work / "level9_multi"
        multi9_content.mkdir()
        self.create_binary_file(multi9_content / "l9_multipart_content_1.txt", 10000)
        self.create_binary_file(multi9_content / "l9_multipart_content_2.txt", 15000)
        self.create_multipart(
            level9_content / "multi_small",
            multi9_content,
            part_size=5000,
            num_parts=5,
            min_parts=5
        )
        
        # Include level 10
        shutil.copy(level10_archive, level9_content / "level10_archive.tar.gz")
        
        level9_archive = work / "level9_container.tar.gz"
        self.create_tar_gz(level9_archive, level9_content)
        
        # ========================================
        # Level 8: Large multipart archive (parts > 65536)
        # ========================================
        print("  Level 8: Large multipart with big parts")
        level8_content = work / "level8_content"
        level8_content.mkdir()
        
        # Large files for big multipart
        large_content = work / "level8_large"
        large_content.mkdir()
        self.create_binary_file(large_content / "large_content_1.txt", 100000)
        self.create_binary_file(large_content / "large_content_2.txt", 150000)
        self.create_binary_file(large_content / "large_content_3.txt", 80000)
        
        self.create_multipart(
            level8_content / "large_multi",
            large_content,
            part_size=100000,
            num_parts=4,
            min_parts=4
        )
        
        # Include level 9
        shutil.copy(level9_archive, level8_content / "level9_container.tar.gz")
        
        level8_archive = work / "level8_big_parts.tar.gz"
        self.create_tar_gz(level8_archive, level8_content)
        
        # ========================================
        # Level 7: Many small archives in same level (12+)
        # ========================================
        print("  Level 7: Many archives at same level")
        level7_content = work / "level7_content"
        level7_content.mkdir()
        
        for i in range(1, 13):
            subdir = work / f"level7_subdir_{i:02d}"
            subdir.mkdir()
            self.create_text_file(subdir / f"file_{i:02d}_a.txt", f"File {i} A\n" * 10)
            self.create_text_file(subdir / f"file_{i:02d}_b.txt", f"File {i} B\n" * 15)
            
            archive = level7_content / f"archive_{i:02d}.tar.gz"
            self.create_tar_gz(archive, subdir)
        
        # Include level 8
        shutil.copy(level8_archive, level7_content / "level8_big_parts.tar.gz")
        
        level7_archive = work / "level7_many_archives.tar.gz"
        self.create_tar_gz(level7_archive, level7_content)
        
        # ========================================
        # Level 6: Multipart with small parts (< 65536)
        # ========================================
        print("  Level 6: Multipart with small parts")
        level6_content = work / "level6_content"
        level6_content.mkdir()
        
        # Small part multipart
        small_part_content = work / "level6_small_part"
        small_part_content.mkdir()
        self.create_binary_file(small_part_content / "small_part_content.txt", 50000)
        
        self.create_multipart(
            level6_content / "tiny_parts",
            small_part_content,
            part_size=8192,
            num_parts=8,
            min_parts=8
        )
        
        # Include level 7
        shutil.copy(level7_archive, level6_content / "level7_many_archives.tar.gz")
        
        level6_archive = work / "level6_tiny_parts.tar.gz"
        self.create_tar_gz(level6_archive, level6_content)
        
        # ========================================
        # Level 5: Nested multipart archives
        # ========================================
        print("  Level 5: Nested multipart")
        level5_content = work / "level5_content"
        level5_content.mkdir()
        
        # First multipart
        nested_multi_content = work / "level5_nested_multi"
        nested_multi_content.mkdir()
        self.create_binary_file(nested_multi_content / "l5_data_1.txt", 5000)
        self.create_binary_file(nested_multi_content / "l5_data_2.txt", 7000)
        
        self.create_multipart(
            level5_content / "nested_multi",
            nested_multi_content,
            part_size=12000,
            num_parts=3,
            min_parts=3
        )
        
        # Second multipart
        extra_multi_content = work / "level5_extra_multi"
        extra_multi_content.mkdir()
        self.create_binary_file(extra_multi_content / "l5_extra.txt", 3000)
        
        self.create_multipart(
            level5_content / "extra_multi",
            extra_multi_content,
            part_size=6000,
            num_parts=2,
            min_parts=2
        )
        
        # Include level 6
        shutil.copy(level6_archive, level5_content / "level6_tiny_parts.tar.gz")
        
        level5_archive = work / "level5_multi_nest.tar.gz"
        self.create_tar_gz(level5_archive, level5_content)
        
        # ========================================
        # Level 4: Deep directory structure with archives
        # ========================================
        print("  Level 4: Deep directory paths")
        level4_content = work / "level4_content"
        level4_content.mkdir()
        
        # Create deep directory structure
        deep_path = level4_content / "very/long/directory/path/structure/that/goes/deep/into/filesystem"
        deep_path.mkdir(parents=True)
        
        self.create_text_file(deep_path / "deeply_nested_file.txt", "Deep file\n" * 50)
        
        # Place archive deep in directory
        shutil.copy(level5_archive, deep_path / "level5_multi_nest.tar.gz")
        
        # Create multipart deep in directory
        deep_multi_content = work / "level4_deep_multi"
        deep_multi_content.mkdir()
        self.create_binary_file(deep_multi_content / "deep_content_1.txt", 8000)
        self.create_binary_file(deep_multi_content / "deep_content_2.txt", 12000)
        
        self.create_multipart(
            deep_path / "deep_multi",
            deep_multi_content,
            part_size=10000,
            num_parts=2,
            min_parts=2
        )
        
        # Create nested archive deep in directory
        deep_nested_content = work / "level4_deep_nested"
        deep_nested_content.mkdir()
        self.create_text_file(deep_nested_content / "deep_nested.txt", "Nested\n" * 100)
        deep_nested_archive = deep_path / "deep_nested.tar.gz"
        self.create_tar_gz(deep_nested_archive, deep_nested_content)
        
        # Create deep_dirs.tar.gz from the directory structure
        deep_dirs_work = work / "level4_deep_dirs_work"
        deep_dirs_work.mkdir()
        shutil.copytree(level4_content / "very", deep_dirs_work / "very")
        deep_dirs_archive = work / "deep_dirs.tar.gz"
        self.create_tar_gz(deep_dirs_archive, deep_dirs_work)
        
        # Create level4_deep_paths.tar.gz
        level4_final_content = work / "level4_final_content"
        level4_final_content.mkdir()
        shutil.copy(deep_dirs_archive, level4_final_content / "deep_dirs.tar.gz")
        shutil.copy(level5_archive, level4_final_content / "level5_multi_nest.tar.gz")
        
        level4_archive = work / "level4_deep_paths.tar.gz"
        self.create_tar_gz(level4_archive, level4_final_content)
        
        # ========================================
        # Level 3: Alternating archive and multipart
        # ========================================
        print("  Level 3: Alternating patterns")
        level3_content = work / "level3_content"
        level3_content.mkdir()
        
        # Regular archive 1
        regular1_content = work / "level3_regular1"
        regular1_content.mkdir()
        self.create_text_file(regular1_content / "regular_1.txt", "Regular 1\n" * 100)
        regular1_archive = level3_content / "regular_archive_1.tar.gz"
        self.create_tar_gz(regular1_archive, regular1_content)
        
        # Multipart 1
        multi1_content = work / "level3_multi1"
        multi1_content.mkdir()
        self.create_binary_file(multi1_content / "multi_content_1.txt", 8000)
        self.create_multipart(
            level3_content / "alternating_multi_1",
            multi1_content,
            part_size=4000,
            num_parts=3,
            min_parts=3
        )
        
        # Regular archive 2
        regular2_content = work / "level3_regular2"
        regular2_content.mkdir()
        self.create_text_file(regular2_content / "regular_2.txt", "Regular 2\n" * 150)
        regular2_archive = level3_content / "regular_archive_2.tar.gz"
        self.create_tar_gz(regular2_archive, regular2_content)
        
        # Multipart 2
        multi2_content = work / "level3_multi2"
        multi2_content.mkdir()
        self.create_binary_file(multi2_content / "multi_content_2.txt", 10000)
        self.create_multipart(
            level3_content / "alternating_multi_2",
            multi2_content,
            part_size=5000,
            num_parts=3,
            min_parts=3
        )
        
        # Include level 4
        shutil.copy(level4_archive, level3_content / "level4_deep_paths.tar.gz")
        
        level3_archive = work / "level3_alternating.tar.gz"
        self.create_tar_gz(level3_archive, level3_content)
        
        # ========================================
        # Level 2: Many multipart archives + deep paths
        # ========================================
        print("  Level 2: Multiple multipart archives")
        level2_content = work / "level2_content"
        level2_content.mkdir()
        
        # Multiple multipart archives (5 sets)
        for i in range(1, 6):
            multi_content = work / f"level2_multi_set_{i}"
            multi_content.mkdir()
            self.create_binary_file(multi_content / f"multi_set_{i}.txt", 5000 * i)
            
            self.create_multipart(
                level2_content / f"multi_set_{i}",
                multi_content,
                part_size=2000 + i * 1000,
                num_parts=3,
                min_parts=3
            )
        
        # Another deep directory pattern
        deep_path2 = level2_content / "another/deep/path/to/test/traversal/capabilities"
        deep_path2.mkdir(parents=True)
        
        # Multipart deep in path
        path_multi_content = work / "level2_path_multi"
        path_multi_content.mkdir()
        self.create_binary_file(path_multi_content / "path_content.txt", 6000)
        
        self.create_multipart(
            deep_path2 / "path_multi",
            path_multi_content,
            part_size=3000,
            num_parts=2,
            min_parts=2
        )
        
        # Regular archive deep in path
        path_regular_content = work / "level2_path_regular"
        path_regular_content.mkdir()
        self.create_text_file(path_regular_content / "path_regular.txt", "Path regular\n" * 200)
        path_archive = deep_path2 / "path_archive.tar.gz"
        self.create_tar_gz(path_archive, path_regular_content)
        
        # Create deep_path_2.tar.gz from another/ directory
        deep_path2_work = work / "level2_deep_path2_work"
        deep_path2_work.mkdir()
        shutil.copytree(level2_content / "another", deep_path2_work / "another")
        deep_path2_archive = work / "deep_path_2.tar.gz"
        self.create_tar_gz(deep_path2_archive, deep_path2_work)
        
        # Remove the another/ directory from level2_content and add the archive
        shutil.rmtree(level2_content / "another")
        shutil.copy(deep_path2_archive, level2_content / "deep_path_2.tar.gz")
        
        # Include level 3
        shutil.copy(level3_archive, level2_content / "level3_alternating.tar.gz")
        
        level2_archive = work / "level2_multi_set.tar.gz"
        self.create_tar_gz(level2_archive, level2_content)
        
        # ========================================
        # Level 1: Top level with everything
        # ========================================
        print("  Level 1: Final composition")
        level1_content = work / "level1_content"
        level1_content.mkdir()
        
        # Direct files
        self.create_text_file(level1_content / "root_file_1.txt", "Root 1\n" * 75)
        self.create_text_file(level1_content / "root_file_2.txt", "Root 2\n" * 125)
        
        # Root archives
        root1_content = work / "level1_root1"
        root1_content.mkdir()
        shutil.copy(level1_content / "root_file_1.txt", root1_content / "root_file_1.txt")
        root1_archive = level1_content / "root_archive_1.tar.gz"
        self.create_tar_gz(root1_archive, root1_content)
        
        root2_content = work / "level1_root2"
        root2_content.mkdir()
        shutil.copy(level1_content / "root_file_2.txt", root2_content / "root_file_2.txt")
        root2_archive = level1_content / "root_archive_2.tar.gz"
        self.create_tar_gz(root2_archive, root2_content)
        
        # Remove direct files after creating archives
        (level1_content / "root_file_1.txt").unlink()
        (level1_content / "root_file_2.txt").unlink()
        
        # Root multipart (4 parts)
        root_multi_content = work / "level1_root_multi"
        root_multi_content.mkdir()
        self.create_binary_file(root_multi_content / "root_multi_content.txt", 20000)
        
        self.create_multipart(
            level1_content / "root_multipart",
            root_multi_content,
            part_size=7500,
            num_parts=4,
            min_parts=4
        )
        
        # Include level 2
        shutil.copy(level2_archive, level1_content / "level2_multi_set.tar.gz")
        
        level1_archive = work / "level1_final.tar.gz"
        self.create_tar_gz(level1_archive, level1_content)
        
        # ========================================
        # Ultimate level: Final stress test
        # ========================================
        print("  Ultimate: Creating final stress test archive")
        ultimate_content = work / "ultimate_content"
        ultimate_content.mkdir()
        
        # Include level 1
        shutil.copy(level1_archive, ultimate_content / "level1_final.tar.gz")
        
        # Add extra archives (8)
        for i in range(1, 9):
            extra_content = work / f"ultimate_extra_{i}"
            extra_content.mkdir()
            self.create_text_file(extra_content / f"extra_file_{i}.txt", f"Extra {i}\n" * (1000 * i // 10))
            
            extra_archive = ultimate_content / f"extra_archive_{i}.tar.gz"
            self.create_tar_gz(extra_archive, extra_content)
        
        # Ultimate multipart archives (3 sets, 4 parts each)
        for i in range(1, 4):
            ultimate_multi_content = work / f"ultimate_multi_{i}"
            ultimate_multi_content.mkdir()
            self.create_binary_file(ultimate_multi_content / f"ultimate_multi_{i}.txt", 15000 * i)
            
            self.create_multipart(
                ultimate_content / f"ultimate_multi_{i}",
                ultimate_multi_content,
                part_size=6000 * i,
                num_parts=4,
                min_parts=4
            )
        
        # Create final archive
        final = self.output_dir / "stress_test_ultimate.tar.gz"
        self.create_tar_gz(final, ultimate_content)
        
        print(f"✓ Created: {final}")
        print(f"  Structure: 10+ levels of nesting")
        print(f"  Archives: 30+ total")
        print(f"  Multipart sets: 15+ with various part sizes")
        return final
    
    def generate_all(self):
        """Generate all test data files"""
        print("=" * 60)
        print("Archive_r Test Data Generator")
        print("=" * 60)
        
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
        archives = [
            self.generate_simple_multipart_test(),
            self.generate_nested_with_multipart_test(),
            self.generate_deeply_nested_test(),
            self.generate_deeply_nested_multipart_test(),
            self.generate_stress_test_ultimate(),
        ]
        
        print("\n" + "=" * 60)
        print("Summary:")
        print("=" * 60)
        for archive in archives:
            size = archive.stat().st_size
            print(f"  {archive.name:40s} {size:8d} bytes")
        
        print("\n✓ All test data generated successfully!")


def main():
    script_dir = Path(__file__).parent
    output_dir = script_dir
    
    if len(sys.argv) > 1:
        output_dir = Path(sys.argv[1])
    
    with TestDataGenerator(output_dir) as gen:
        gen.generate_all()


if __name__ == "__main__":
    main()
