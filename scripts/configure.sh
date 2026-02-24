#!/usr/bin/env bash
set -euo pipefail

PRESET="${1:-default}"
shift || true

EXTRA_ARGS=()

# Auto-detect Qt on macOS.
# OBS 32 uses Qt6, so when OBS.app is installed locally we skip the Homebrew
# Qt prefix and let CMake use the framework fallback — which compiles against
# Qt headers from Homebrew but links against OBS.app's bundled Qt frameworks,
# avoiding a dual-Qt-runtime crash.  On CI (no OBS.app) we still pass the
# Homebrew qt@5 prefix so find_package(Qt5) succeeds.
if [[ "$(uname)" == "Darwin" ]]; then
  OBS_FW_DIR="/Applications/OBS.app/Contents/Frameworks"
  if [[ -d "${OBS_FW_DIR}/QtCore.framework" ]]; then
    EXTRA_ARGS+=("-DUSE_OBS_QT_FRAMEWORKS=ON")
    # OBS.app present — let CMake use its framework fallback
  else
    QT5_PREFIX="$(brew --prefix qt@5 2>/dev/null || true)"
    if [[ -n "$QT5_PREFIX" && -d "$QT5_PREFIX" ]]; then
      EXTRA_ARGS+=("-DCMAKE_PREFIX_PATH=${QT5_PREFIX}")
    fi
  fi
fi

cmake --preset "${PRESET}" ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} "$@"
