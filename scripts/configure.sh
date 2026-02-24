#!/usr/bin/env bash
set -euo pipefail

PRESET="${1:-default}"
shift || true

EXTRA_ARGS=()

# Auto-detect Qt5 on macOS via Homebrew (keg-only, needs explicit prefix path)
if [[ "$(uname)" == "Darwin" ]]; then
  QT5_PREFIX="$(brew --prefix qt@5 2>/dev/null || true)"
  if [[ -n "$QT5_PREFIX" && -d "$QT5_PREFIX" ]]; then
    EXTRA_ARGS+=("-DCMAKE_PREFIX_PATH=${QT5_PREFIX}")
  fi
fi

cmake --preset "${PRESET}" "${EXTRA_ARGS[@]}" "$@"
