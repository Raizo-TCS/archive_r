#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deps_script="${script_dir}/install-deps-ubuntu.sh"

if [[ ! -x "${deps_script}" ]]; then
  echo "Missing ${deps_script}" >&2
  exit 1
fi

pf=""
if [[ "${1:-}" == "--pf" ]]; then
  pf="${2:-}"
  shift 2
fi

export DEBIAN_FRONTEND=noninteractive

if [[ "${pf}" == "arm64-cross" ]]; then
  bash "${deps_script}" --cross --arch arm64 --clean-apt-lists
else
  bash "${deps_script}" --clean-apt-lists
fi
