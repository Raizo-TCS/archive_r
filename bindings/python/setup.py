# SPDX-License-Identifier: MIT
# Copyright (c) 2025 archive_r Team

import atexit
import os
import platform
import shutil
import sys
from pathlib import Path
from typing import Iterable, List, Optional, Tuple

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

# Ensure LICENSE.txt is present in the binding directory
if (archive_r_root / 'LICENSE.txt').exists():
    shutil.copy(archive_r_root / 'LICENSE.txt', local_license)

local_version = binding_root / 'VERSION'
system_name = platform.system().lower()
is_windows = system_name == 'windows' or os.name == 'nt'
print(f"DEBUG: system_name={system_name}, os.name={os.name}, sys.platform={sys.platform}")
libraries: List[str] = ['archive']
library_dirs: List[str] = []
include_dirs_override: List[str] = []
extra_link_args: List[str] = []
runtime_library_dirs: List[str] = []


def extend_path_entries(target: List[str], raw_value: Optional[str]) -> None:
    if not raw_value:
        return
    for entry in raw_value.split(os.pathsep):
        normalized = entry.strip()
        if normalized:
            target.append(normalized)


def configure_libarchive_paths_from_root(root_value: Optional[str]) -> None:
    if not root_value:
        return

    root = Path(root_value).expanduser().resolve(strict=False)
    include_candidates = [root / 'include']
    lib_candidates = [root / 'lib', root / 'lib64', root / 'lib/x86_64']

    for candidate in include_candidates:
        if candidate.exists():
            include_dirs_override.append(str(candidate))

    for candidate in lib_candidates:
        if candidate.exists():
            library_dirs.append(str(candidate))

    if not is_windows:
        for candidate in lib_candidates:
            if candidate.exists():
                runtime_library_dirs.append(str(candidate))


configure_libarchive_paths_from_root(os.environ.get('LIBARCHIVE_ROOT'))
extend_path_entries(include_dirs_override, os.environ.get('LIBARCHIVE_INCLUDE_DIRS'))
extend_path_entries(library_dirs, os.environ.get('LIBARCHIVE_LIBRARY_DIRS'))
extend_path_entries(runtime_library_dirs, os.environ.get('LIBARCHIVE_RUNTIME_DIRS'))

user_defined_libs = os.environ.get('LIBARCHIVE_LIBRARIES')
if user_defined_libs:
    libraries = [name.strip() for name in user_defined_libs.split(',') if name.strip()]

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
    return 'Fast archive traversal library with support for nested archives and multipart files.'


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

def find_prebuilt_library() -> Optional[Path]:
    candidates = [
        archive_r_build / 'libarchive_r_core.lib',
        archive_r_build / 'libarchive_r_core.a',
        archive_r_build / 'archive_r_core.lib',
        archive_r_build / 'Release' / 'libarchive_r_core.lib',
        archive_r_build / 'Release' / 'archive_r_core.lib',
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate

    return None


extra_objects: List[str] = []
static_lib = find_prebuilt_library()
if static_lib:
    extra_objects = [str(static_lib)]
    print(f"Using pre-built archive_r library: {static_lib}")
else:
    # Build from source as fallback
    print("Pre-built library not found, will compile from source")
    fallback_units = sorted(core_src_dir.glob('*.cc'))
    if not fallback_units:
        raise RuntimeError(f"No .cc files found under {core_src_dir} for fallback build")
    sources.extend([str(unit) for unit in fallback_units])


base_include_dirs = [
    pybind11_include,
    str(core_include_dir),
    str(core_src_dir),
]
if include_dirs_override:
    base_include_dirs.extend(include_dirs_override)

class BuildExt(build_ext):
    def build_extensions(self):
        compiler_type = self.compiler.compiler_type
        opts = []
        if compiler_type == 'msvc':
            opts = ['/std:c++17', '/EHsc', '/DNOMINMAX']
        else:
            opts = ['-std=c++17', '-fvisibility=hidden']
            if system_name == 'darwin':
                opts.append('-stdlib=libc++')
        
        for ext in self.extensions:
            ext.extra_compile_args = opts
        
        build_ext.build_extensions(self)

ext_modules = [
    Extension(
        'archive_r',
        sources=sources,
        include_dirs=base_include_dirs,
        library_dirs=library_dirs,
        libraries=libraries,
        extra_objects=extra_objects,
        language='c++',
        extra_link_args=extra_link_args,
        runtime_library_dirs=runtime_library_dirs,
        define_macros=[('ARCHIVE_R_VERSION', f'"{package_version}"')],
    ),
]


setup(
    version=package_version,
    cmdclass={'build_ext': BuildExt},
    ext_modules=ext_modules,
    long_description=read_readme(),
    long_description_content_type='text/markdown',
)
