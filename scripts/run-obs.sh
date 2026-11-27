#!/usr/bin/env bash
set -euo pipefail

OBS_BIN="${OBS_APP_PATH:-/Applications/OBS.app/Contents/MacOS/OBS}"

if [ ! -x "${OBS_BIN}" ]; then
  echo "OBS executable not found: ${OBS_BIN}" >&2
  echo "Set OBS_APP_PATH to your OBS executable path." >&2
  exit 1
fi

# OBS already searches its normal per-user plugin directory. If supplied by
# the caller, OBS_PLUGINS_PATH is one root containing complete .plugin bundles.
exec "${OBS_BIN}" --verbose "$@"
