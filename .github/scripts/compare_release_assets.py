#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import io
import os
import re
import shutil
import subprocess
import tarfile
import gzip
import zipfile
from dataclasses import dataclass
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Dict, Iterable, List, Optional, Tuple


IGNORED_PATH_PATTERNS = [
    re.compile(r"(^|/)\.DS_Store$"),
]


# The compare workflow is decision-support: we want to detect *release-meaningful*
# content updates, not toolchain/packaging metadata noise.
WHEEL_IGNORED_MEMBERS = {
    # Wheel RECORD files change whenever packaging/layout changes and often include
    # hashes and sizes (and can vary even when code is effectively the same).
    # For release decision support we treat it as noise.
    "RECORD",
}


def _is_wheel_ignored_member(path: str) -> bool:
    normalized = path.replace("\\", "/")
    if normalized.endswith(".dist-info/RECORD"):
        return True
    # delvewheel adds this file to record the repair operation (toolchain metadata).
    if normalized.endswith(".dist-info/DELVEWHEEL"):
        return True
    return False


_PE_IMPORT_DLL_RE = re.compile(r"^\s*DLL Name:\s*(?P<name>.+?)\s*$")


def _wheel_imports(path: Path) -> Optional[List[str]]:
    """Return imported DLL names from .pyd files inside a wheel, if supported.

    This runs on Linux in CI and relies on binutils 'objdump' to parse the PE import table.
    If objdump is unavailable, returns None (caller should skip wheel-specific heuristics).
    """

    objdump = shutil.which("objdump")
    if objdump is None:
        return None

    imports: set[str] = set()
    try:
        with zipfile.ZipFile(path, "r") as zf:
            pyd_members = [n for n in zf.namelist() if n.lower().endswith(".pyd")]
            if not pyd_members:
                return []

            with TemporaryDirectory() as td:
                td_path = Path(td)
                for member in pyd_members:
                    try:
                        out_path = td_path / Path(member).name
                        out_path.write_bytes(zf.read(member))
                    except Exception:
                        continue

                    try:
                        proc = subprocess.run(
                            [objdump, "-p", str(out_path)],
                            check=False,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True,
                        )
                        out = proc.stdout
                    except Exception:
                        continue

                    for line in out.splitlines():
                        m = _PE_IMPORT_DLL_RE.match(line)
                        if not m:
                            continue
                        dll = m.group("name").strip().lower()
                        if dll:
                            imports.add(dll)
    except Exception:
        return None

    return sorted(imports)


def _is_python_sdist_ignored_member(path: str) -> bool:
    normalized = path.replace("\\", "/")
    # Generated packaging metadata (PEP 517 / setuptools) that is not a source-of-truth
    # for code changes and may vary across toolchain versions.
    if normalized.endswith("/PKG-INFO"):
        return True
    if normalized.endswith(".egg-info/PKG-INFO"):
        return True
    if normalized.endswith(".egg-info/SOURCES.txt"):
        return True
    if normalized.endswith(".egg-info/dependency_links.txt"):
        return True
    if normalized.endswith(".egg-info/top_level.txt"):
        return True
    return False


def _is_ignored_member(path: str) -> bool:
    normalized = path.replace("\\", "/")
    return any(p.search(normalized) for p in IGNORED_PATH_PATTERNS)


def _sha256_bytes(data: bytes) -> str:
    h = hashlib.sha256()
    h.update(data)
    return h.hexdigest()


def _sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _is_zip(path: Path) -> bool:
    return path.suffix.lower() in {".whl", ".zip"}


def _is_tar(path: Path) -> bool:
    name = path.name.lower()
    return name.endswith(".tar.gz") or name.endswith(".tgz") or name.endswith(".gem")


def _is_gem(path: Path) -> bool:
    return path.name.lower().endswith(".gem")


@dataclass(frozen=True)
class CompareResult:
    status: str  # same|changed|error
    details: Dict[str, object]


def _zip_manifest(path: Path) -> Tuple[Dict[str, str], List[str]]:
    hashes: Dict[str, str] = {}
    errors: List[str] = []
    try:
        with zipfile.ZipFile(path, "r") as zf:
            for info in zf.infolist():
                name = info.filename
                if name.endswith("/"):
                    continue
                if _is_ignored_member(name):
                    continue
                if _is_wheel_ignored_member(name):
                    continue

                try:
                    data = zf.read(info)
                except Exception as e:  # noqa: BLE001
                    errors.append(f"read failed: {name}: {e}")
                    continue
                hashes[name] = _sha256_bytes(data)
    except Exception as e:  # noqa: BLE001
        errors.append(str(e))
    return hashes, errors


def _tar_manifest(path: Path) -> Tuple[Dict[str, str], Dict[str, str], List[str]]:
    file_hashes: Dict[str, str] = {}
    link_targets: Dict[str, str] = {}
    errors: List[str] = []
    try:
        with tarfile.open(path, "r:*") as tf:
            for member in tf.getmembers():
                name = member.name
                if name in {"pax_global_header"}:
                    continue
                if _is_ignored_member(name):
                    continue
                if _is_python_sdist_ignored_member(name):
                    continue
                if member.isdir():
                    continue
                if member.issym() or member.islnk():
                    link_targets[name] = member.linkname or ""
                    continue
                if member.isfile():
                    try:
                        f = tf.extractfile(member)
                        if f is None:
                            errors.append(f"extractfile returned None: {name}")
                            continue
                        data = f.read()
                    except Exception as e:  # noqa: BLE001
                        errors.append(f"read failed: {name}: {e}")
                        continue
                    file_hashes[name] = _sha256_bytes(data)
                    continue
                errors.append(f"unsupported member type: {name}")
    except Exception as e:  # noqa: BLE001
        errors.append(str(e))
    return file_hashes, link_targets, errors


def _gem_manifest(path: Path) -> Tuple[Dict[str, str], List[str]]:
    """Compute a content manifest for .gem, ignoring nested archive container metadata.

    Ruby gems are tar files containing (at least) data.tar.gz and metadata.gz.
    Comparing raw bytes of data.tar.gz is sensitive to tar metadata timestamps.
    Instead, we expand data.tar.gz and hash the extracted file contents.
    """
    hashes: Dict[str, str] = {}
    errors: List[str] = []
    try:
        with tarfile.open(path, "r:*") as gem_tf:
            data_tgz: Optional[bytes] = None
            metadata_gz: Optional[bytes] = None

            for member in gem_tf.getmembers():
                name = member.name
                if member.isdir() or name in {"pax_global_header"}:
                    continue
                if _is_ignored_member(name):
                    continue
                if not member.isfile():
                    continue
                try:
                    f = gem_tf.extractfile(member)
                    if f is None:
                        errors.append(f"extractfile returned None: {name}")
                        continue
                    payload = f.read()
                except Exception as e:  # noqa: BLE001
                    errors.append(f"read failed: {name}: {e}")
                    continue

                if name == "data.tar.gz":
                    data_tgz = payload
                elif name == "metadata.gz":
                    metadata_gz = payload
                else:
                    if name == "checksums.yaml.gz":
                        # Checksums are derived packaging metadata and can vary
                        # across RubyGems versions.
                        continue
                    # Other files are typically small metadata; hash raw bytes.
                    hashes[f"gem/{name}"] = _sha256_bytes(payload)

            # Note: we intentionally do NOT compare gem metadata.gz contents here.
            # It often embeds RubyGems/toolchain version and other non-functional
            # build metadata, which is not release-meaningful for this repository's
            # decision-support comparison.
            _ = metadata_gz

            if data_tgz is None:
                errors.append("data.tar.gz not found in gem")
            else:
                try:
                    with tarfile.open(fileobj=io.BytesIO(data_tgz), mode="r:gz") as data_tf:
                        for member in data_tf.getmembers():
                            name = member.name
                            if name in {"pax_global_header"}:
                                continue
                            if _is_ignored_member(name):
                                continue
                            if member.isdir():
                                continue
                            if member.issym() or member.islnk():
                                hashes[f"data/{name}"] = _sha256_bytes((member.linkname or "").encode("utf-8"))
                                continue
                            if member.isfile():
                                try:
                                    f = data_tf.extractfile(member)
                                    if f is None:
                                        errors.append(f"data extractfile returned None: {name}")
                                        continue
                                    payload = f.read()
                                except Exception as e:  # noqa: BLE001
                                    errors.append(f"data read failed: {name}: {e}")
                                    continue
                                hashes[f"data/{name}"] = _sha256_bytes(payload)
                except Exception as e:  # noqa: BLE001
                    errors.append(f"data.tar.gz parse failed: {e}")
    except Exception as e:  # noqa: BLE001
        errors.append(str(e))

    return hashes, errors


def compare_files(base_path: Path, new_path: Path) -> CompareResult:
    if _is_gem(base_path) and _is_gem(new_path):
        base_hashes, base_errors = _gem_manifest(base_path)
        new_hashes, new_errors = _gem_manifest(new_path)
        errors = base_errors + new_errors
        if errors:
            return CompareResult(status="error", details={"errors": errors})
        if base_hashes == new_hashes:
            return CompareResult(status="same", details={"members": len(base_hashes)})

        missing = sorted(set(base_hashes) - set(new_hashes))
        extra = sorted(set(new_hashes) - set(base_hashes))
        changed = sorted(
            name for name in set(base_hashes) & set(new_hashes) if base_hashes[name] != new_hashes[name]
        )

        return CompareResult(
            status="changed",
            details={
                "members_base": len(base_hashes),
                "members_new": len(new_hashes),
                "missing_members": missing,
                "extra_members": extra,
                "changed_members": changed,
                "missing_members_count": len(missing),
                "extra_members_count": len(extra),
                "changed_members_count": len(changed),
            },
        )

    if _is_zip(base_path) and _is_zip(new_path):
        base_hashes, base_errors = _zip_manifest(base_path)
        new_hashes, new_errors = _zip_manifest(new_path)
        errors = base_errors + new_errors
        if errors:
            return CompareResult(status="error", details={"errors": errors})
        if base_hashes == new_hashes:
            return CompareResult(status="same", details={"members": len(base_hashes)})
        missing = sorted(set(base_hashes) - set(new_hashes))
        extra = sorted(set(new_hashes) - set(base_hashes))
        changed = sorted(
            name for name in set(base_hashes) & set(new_hashes) if base_hashes[name] != new_hashes[name]
        )

        # Content-aware rename/move detection (e.g., delvewheel mangled DLL names).
        # If a base-only member has the same content hash as a new-only member,
        # treat it as moved instead of missing/extra.
        moved_members: List[str] = []
        extra_by_hash: Dict[str, List[str]] = {}
        for n in extra:
            extra_by_hash.setdefault(new_hashes[n], []).append(n)
        for h in extra_by_hash.values():
            h.sort()

        missing_remaining: List[str] = []
        used_extra: set[str] = set()
        for n in missing:
            h = base_hashes[n]
            candidates = extra_by_hash.get(h)
            if not candidates:
                missing_remaining.append(n)
                continue
            moved_to = candidates.pop(0)
            used_extra.add(moved_to)
            moved_members.append(f"{n} -> {moved_to}")

        extra_remaining = [n for n in extra if n not in used_extra]
        missing = missing_remaining
        extra = extra_remaining

        # Wheel-specific insights: surface meaningful dependency shifts.
        wheel_imports_added: List[str] = []
        wheel_imports_removed: List[str] = []
        if base_path.suffix.lower() == ".whl" and new_path.suffix.lower() == ".whl":
            base_imports = _wheel_imports(base_path)
            new_imports = _wheel_imports(new_path)
            if base_imports is not None and new_imports is not None:
                base_set = set(base_imports)
                new_set = set(new_imports)
                wheel_imports_added = sorted(new_set - base_set)
                wheel_imports_removed = sorted(base_set - new_set)

        return CompareResult(
            status="changed",
            details={
                "members_base": len(base_hashes),
                "members_new": len(new_hashes),
                "missing_members": missing,
                "extra_members": extra,
                "changed_members": changed,
                "missing_members_count": len(missing),
                "extra_members_count": len(extra),
                "changed_members_count": len(changed),
                "moved_members": moved_members,
                "moved_members_count": len(moved_members),
                "wheel_imports_added": wheel_imports_added,
                "wheel_imports_added_count": len(wheel_imports_added),
                "wheel_imports_removed": wheel_imports_removed,
                "wheel_imports_removed_count": len(wheel_imports_removed),
            },
        )

    if _is_tar(base_path) and _is_tar(new_path):
        base_files, base_links, base_errors = _tar_manifest(base_path)
        new_files, new_links, new_errors = _tar_manifest(new_path)
        errors = base_errors + new_errors
        if errors:
            return CompareResult(status="error", details={"errors": errors})

        if base_files == new_files and base_links == new_links:
            return CompareResult(
                status="same",
                details={"files": len(base_files), "links": len(base_links)},
            )

        missing_files = sorted(set(base_files) - set(new_files))
        extra_files = sorted(set(new_files) - set(base_files))
        changed_files = sorted(
            name for name in set(base_files) & set(new_files) if base_files[name] != new_files[name]
        )

        missing_links = sorted(set(base_links) - set(new_links))
        extra_links = sorted(set(new_links) - set(base_links))
        changed_links = sorted(
            name for name in set(base_links) & set(new_links) if base_links[name] != new_links[name]
        )

        return CompareResult(
            status="changed",
            details={
                "files_base": len(base_files),
                "files_new": len(new_files),
                "links_base": len(base_links),
                "links_new": len(new_links),
                "missing_files": missing_files,
                "extra_files": extra_files,
                "changed_files": changed_files,
                "missing_links": missing_links,
                "extra_links": extra_links,
                "changed_links": changed_links,
                "missing_files_count": len(missing_files),
                "extra_files_count": len(extra_files),
                "changed_files_count": len(changed_files),
                "missing_links_count": len(missing_links),
                "extra_links_count": len(extra_links),
                "changed_links_count": len(changed_links),
            },
        )

    # Fallback: byte-level comparison
    base_hash = _sha256_file(base_path)
    new_hash = _sha256_file(new_path)
    if base_hash == new_hash:
        return CompareResult(status="same", details={"sha256": base_hash})
    return CompareResult(status="changed", details={"base_sha256": base_hash, "new_sha256": new_hash})


def _collect_files(root: Path) -> List[Path]:
    if not root.exists():
        return []
    files: List[Path] = []
    for p in root.rglob("*"):
        if p.is_file():
            files.append(p)
    return files


def _basename_map(files: Iterable[Path]) -> Tuple[Dict[str, Path], List[str]]:
    groups: Dict[str, List[Path]] = {}
    for p in files:
        groups.setdefault(p.name, []).append(p)

    chosen: Dict[str, Path] = {}
    issues: List[str] = []

    for name, paths in groups.items():
        if len(paths) == 1:
            chosen[name] = paths[0]
            continue

        # If multiple files share the same basename (e.g., artifacts from multiple jobs),
        # ensure they are byte-identical; otherwise surface as an issue.
        digests = {}
        for p in paths:
            try:
                digests[str(p)] = _sha256_file(p)
            except Exception as e:  # noqa: BLE001
                issues.append(f"duplicate basename read failed: {name}: {p}: {e}")
        unique_digests = sorted(set(digests.values()))
        if len(unique_digests) == 1:
            # Deterministically pick the shortest path (more stable across artifact layouts)
            chosen[name] = sorted(paths, key=lambda x: (len(str(x)), str(x)))[0]
            issues.append(f"duplicate basename (identical content): {name} ({len(paths)} copies)")
        else:
            chosen[name] = sorted(paths, key=lambda x: (len(str(x)), str(x)))[0]
            issues.append(f"duplicate basename (different content): {name} ({len(paths)} copies)")

    return chosen, issues


def _render_markdown(report: Dict[str, object]) -> str:
    base_tag = report.get("base_tag", "")
    commit_sha = report.get("commit_sha", "")
    counts = report.get("counts", {})
    rows: List[Dict[str, object]] = report.get("results", [])  # type: ignore[assignment]

    md: List[str] = []
    md.append(f"## Release asset comparison\n")
    md.append(f"- Base tag: `{base_tag}`\n")
    md.append(f"- Commit: `{commit_sha}`\n")
    md.append(
        "- Summary: "
        + ", ".join(
            f"{k}={v}" for k, v in counts.items()  # type: ignore[union-attr]
        )
        + "\n"
    )
    md.append("\n")
    md.append("| File | Status | Notes |\n")
    md.append("|---|---:|---|\n")
    for r in rows:
        name = str(r.get("name"))
        status = str(r.get("status"))
        notes = ""
        details = r.get("details")
        if isinstance(details, dict):
            if status == "changed":
                parts: List[str] = []
                for key in (
                    "changed_members_count",
                    "missing_members_count",
                    "extra_members_count",
                    "changed_files_count",
                    "missing_files_count",
                    "extra_files_count",
                ):
                    if key in details and isinstance(details[key], int):
                        parts.append(f"{key}={details[key]}")
                notes = "; ".join(parts)
            elif status in {"missing", "new"}:
                notes = ""
            elif status == "error":
                errs = details.get("errors")
                if isinstance(errs, list) and errs:
                    notes = str(errs[0])
        md.append(f"| {name} | {status} | {notes} |\n")
    md.append("\n")

    def _render_lines_block(lines: List[str]) -> str:
        if not lines:
            return "*(none)*\n"
        safe_lines = [str(x) for x in lines]
        return "```text\n" + "\n".join(safe_lines) + "\n```\n"

    def _render_kv_block(details: Dict[str, object]) -> str:
        pairs: List[str] = []
        for k in (
            "members_base",
            "members_new",
            "files_base",
            "files_new",
            "links_base",
            "links_new",
            "missing_members_count",
            "extra_members_count",
            "changed_members_count",
            "moved_members_count",
            "wheel_imports_added_count",
            "wheel_imports_removed_count",
            "missing_files_count",
            "extra_files_count",
            "changed_files_count",
            "missing_links_count",
            "extra_links_count",
            "changed_links_count",
        ):
            v = details.get(k)
            if isinstance(v, int):
                pairs.append(f"- {k}: {v}\n")
        if not pairs:
            return ""
        return "".join(pairs) + "\n"

    md.append("## Details\n\n")
    any_details = False
    for r in rows:
        name = str(r.get("name"))
        status = str(r.get("status"))
        details = r.get("details")
        if status not in {"changed", "error"}:
            continue
        if not isinstance(details, dict):
            continue
        any_details = True
        md.append("<details>\n")
        md.append(f"<summary>{name} ({status})</summary>\n\n")

        if status == "error":
            errs = details.get("errors")
            if isinstance(errs, list):
                md.append("### errors\n\n")
                md.append(_render_lines_block([str(e) for e in errs]))
            md.append("</details>\n\n")
            continue

        md.append(_render_kv_block(details))

        # Zip/Gem member diffs
        for key in (
            "missing_members",
            "extra_members",
            "changed_members",
            "moved_members",
            "wheel_imports_added",
            "wheel_imports_removed",
        ):
            v = details.get(key)
            if isinstance(v, list):
                md.append(f"### {key}\n\n")
                md.append(_render_lines_block([str(x) for x in v]))

        # Tar file/link diffs
        for key in (
            "missing_files",
            "extra_files",
            "changed_files",
            "missing_links",
            "extra_links",
            "changed_links",
        ):
            v = details.get(key)
            if isinstance(v, list):
                md.append(f"### {key}\n\n")
                md.append(_render_lines_block([str(x) for x in v]))

        # Fallback byte-level hash comparison
        base_sha = details.get("base_sha256")
        new_sha = details.get("new_sha256")
        if isinstance(base_sha, str) and isinstance(new_sha, str):
            md.append("### sha256\n\n")
            md.append(f"- base_sha256: `{base_sha}`\n")
            md.append(f"- new_sha256: `{new_sha}`\n\n")

        md.append("</details>\n\n")

    if not any_details:
        md.append("(no changed/error files)\n")

    return "".join(md)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--base-dir", required=True)
    ap.add_argument("--new-dir", required=True)
    ap.add_argument("--base-tag", required=True)
    ap.add_argument("--commit-sha", required=True)
    ap.add_argument("--out-json", required=True)
    ap.add_argument("--out-md", required=True)
    args = ap.parse_args()

    base_dir = Path(args.base_dir)
    new_dir = Path(args.new_dir)

    base_files = _collect_files(base_dir)
    new_files = _collect_files(new_dir)

    base_map, base_errors = _basename_map(base_files)
    new_map, new_errors = _basename_map(new_files)

    results: List[Dict[str, object]] = []
    counts = {"same": 0, "changed": 0, "missing": 0, "new": 0, "error": 0}
    errors: List[str] = base_errors + new_errors

    all_names = sorted(set(base_map) | set(new_map))
    for name in all_names:
        if name not in base_map:
            results.append({"name": name, "status": "new", "details": {}})
            counts["new"] += 1
            continue
        if name not in new_map:
            results.append({"name": name, "status": "missing", "details": {}})
            counts["missing"] += 1
            continue
        try:
            cr = compare_files(base_map[name], new_map[name])
            results.append({"name": name, "status": cr.status, "details": cr.details})
            counts[cr.status] = counts.get(cr.status, 0) + 1
        except Exception as e:  # noqa: BLE001
            results.append({"name": name, "status": "error", "details": {"errors": [str(e)]}})
            counts["error"] += 1

    report = {
        "base_tag": args.base_tag,
        "commit_sha": args.commit_sha,
        "counts": counts,
        "errors": errors,
        "results": results,
    }

    Path(args.out_json).write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    Path(args.out_md).write_text(_render_markdown(report), encoding="utf-8")

    # Always succeed: this workflow is for decision support.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
