# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

cmake_minimum_required(VERSION 3.10)
project(archive_r_experimental2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(UNIX)
    add_definitions(-D_FILE_OFFSET_BITS=64)
endif()

find_package(LibArchive REQUIRED)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${LibArchive_INCLUDE_DIRS}
)

set(LIBRARY_SOURCES
    src/archive_type.cc
    src/data_stream.cc
    src/archive_stack_orchestrator.cc
    src/archive_stack_cursor.cc
    src/multi_volume_manager.cc
    src/multi_volume_stream_base.cc
    src/system_file_stream.cc
    src/path_hierarchy.cc
    src/path_hierarchy_utils.cc
    src/traverser.cc
    src/entry.cc
    src/entry_fault.cc
    src/entry_fault_error.cc
)

add_library(archive_r_experimental2 STATIC ${LIBRARY_SOURCES})
target_link_libraries(archive_r_experimental2 ${LibArchive_LIBRARIES})

# ========================================
# Examples
# ========================================
add_executable(find_and_traverse examples/find_and_traverse.cc)
target_link_libraries(find_and_traverse archive_r_experimental2 ${LibArchive_LIBRARIES})

# benchmark_stream removed - FileStream/MultipartFileStream no longer exist

# ========================================
# Tests
# ========================================
add_executable(test_iterator test/test_iterator.cc)
target_link_libraries(test_iterator archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_skip_descent test/test_skip_descent.cc)
target_link_libraries(test_skip_descent archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_multi_volume_iterator test/test_multi_volume_iterator.cc)
target_link_libraries(test_multi_volume_iterator archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_multi_volume_input test/test_multi_volume_input.cc)
target_link_libraries(test_multi_volume_input archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_multi_volume_debug test/test_multi_volume_debug.cc)
target_link_libraries(test_multi_volume_debug archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_multi_volume_functionality test/test_multi_volume_functionality.cc)
target_link_libraries(test_multi_volume_functionality archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_multi_volume_retry test/test_multi_volume_retry.cc)
target_link_libraries(test_multi_volume_retry archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_multi_volume_ordering test/test_multi_volume_ordering.cc)
target_link_libraries(test_multi_volume_ordering archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_multi_volume_traversal_regression test/test_multi_volume_traversal_regression.cc)
target_link_libraries(test_multi_volume_traversal_regression archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_multi_volume_entry_persistence test/test_multi_volume_entry_persistence.cc)
target_link_libraries(test_multi_volume_entry_persistence archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_directory_container test/test_directory_container.cc)
target_link_libraries(test_directory_container archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_metadata_filesystem test/test_metadata_filesystem.cc)
target_link_libraries(test_metadata_filesystem archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_directory_navigation test/test_directory_navigation.cc)
target_link_libraries(test_directory_navigation archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_descent test/test_descent.cc)
target_link_libraries(test_descent archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_simple_count test/test_simple_count.cc)
target_link_libraries(test_simple_count archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_stress_ultimate_verification test/test_stress_ultimate_verification.cc)
target_link_libraries(test_stress_ultimate_verification archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_entry_read test/test_entry_read.cc)
target_link_libraries(test_entry_read archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_error_handling_step0 test/test_error_handling_step0.cc)
target_link_libraries(test_error_handling_step0 archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_data_source_reader_read test/test_data_source_reader_read.cc)
target_link_libraries(test_data_source_reader_read archive_r_experimental2 ${LibArchive_LIBRARIES})

# test_stress_ultimate_validation uses old API (ICallback) - disabled
# add_executable(test_stress_ultimate_validation test/test_stress_ultimate_validation.cc)
# target_link_libraries(test_stress_ultimate_validation archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_stress_multi_volume_source test/test_stress_multi_volume_source.cc)
target_link_libraries(test_stress_multi_volume_source archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_metadata_options test/test_metadata_options.cc)
target_link_libraries(test_metadata_options archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_multi_root_traversal test/test_multi_root_traversal.cc)
target_link_libraries(test_multi_root_traversal archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_root_file_multi_volume test/test_root_file_multi_volume.cc)
target_link_libraries(test_root_file_multi_volume archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_custom_root_stream test/test_custom_root_stream.cc)
target_link_libraries(test_custom_root_stream archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_threaded_traverser test/test_threaded_traverser.cc)
target_link_libraries(test_threaded_traverser archive_r_experimental2 ${LibArchive_LIBRARIES})

add_executable(test_performance_compare test/test_performance_no_descend.cc)
target_link_libraries(test_performance_compare archive_r_experimental2 ${LibArchive_LIBRARIES})


