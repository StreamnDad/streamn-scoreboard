#!/usr/bin/env bash
set -euo pipefail

PLUGIN_NAME="streamn-obs-scoreboard"
LOG_DIR="${HOME}/Library/Application Support/obs-studio/logs"

LATEST_LOG="$(ls -1t "${LOG_DIR}"/*.txt 2>/dev/null | head -n 1 || true)"
if [ -z "${LATEST_LOG}" ]; then
  echo "No OBS logs found in ${LOG_DIR}"
  exit 1
fi

echo "Latest log: ${LATEST_LOG}"

echo "--- Matches for ${PLUGIN_NAME} ---"
if ! rg -n "${PLUGIN_NAME}|\[streamn-obs-scoreboard\]" "${LATEST_LOG}"; then
  echo "No plugin log lines found for ${PLUGIN_NAME}."
  echo "After launching OBS, look for:"
  echo "  Loading module: ${PLUGIN_NAME}"
  echo "  [streamn-obs-scoreboard] module loaded"
fi
