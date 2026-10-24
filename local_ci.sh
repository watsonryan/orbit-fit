#!/usr/bin/env bash
# @file local_ci.sh
# @brief Build, test, relocate an installation, and audit its standalone consumer for one preset.
# @author watson
set -euo pipefail
cd "$(dirname "$0")"
preset="${1:-release}"
if [[ $# -gt 0 ]]; then shift; fi
case "$preset" in release|debug|sanitize) ;; *) echo "Usage: $0 [release|debug|sanitize] [CMake arguments]" >&2; exit 1;; esac
cmake --preset "$preset" "$@"
cmake --build --preset "$preset" --parallel "${ORBITFIT_JOBS:-4}"
ctest --preset "$preset"
cmake --build --preset "$preset" --target package-check
