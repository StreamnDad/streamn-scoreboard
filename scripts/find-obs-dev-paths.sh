#!/usr/bin/env bash
set -euo pipefail

ROOTS=(
  "${HOME}"
  "/opt/homebrew"
  "/usr/local"
  "/Applications"
)

LIB_CANDIDATES=(
  "/Applications/OBS.app/Contents/Frameworks/libobs.framework/Versions/A/libobs"
  "/Applications/OBS.app/Contents/Frameworks/libobs.framework/libobs"
  "/opt/homebrew/lib/libobs.dylib"
  "/usr/local/lib/libobs.dylib"
)

printf "Searching for obs-module.h (header root) ...\n"
HEADER_HITS=()
while IFS= read -r line; do
  [ -n "$line" ] && HEADER_HITS+=("$line")
done < <(
  {
    for root in "${ROOTS[@]}"; do
      if [ -d "$root" ]; then
        find "$root" -type f -name obs-module.h 2>/dev/null
      fi
    done
  } | sort -u
)

printf "Searching for libobs library ...\n"
LIB_HITS=()
while IFS= read -r line; do
  [ -n "$line" ] && LIB_HITS+=("$line")
done < <(
  {
    for lib in "${LIB_CANDIDATES[@]}"; do
      [ -f "$lib" ] && printf "%s\n" "$lib"
    done
    for root in "${ROOTS[@]}"; do
      if [ -d "$root" ]; then
        find "$root" -type f \( -name 'libobs.dylib' -o -name 'libobs' \) 2>/dev/null
      fi
    done
  } | sort -u
)

if [ "${#HEADER_HITS[@]}" -eq 0 ]; then
  echo "No obs-module.h found."
else
  echo "Header candidates:"
  for h in "${HEADER_HITS[@]}"; do
    printf "  %s\n" "$(dirname "$h")"
  done
fi

if [ "${#LIB_HITS[@]}" -eq 0 ]; then
  echo "No libobs library found."
else
  echo "Library candidates:"
  for l in "${LIB_HITS[@]}"; do
    printf "  %s\n" "$l"
  done
fi

if [ "${#HEADER_HITS[@]}" -gt 0 ] && [ "${#LIB_HITS[@]}" -gt 0 ]; then
  INCLUDE_DIR="$(dirname "${HEADER_HITS[0]}")"
  LIB_PATH="${LIB_HITS[0]}"
  echo
  echo "Use this configure command:"
  printf "./scripts/configure.sh default -DOBS_INCLUDE_DIR=%q -DOBS_LIBRARY=%q\n" "$INCLUDE_DIR" "$LIB_PATH"
else
  echo
  echo "To proceed, install/build OBS development headers (obs-module.h), then rerun this script."
fi
