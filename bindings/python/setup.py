# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

import os
import sys
from pathlib import Path
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext

# Check for pybind11 early
try:
    import pybind11
    pybind11_include = pybind11.get_include()
except ImportError:
    print("Error: pybind11 is required. Install it with: pip install pybind11")
    sys.exit(1)

# Get archive_r root directory
archive_r_root = Path(__file__).parent.parent.parent.absolute()
archive_r_include = archive_r_root / 'include'
archive_r_src = archive_r_root / 'src'
archive_r_build = archive_r_root / 'build'

# Source files
sources = ['src/archive_r_py.cc']

# Try to use pre-built library first
extra_objects = []
static_lib = archive_r_build / 'libarchive_r_core.a'
if static_lib.exists():
    extra_objects = [str(static_lib)]
    print(f"Using pre-built archive_r library: {static_lib}")
else:
    # Build from source as fallback
    print("Pre-built library not found, will compile from source")
    sources.extend([
        str(archive_r_src / 'archive_type.cc'),
        str(archive_r_src / 'libarchive_common.cc'),
        str(archive_r_src / 'nested_archive_reader.cc'),
        str(archive_r_src / 'traverser.cc'),
        str(archive_r_src / 'navigation_state.cc'),
        str(archive_r_src / 'archive_container.cc'),
        str(archive_r_src / 'directory_container.cc'),
    str(archive_r_src / 'entry.cc'),
        str(archive_r_src / 'stream.cc'),
    ])

ext_modules = [
    Extension(
        'archive_r',
        sources=sources,
        include_dirs=[
            pybind11_include,
            str(archive_r_include),
            str(archive_r_src),
        ],
        libraries=['archive'],
        extra_objects=extra_objects,
        language='c++',
        extra_compile_args=['-std=c++17'],
    ),
]

setup(
    name='archive_r',
    version='0.1.0',
    author='archive_r Team',
    description='Python bindings for archive_r library',
    long_description='Fast archive traversal library with support for nested archives and multipart files',
    ext_modules=ext_modules,
    python_requires='>=3.7',
    install_requires=['pybind11>=2.6.0'],
    zip_safe=False,
)
