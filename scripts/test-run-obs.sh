#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
scratch="$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/scoreboard-launch-test.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT

obs_bin="$scratch/Fake OBS"
cat > "$obs_bin" <<'OBS'
#!/bin/bash
printf 'plugins=%s\n' "${OBS_PLUGINS_PATH-<unset>}"
printf 'dyld=%s\n' "${DYLD_LIBRARY_PATH-<unset>}"
printf 'arg=%s\n' "$@"
OBS
chmod +x "$obs_bin"

unset OBS_PLUGINS_PATH DYLD_LIBRARY_PATH
actual=$(OBS_APP_PATH="$obs_bin" bash "$script_dir/run-obs.sh")
expected=$'plugins=<unset>\ndyld=<unset>\narg=--verbose'
[[ "$actual" == "$expected" ]] || { printf 'Unexpected default launch:\n%s\n' "$actual" >&2; exit 1; }

stage="$scratch/staged plugins"
mkdir -p "$stage/streamn-obs-scoreboard.plugin/Contents/MacOS"
actual=$(OBS_APP_PATH="$obs_bin" OBS_PLUGINS_PATH="$stage" bash "$script_dir/run-obs.sh" --multi)
expected=$(printf 'plugins=%s\ndyld=<unset>\narg=--verbose\narg=--multi' "$stage")
[[ "$actual" == "$expected" ]] || { printf 'Unexpected explicit launch:\n%s\n' "$actual" >&2; exit 1; }

if actual=$(OBS_APP_PATH="$scratch/missing OBS" bash "$script_dir/run-obs.sh" 2>&1); then
  echo 'Launcher unexpectedly succeeded without an OBS executable.' >&2
  exit 1
fi
[[ "$actual" == *'OBS executable not found'* ]]
echo 'OBS launcher regression test passed.'
