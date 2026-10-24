#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
preset="${1:-release}"
if [[ $# -gt 0 ]]; then shift; fi
case "$preset" in release|debug|sanitize) ;; *) echo "Usage: $0 [release|debug|sanitize] [CMake arguments]" >&2; exit 1;; esac
cmake --preset "$preset" "$@"
cmake --build --preset "$preset" --parallel "${ORBITFIT_JOBS:-4}"
ctest --preset "$preset"
cmake --install "build/$preset" --prefix "$PWD/build/$preset/install" > "build/$preset/install.log"
cmake -S tests/consumer -B "build/$preset/consumer" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/build/$preset/install" \
  -DORBITFIT_PARENT_BUILD="$PWD/build/$preset"
cmake --build "build/$preset/consumer" --parallel "${ORBITFIT_JOBS:-4}"
ctest --test-dir "build/$preset/consumer" --output-on-failure
