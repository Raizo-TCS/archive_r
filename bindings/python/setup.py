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
libs_dir = binding_root / '.libs'
local_readme = binding_root / 'README.md'
local_license = binding_root / 'LICENSE'
local_notice = binding_root / 'NOTICE'

# Ensure LICENSE is present in the binding directory. Skip overwrite when a copy already exists
# to avoid permission issues from prior root-owned builds.
if (archive_r_root / 'LICENSE').exists() and not local_license.exists():
    shutil.copy(archive_r_root / 'LICENSE', local_license)

# Ensure NOTICE is present in the binding directory. Skip overwrite when a copy already exists
# to avoid permission issues from prior root-owned builds.
if (archive_r_root / 'NOTICE').exists() and not local_notice.exists():
    shutil.copy(archive_r_root / 'NOTICE', local_notice)

local_version = binding_root / 'VERSION'
system_name = platform.system().lower()
is_windows = system_name == 'windows' or os.name == 'nt'
target_triple = os.environ.get('ARCHIVE_R_TARGET_TRIPLE')
sysroot_override = os.environ.get('ARCHIVE_R_SYSROOT')
bootstrap_cmd = os.environ.get('ARCHIVE_R_BOOTSTRAP_CMD')
bootstrap_prefix = os.environ.get('ARCHIVE_R_BOOTSTRAP_PREFIX')
bootstrap_args = os.environ.get('ARCHIVE_R_BOOTSTRAP_ARGS', '')
auto_fetch_deps = os.environ.get('ARCHIVE_R_AUTO_FETCH_DEPS', '0') == '1'
bootstrap_scripts = []
if system_name == 'darwin':
    bootstrap_scripts.append(binding_root / 'tools' / 'build-deps-macos.sh')
bootstrap_scripts.append(binding_root / 'tools' / 'build-deps-manylinux.sh')
bootstrap_script = next((p for p in bootstrap_scripts if p.exists()), None)
force_source_build = os.environ.get('ARCHIVE_R_FORCE_SOURCE', '0') == '1'
print(f"DEBUG: system_name={system_name}, os.name={os.name}, sys.platform={sys.platform}")
libraries: List[str] = ['archive']
library_dirs: List[str] = []
include_dirs_override: List[str] = []
extra_link_args: List[str] = []
runtime_library_dirs: List[str] = []
extra_compile_args: List[str] = []
staged_shared_libs: List[Path] = []


def extend_path_entries(target: List[str], raw_value: Optional[str]) -> None:
    if not raw_value:
        return
    for entry in raw_value.split(os.pathsep):
        normalized = entry.strip()
        if normalized:
            target.append(normalized)


def stage_shared_libs(paths: Iterable[Path]) -> None:
    libs_dir.mkdir(parents=True, exist_ok=True)
    try:
        libs_dir.chmod(0o755)
    except PermissionError as exc:
        print(f"Warning: failed to chmod {libs_dir}: {exc}", file=sys.stderr)
    for candidate in paths:
        if not candidate.exists() or not candidate.is_file():
            continue
        if not any(str(candidate).endswith(ext) for ext in ('.so', '.dylib', '.dll')) and '.so.' not in candidate.name:
            continue
        target = libs_dir / candidate.name
        if target.exists():
            try:
                target.chmod(0o644)
            except PermissionError as exc:
                print(f"Warning: failed to chmod {target}: {exc}", file=sys.stderr)
            try:
                target.unlink()
            except PermissionError as exc:
                # Even if unlink is denied, continue and attempt copy2 overwrite.
                print(f"Warning: failed to unlink {target}: {exc}", file=sys.stderr)
        shutil.copy2(candidate, target)
        staged_shared_libs.append(target)


def collect_shared_from_prefix(prefix: Path) -> List[Path]:
    results: List[Path] = []
    for sub in ('lib', 'lib64', 'bin'):
        base = prefix / sub
        if not base.exists():
            continue
        for glob_pattern in ('*.so', '*.so.*', '*.dylib', '*.dll'):
            results.extend(base.glob(glob_pattern))
    return results


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
if sysroot_override:
    configure_libarchive_paths_from_root(sysroot_override)
    if not is_windows:
        extra_compile_args.append(f"--sysroot={sysroot_override}")
        extra_link_args.append(f"--sysroot={sysroot_override}")

bootstrap_root: Optional[Path] = None
if auto_fetch_deps:
    bootstrap_root = Path(bootstrap_prefix or (binding_root / '_deps')).expanduser().resolve()
    configure_libarchive_paths_from_root(str(bootstrap_root))


def run_bootstrap_if_requested() -> None:
    global bootstrap_cmd
    if not bootstrap_cmd and auto_fetch_deps:
        if not bootstrap_root:
            raise RuntimeError("bootstrap root was not resolved")
        if not bootstrap_script:
            raise RuntimeError("bootstrap script missing from sdist: tools/build-deps-*.sh")
        cmd_parts = ["bash", str(bootstrap_script), "--prefix", str(bootstrap_root)]
        # Insert --host derived from target_triple first to avoid duplicates
        host_injected = False
        if target_triple:
            cmd_parts.extend(["--host", target_triple])
            host_injected = True
        if bootstrap_args:
            args_parts = bootstrap_args.split()
            # Avoid double-injecting --host when already present
            if host_injected and "--host" in args_parts:
                filtered = []
                skip_next = False
                for part in args_parts:
                    if skip_next:
                        skip_next = False
                        continue
                    if part == "--host":
                        skip_next = True
                        continue
                    filtered.append(part)
                args_parts = filtered
            cmd_parts.extend(args_parts)
        bootstrap_cmd = " ".join(cmd_parts)

    if not bootstrap_cmd:
        return
    print(f"Running bootstrap command for dependencies: {bootstrap_cmd}")
    result = os.system(bootstrap_cmd)
    if result != 0:
        raise RuntimeError(f"bootstrap command failed with exit code {result}")


run_bootstrap_if_requested()

# Stage shared dependencies for bundling (bootstrap root and user-provided roots)
if bootstrap_root:
    stage_shared_libs(collect_shared_from_prefix(bootstrap_root))

libarchive_root_env = os.environ.get('LIBARCHIVE_ROOT')
if libarchive_root_env:
    stage_shared_libs(collect_shared_from_prefix(Path(libarchive_root_env)))

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
    copy_file(archive_r_root / 'LICENSE', local_license)
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

def find_prebuilt_shared_library() -> Optional[Path]:
    if target_triple:
        return None
    candidates = [
        archive_r_build / 'libarchive_r_core.so',
        archive_r_build / 'libarchive_r_core.dylib',
        archive_r_build / 'archive_r_core.dll',
        archive_r_build / 'libarchive_r_core.dll',
        archive_r_build / 'archive_r_core.lib',
        archive_r_build / 'Release' / 'archive_r_core.dll',
        archive_r_build / 'Release' / 'archive_r_core.lib',
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


prebuilt_shared = None if force_source_build else find_prebuilt_shared_library()
if prebuilt_shared:
    libraries.append('archive_r_core')
    library_dirs.append(str(prebuilt_shared.parent))
    if not is_windows:
        runtime_library_dirs.append(str(prebuilt_shared.parent))
    stage_shared_libs([prebuilt_shared])
    print(f"Using pre-built shared archive_r library: {prebuilt_shared}")
else:
    print("Pre-built shared library not found, will compile from source")
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

library_dirs.append(str(libs_dir))
if not is_windows:
    runtime_library_dirs.append(str(libs_dir))
    runtime_library_dirs.append('$ORIGIN/.libs')
    extra_link_args.append('-Wl,-rpath,$ORIGIN/.libs')

def _dedupe(seq: List[str]) -> List[str]:
    seen = set()
    out: List[str] = []
    for item in seq:
        if item in seen:
            continue
        seen.add(item)
        out.append(item)
    return out

library_dirs = _dedupe(library_dirs)
runtime_library_dirs = _dedupe(runtime_library_dirs)
extra_link_args = _dedupe(extra_link_args)

class BuildExt(build_ext):
    def get_ext_filename(self, ext_name: str) -> str:  # type: ignore[override]
        filename = super().get_ext_filename(ext_name)
        # When cross-compiling, allow overriding to match target architecture suffix
        override_suffix = os.environ.get('ARCHIVE_R_EXT_SUFFIX')
        if override_suffix:
            stem, _ = os.path.splitext(filename)
            return f"{stem}{override_suffix}"

        if target_triple and not is_windows:
            stem, _ = os.path.splitext(filename)
            py_ver = f"{sys.version_info.major}{sys.version_info.minor}"
            return f"{stem}.cpython-{py_ver}-{target_triple}.so"

        return filename

    def build_extensions(self):
        compiler_type = self.compiler.compiler_type
        opts = []
        if compiler_type == 'msvc':
            opts = ['/std:c++17', '/EHsc', '/DNOMINMAX']
        else:
            opts = ['-std=c++17', '-fvisibility=hidden']
            if system_name == 'darwin':
                opts.append('-stdlib=libc++')
            if sysroot_override:
                opts.append(f"--sysroot={sysroot_override}")
        opts.extend(extra_compile_args)
        
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
        language='c++',
        extra_link_args=extra_link_args,
        runtime_library_dirs=runtime_library_dirs,
        define_macros=[('ARCHIVE_R_VERSION', f'"{package_version}"')],
    ),
]


data_files = []
if staged_shared_libs:
    data_files.append(('.libs', [str(p) for p in staged_shared_libs]))

setup(
    version=package_version,
    cmdclass={'build_ext': BuildExt},
    ext_modules=ext_modules,
    long_description=read_readme(),
    long_description_content_type='text/markdown',
    data_files=data_files,
)
