#!/usr/bin/env python3
import re
import sys
from pathlib import Path

RE_VERSION = re.compile(r'^([A-Z0-9_]+)="\$\{\1:-([^}]+)\}"$')
RE_DEP = re.compile(r'^#\s*@dep\s+(.+)$')
RE_BUILD_FN = re.compile(r'^(build_[a-z0-9_]+)\(\)\s*\{\s*$')
RE_BUILD_CALL = re.compile(r'^(build_[a-z0-9_]+)\s*$')

REQUIRED_KEYS = [
    "name",
    "version_env",
    "cpe_vendor",
    "cpe_product",
    "build_fn",
]


def parse_kv(text: str) -> dict:
    result = {}
    for token in text.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        result[key.strip()] = value.strip()
    return result


def fail(messages: list[str]) -> int:
    for msg in messages:
        print(f"[dep-metadata] ERROR: {msg}", file=sys.stderr)
    return 1


def main() -> int:
    script_path = Path(__file__).with_name("build-deps-manylinux.sh")
    lines = script_path.read_text(encoding="utf-8").splitlines()

    versions = {}
    for line in lines:
        match = RE_VERSION.match(line.strip())
        if match:
            versions[match.group(1)] = match.group(2)

    dep_rows = []
    build_functions = set()
    build_calls = []

    for index, line in enumerate(lines, start=1):
        stripped = line.strip()

        dep_match = RE_DEP.match(stripped)
        if dep_match:
            data = parse_kv(dep_match.group(1))
            data["_line"] = str(index)
            dep_rows.append(data)

        fn_match = RE_BUILD_FN.match(stripped)
        if fn_match:
            build_functions.add(fn_match.group(1))

        call_match = RE_BUILD_CALL.match(stripped)
        if call_match:
            function_name = call_match.group(1)
            if function_name.startswith("build_"):
                build_calls.append(function_name)

    errors = []

    if not dep_rows:
        errors.append("No @dep metadata entries found.")
        return fail(errors)

    names = set()
    version_envs = set()
    build_fns_from_metadata = set()

    for row in dep_rows:
        line_no = row.get("_line", "?")
        for key in REQUIRED_KEYS:
            if not row.get(key):
                errors.append(f"Line {line_no}: missing required key '{key}'.")

        name = row.get("name")
        if name:
            if name in names:
                errors.append(f"Line {line_no}: duplicate name '{name}'.")
            names.add(name)

        version_env = row.get("version_env")
        if version_env:
            if version_env in version_envs:
                errors.append(f"Line {line_no}: duplicate version_env '{version_env}'.")
            version_envs.add(version_env)
            if version_env not in versions:
                errors.append(f"Line {line_no}: version_env '{version_env}' is not defined in version variables.")

        build_fn = row.get("build_fn")
        if build_fn:
            if build_fn in build_fns_from_metadata:
                errors.append(f"Line {line_no}: duplicate build_fn '{build_fn}'.")
            build_fns_from_metadata.add(build_fn)
            if build_fn not in build_functions:
                errors.append(f"Line {line_no}: build_fn '{build_fn}' does not exist as a function.")

    called_builds = [fn for fn in build_calls if fn in build_functions]
    called_set = set(called_builds)

    missing_metadata = sorted(called_set - build_fns_from_metadata)
    if missing_metadata:
        errors.append(
            "Missing @dep metadata for called build functions: " + ", ".join(missing_metadata)
        )

    extra_metadata = sorted(build_fns_from_metadata - called_set)
    if extra_metadata:
        errors.append(
            "@dep metadata exists for functions not called in build order: " + ", ".join(extra_metadata)
        )

    if errors:
        return fail(errors)

    print(f"[dep-metadata] OK: validated {len(dep_rows)} dependencies in {script_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())