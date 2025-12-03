# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

import atexit
import os
import shutil
import sys
from pathlib import Path
from typing import Iterable, List, Tuple

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext


binding_root = Path(__file__).resolve().parent
archive_r_root = binding_root.parents[1]
archive_r_build = archive_r_root / 'build'
vendor_root = binding_root / '_vendor' / 'archive_r'
vendor_include = vendor_root / 'include'
vendor_src = vendor_root / 'src'
local_readme = binding_root / 'README.md'
local_license = binding_root / 'LICENSE.txt'
local_version = binding_root / 'VERSION'

generated_paths: List[Tuple[Path, str]] = []


def track_generated(path: Path, kind: str) -> None:
    generated_paths.append((path, kind))


def copy_file(source: Path, target: Path) -> bool:
    if not source.exists():
        return False
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    track_generated(target, 'file')
    return True


def copy_tree(source: Path, target: Path) -> bool:
    if not source.exists():
        return False
    if target.exists():
        shutil.rmtree(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, target)
    track_generated(target, 'dir')
    return True


def cleanup_generated() -> None:
    for path, kind in reversed(generated_paths):
        try:
            if kind == 'dir':
                shutil.rmtree(path)
            else:
                path.unlink()
        except FileNotFoundError:
            pass

    removable = sorted({path.parent for path, _ in generated_paths}, key=lambda p: len(p.parts), reverse=True)
    for directory in removable:
        if directory == binding_root:
            continue
        try:
            directory.rmdir()
        except OSError:
            pass


atexit.register(cleanup_generated)


def prepare_distribution_assets() -> None:
    copy_file(archive_r_root / 'LICENSE.txt', local_license)
    copy_file(archive_r_root / 'VERSION', local_version)
    copy_tree(archive_r_root / 'include', vendor_include)
    copy_tree(archive_r_root / 'src', vendor_src)


def read_first_existing(paths: Iterable[Path], default: str = '') -> str:
    for candidate in paths:
        if candidate.exists():
            content = candidate.read_text(encoding='utf-8').strip()
            if content:
                return content
    return default


def read_version() -> str:
    return read_first_existing(
        [archive_r_root / 'VERSION', local_version],
        default='0.0.0',
    )


def read_readme() -> str:
    paths = [local_readme, archive_r_root / 'README.md']
    for candidate in paths:
        if candidate.exists():
            return candidate.read_text(encoding='utf-8')
    return 'Python bindings for archive_r that recursively traverse nested archives without creating temporary extraction files.'


def resolve_core_paths() -> Tuple[Path, Path]:
    include_dir = archive_r_root / 'include'
    src_dir = archive_r_root / 'src'
    if include_dir.exists() and src_dir.exists():
        return include_dir, src_dir

    include_dir = vendor_include
    src_dir = vendor_src
    if include_dir.exists() and src_dir.exists():
        return include_dir, src_dir

    raise RuntimeError('archive_r core sources are missing. Run build.sh to generate vendor files.')


prepare_distribution_assets()

try:
    import pybind11

    pybind11_include = pybind11.get_include()
except ImportError:
    print("Error: pybind11 is required. Install it with: pip install pybind11")
    sys.exit(1)


package_version = read_version()
core_include_dir, core_src_dir = resolve_core_paths()

sources = ['src/archive_r_py.cc']

# Try to use pre-built library first
extra_objects: List[str] = []
static_lib = archive_r_build / 'libarchive_r_core.a'
if static_lib.exists():
    extra_objects = [str(static_lib)]
    print(f"Using pre-built archive_r library: {static_lib}")
else:
    # Build from source as fallback
    print("Pre-built library not found, will compile from source")
    fallback_units = sorted(core_src_dir.glob('*.cc'))
    if not fallback_units:
        raise RuntimeError(f"No .cc files found under {core_src_dir} for fallback build")
    sources.extend([str(unit) for unit in fallback_units])


class BuildExt(build_ext):
    def build_extensions(self):
        compiler_type = self.compiler.compiler_type
        opts = []
        if compiler_type == 'msvc':
            opts.append('/std:c++17')
        else:
            opts.append('-std=c++17')

        for ext in self.extensions:
            ext.extra_compile_args = opts

        build_ext.build_extensions(self)


include_dirs = [
    pybind11_include,
    str(core_include_dir),
    str(core_src_dir),
]
library_dirs = []

libarchive_root = os.environ.get('LIBARCHIVE_ROOT')
print(f"DEBUG: LIBARCHIVE_ROOT={libarchive_root}")
if libarchive_root:
    include_dirs.append(os.path.join(libarchive_root, 'include'))
    library_dirs.append(os.path.join(libarchive_root, 'lib'))

ext_modules = [
    Extension(
        'archive_r',
        sources=sources,
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=['archive'],
        extra_objects=extra_objects,
        language='c++',
        define_macros=[('ARCHIVE_R_VERSION', f'"{package_version}"')],
    ),
]


setup(
    version=package_version,
    ext_modules=ext_modules,
    cmdclass={'build_ext': BuildExt},
    long_description=read_readme(),
    long_description_content_type='text/markdown',
    license_files=['LICENSE.txt'],
)
