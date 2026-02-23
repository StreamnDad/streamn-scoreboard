#!/usr/bin/env bash
set -euo pipefail

OBS_BIN="${OBS_APP_PATH:-/Applications/OBS.app/Contents/MacOS/OBS}"

if [ ! -x "${OBS_BIN}" ]; then
  echo "OBS executable not found: ${OBS_BIN}"
  echo "Set OBS_APP_PATH to your OBS executable path."
  exit 1
fi

LOCAL_PLUGIN_PATH="${PWD}/build"
OBS_USER_PLUGIN_PATH="${HOME}/Library/Application Support/obs-studio/plugins"

# OBS can discover plugins from explicit plugin paths.
export OBS_PLUGINS_PATH="${LOCAL_PLUGIN_PATH}:${OBS_USER_PLUGIN_PATH}:${OBS_PLUGINS_PATH:-}"
export DYLD_LIBRARY_PATH="${LOCAL_PLUGIN_PATH}:${DYLD_LIBRARY_PATH:-}"

exec "${OBS_BIN}" --verbose
