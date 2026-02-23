#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build-coverage}"
OBJ_DIR="${BUILD_DIR}/CMakeFiles/scoreboard_core.dir/src"
OUT_DIR="${BUILD_DIR}/coverage"

if [ ! -d "${BUILD_DIR}" ]; then
  echo "Build directory not found: ${BUILD_DIR}"
  echo "Run: ./scripts/configure.sh coverage && ./scripts/build.sh coverage"
  exit 1
fi

find "${OBJ_DIR}" -name "*.gcda" -type f -delete 2>/dev/null || true

ctest --test-dir "${BUILD_DIR}" --output-on-failure -R scoreboard-core

if [ ! -d "${OUT_DIR}" ]; then
  mkdir -p "${OUT_DIR}"
fi

if [ ! -f "${OBJ_DIR}/scoreboard-core.c.gcno" ]; then
  echo "Coverage metadata not found at ${OBJ_DIR}/scoreboard-core.c.gcno"
  echo "Ensure ENABLE_COVERAGE is enabled (coverage preset)."
  exit 1
fi

GCOV_LOG="${OUT_DIR}/gcov.log"
gcov -o "${OBJ_DIR}" "${OBJ_DIR}/scoreboard-core.c.o" > "${GCOV_LOG}"

LINE_COVERAGE="$(
  awk '/Lines executed:/{gsub("Lines executed:", "", $0); print $1; exit}' "${GCOV_LOG}"
)"
if [ -z "${LINE_COVERAGE}" ]; then
  echo "Failed to determine line coverage from ${GCOV_LOG}"
  exit 1
fi

if [ "${LINE_COVERAGE}" != "100.00%" ]; then
  echo "Expected 100.00% line coverage, got ${LINE_COVERAGE}"
  exit 1
fi

echo "Coverage check passed: ${LINE_COVERAGE} line coverage for scoreboard-core.c"
