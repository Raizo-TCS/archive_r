#!/bin/bash
set -e

# Stress test data generator for archive_r
# This creates a highly complex nested archive structure with various patterns

TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="$TEST_DIR/stress_test_work"

echo "=== Creating Stress Test Archive ==="
echo "Working directory: $WORK_DIR"

# Clean up previous work directory
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# ========================================
# Helper Functions
# ========================================

create_text_file() {
    local filename="$1"
    local size="$2"
    echo "Creating $filename (size: $size bytes)"
    head -c "$size" /dev/urandom | base64 > "$filename"
}

create_multipart() {
    local base_name="$1"
    local part_size="$2"
    local num_parts="$3"
    
    echo "Creating multipart archive: $base_name with $num_parts parts of $part_size bytes each"
    
    # Create a tar archive first
    local tar_name="${base_name}.tar"
    tar -czf "$tar_name" $(ls *.txt 2>/dev/null || echo "")
    
    # Split into parts
    split -b "$part_size" -d -a 3 "$tar_name" "${base_name}.part"
    rm "$tar_name"
    
    # Rename parts to match expected pattern
    local i=1
    for part in ${base_name}.part*; do
        mv "$part" "${base_name}.part$(printf "%03d" $i)"
        i=$((i + 1))
    done
}

# ========================================
# Level 10: Deepest level - Simple files
# ========================================
echo ""
echo "=== Level 10: Creating deepest level files ==="
mkdir -p level10
cd level10

create_text_file "deep_file_001.txt" 1024
create_text_file "deep_file_002.txt" 2048
create_text_file "deep_file_003.txt" 512

tar -czf level10_archive.tar.gz *.txt
cd ..

# ========================================
# Level 9: Mix of multipart and regular archives
# ========================================
echo ""
echo "=== Level 9: Multiple archives in same directory ==="
mkdir -p level9
cd level9

create_text_file "l9_file_a.txt" 500
create_text_file "l9_file_b.txt" 750
tar -czf small_archive.tar.gz l9_file_*.txt
rm l9_file_*.txt

create_text_file "l9_multipart_content_1.txt" 10000
create_text_file "l9_multipart_content_2.txt" 15000
create_multipart "multi_small" 5000 5
rm l9_multipart_content_*.txt

cp ../level10/level10_archive.tar.gz .

tar -czf level9_container.tar.gz *.tar.gz *.part*
cd ..

# ========================================
# Level 8: Large multipart archive (parts > 65536)
# ========================================
echo ""
echo "=== Level 8: Large multipart with big parts ==="
mkdir -p level8
cd level8

create_text_file "large_content_1.txt" 100000
create_text_file "large_content_2.txt" 150000
create_text_file "large_content_3.txt" 80000

cp ../level9/level9_container.tar.gz .

create_multipart "large_multi" 100000 4
rm large_content_*.txt

tar -czf level8_big_parts.tar.gz *.part* level9_container.tar.gz
cd ..

# ========================================
# Level 7: Many small archives in same level (10+)
# ========================================
echo ""
echo "=== Level 7: Many archives at same level ==="
mkdir -p level7
cd level7

for i in {01..12}; do
    mkdir -p "subdir_$i"
    cd "subdir_$i"
    create_text_file "file_${i}_a.txt" 200
    create_text_file "file_${i}_b.txt" 300
    tar -czf "../archive_${i}.tar.gz" *.txt
    cd ..
    rm -rf "subdir_$i"
done

cp ../level8/level8_big_parts.tar.gz .

tar -czf level7_many_archives.tar.gz *.tar.gz
cd ..

# ========================================
# Level 6: Multipart with small parts (< 65536)
# ========================================
echo ""
echo "=== Level 6: Multipart with small parts ==="
mkdir -p level6
cd level6

create_text_file "small_part_content.txt" 50000

cp ../level7/level7_many_archives.tar.gz .

create_multipart "tiny_parts" 8192 8
rm small_part_content.txt

tar -czf level6_tiny_parts.tar.gz *.part* level7_many_archives.tar.gz
cd ..

# ========================================
# Level 5: Nested multipart archives
# ========================================
echo ""
echo "=== Level 5: Nested multipart ==="
mkdir -p level5
cd level5

create_text_file "l5_data_1.txt" 5000
create_text_file "l5_data_2.txt" 7000

cp ../level6/level6_tiny_parts.tar.gz .

create_multipart "nested_multi" 12000 3
rm l5_data_*.txt

# Create another multipart
create_text_file "l5_extra.txt" 3000
create_multipart "extra_multi" 6000 2
rm l5_extra.txt

tar -czf level5_multi_nest.tar.gz *.part* level6_tiny_parts.tar.gz
cd ..

# ========================================
# Level 4: Deep directory structure with archives
# ========================================
echo ""
echo "=== Level 4: Deep directory paths with archives ==="
mkdir -p level4
cd level4

# Create deep directory structure with archive at the end
mkdir -p "very/long/directory/path/structure/that/goes/deep/into/filesystem"
cd "very/long/directory/path/structure/that/goes/deep/into/filesystem"
create_text_file "deeply_nested_file.txt" 1000

# Place archives deep in the directory structure
cp ../../../../../../../../../../../level5/level5_multi_nest.tar.gz .

# Create a multipart archive deep in the directory
create_text_file "deep_content_1.txt" 8000
create_text_file "deep_content_2.txt" 12000
create_multipart "deep_multi" 10000 2
rm deep_content_*.txt

# Create a nested archive deep in the directory
create_text_file "deep_nested.txt" 2000
tar -czf deep_nested.tar.gz deep_nested.txt *.part*
rm deep_nested.txt *.part*

cd ../../../../../../../../../../
tar -czf deep_dirs.tar.gz very/
rm -rf very/

cp ../level5/level5_multi_nest.tar.gz .

tar -czf level4_deep_paths.tar.gz deep_dirs.tar.gz level5_multi_nest.tar.gz
cd ..

# ========================================
# Level 3: Alternating archive and multipart
# ========================================
echo ""
echo "=== Level 3: Alternating patterns ==="
mkdir -p level3
cd level3

# Regular archive
create_text_file "regular_1.txt" 2000
tar -czf regular_archive_1.tar.gz regular_1.txt
rm regular_1.txt

# Multipart
create_text_file "multi_content_1.txt" 8000
create_multipart "alternating_multi_1" 4000 3
rm multi_content_1.txt

# Regular archive
create_text_file "regular_2.txt" 3000
tar -czf regular_archive_2.tar.gz regular_2.txt
rm regular_2.txt

# Multipart
create_text_file "multi_content_2.txt" 10000
create_multipart "alternating_multi_2" 5000 3
rm multi_content_2.txt

cp ../level4/level4_deep_paths.tar.gz .

tar -czf level3_alternating.tar.gz *.tar.gz *.part*
cd ..

# ========================================
# Level 2: Many multipart archives + deep paths
# ========================================
echo ""
echo "=== Level 2: Multiple multipart archives with deep paths ==="
mkdir -p level2
cd level2

# Multiple multipart archives
for i in {1..5}; do
    create_text_file "multi_set_${i}.txt" $((5000 * i))
    create_multipart "multi_set_${i}" $((2000 + i * 1000)) 3
    rm "multi_set_${i}.txt"
done

# Add another deep directory pattern with archives at different depths
mkdir -p "another/deep/path/to/test/traversal/capabilities"
cd "another/deep/path/to/test/traversal/capabilities"

# Create multipart deep in this path
create_text_file "path_content.txt" 6000
create_multipart "path_multi" 3000 2
rm path_content.txt

# Create regular archive deep in this path
create_text_file "path_regular.txt" 4000
tar -czf path_archive.tar.gz path_regular.txt *.part*
rm path_regular.txt *.part*

cd ../../../../../../..
tar -czf deep_path_2.tar.gz another/
rm -rf another/

cp ../level3/level3_alternating.tar.gz .

tar -czf level2_multi_set.tar.gz *.part* *.tar.gz
cd ..

# ========================================
# Level 1: Top level with everything
# ========================================
echo ""
echo "=== Level 1: Final composition ==="
mkdir -p level1
cd level1

# Add some direct files
create_text_file "root_file_1.txt" 1500
create_text_file "root_file_2.txt" 2500

# Add multiple archives at root
tar -czf "root_archive_1.tar.gz" root_file_1.txt
tar -czf "root_archive_2.tar.gz" root_file_2.txt
rm root_file_*.txt

# Add a multipart at root
create_text_file "root_multi_content.txt" 20000
create_multipart "root_multipart" 7500 4
rm root_multi_content.txt

# Include previous level
cp ../level2/level2_multi_set.tar.gz .

# Create final archive
tar -czf level1_final.tar.gz *.tar.gz *.part*
cd ..

# ========================================
# Create the ultimate stress test archive
# ========================================
echo ""
echo "=== Creating ultimate stress test archive ==="
mkdir -p ultimate
cd ultimate

cp ../level1/level1_final.tar.gz .

# Add more complexity at this level
for i in {1..8}; do
    mkdir -p "extra_${i}"
    cd "extra_${i}"
    create_text_file "extra_file_${i}.txt" $((1000 * i))
    tar -czf "../extra_archive_${i}.tar.gz" *.txt
    cd ..
    rm -rf "extra_${i}"
done

# Create multipart archives
for i in {1..3}; do
    create_text_file "ultimate_multi_${i}.txt" $((15000 * i))
    create_multipart "ultimate_multi_${i}" $((6000 * i)) 4
    rm "ultimate_multi_${i}.txt"
done

# Final packaging
tar -czf stress_test_ultimate.tar.gz *.tar.gz *.part*
cd ..

# Move final archive to test_data
mv ultimate/stress_test_ultimate.tar.gz "$TEST_DIR/stress_test_ultimate.tar.gz"

# ========================================
# Cleanup
# ========================================
echo ""
echo "=== Cleaning up work directory ==="
cd "$TEST_DIR"
rm -rf "$WORK_DIR"

echo ""
echo "✓ Stress test archive created: $TEST_DIR/stress_test_ultimate.tar.gz"
echo ""
echo "Archive structure summary:"
echo "  - 10+ levels of nesting"
echo "  - Mix of regular archives and multipart archives"
echo "  - Multipart with parts < 65536 bytes and > 65536 bytes"
echo "  - 10+ archives at same level in multiple places"
echo "  - Deep directory structures"
echo "  - Alternating archive/multipart patterns"
echo "  - Total archives: 30+"
echo "  - Total multipart archives: 15+"
