#!/usr/bin/env bash
set -euo pipefail

PRESET="${1:-default}"

if [ ! -f "build/CMakeCache.txt" ] && [ "${PRESET}" = "default" ]; then
  echo "Build directory is not configured yet. Run ./scripts/configure.sh first."
  exit 1
fi

cmake --build --preset "${PRESET}"
