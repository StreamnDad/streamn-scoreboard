#!/usr/bin/env bash
set -euo pipefail

: "${RUNNER_TEMP:?Set RUNNER_TEMP to a directory for the OBS SDK}"

git clone --depth 1 --branch 30.2.3 \
  https://github.com/obsproject/obs-studio.git "$RUNNER_TEMP/obs-studio"
obs_dmg="$RUNNER_TEMP/obs.dmg"
curl --fail --silent --show-error --location --retry 3 \
  https://github.com/obsproject/obs-studio/releases/download/30.2.3/OBS-Studio-30.2.3-macOS-Apple.dmg \
  --output "$obs_dmg"
printf '%s  %s\n' 6714f8fcbc8dc1f6b92ac1da58e2c7570dc65fefb819d68536375711973be0fb "$obs_dmg" | shasum -a 256 -c -
mountpoint="$RUNNER_TEMP/obs-mount"
hdiutil attach "$obs_dmg" -readonly -nobrowse -mountpoint "$mountpoint"
trap 'hdiutil detach "$mountpoint"' EXIT
ditto "$mountpoint/OBS.app" "$RUNNER_TEMP/OBS.app"
hdiutil detach "$mountpoint"
trap - EXIT
obs_frameworks="$RUNNER_TEMP/OBS.app/Contents/Frameworks"

# Use the Qt build pinned by the OBS 30.2.3 buildspec.
qt_archive="$RUNNER_TEMP/obs-qt.tar.xz"
curl --fail --silent --show-error --location --retry 3 \
  https://github.com/obsproject/obs-deps/releases/download/2024-05-08/macos-deps-qt6-2024-05-08-universal.tar.xz \
  --output "$qt_archive"
printf '%s  %s\n' 248fb342e7ddf574af0960aaedeffb832deda1485dc81583302646e979593a6e "$qt_archive" | shasum -a 256 -c -
mkdir -p "$RUNNER_TEMP/obs-qt"
tar -xf "$qt_archive" -C "$RUNNER_TEMP/obs-qt"
qt_config="$(find "$RUNNER_TEMP/obs-qt" -name Qt6Config.cmake -print -quit)"
test -n "$qt_config"

cmake --preset default \
  -DOBS_INCLUDE_DIR="$RUNNER_TEMP/obs-studio/libobs" \
  -DOBS_LIBRARY="$obs_frameworks/libobs.framework/libobs" \
  -DOBS_SOURCE_DIR="$RUNNER_TEMP/obs-studio" \
  -DOBS_FRAMEWORKS_DIR="$obs_frameworks" \
  -DQt6_DIR="$(dirname "$qt_config")" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
  -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF \
  '-DCMAKE_INSTALL_RPATH=@executable_path/../Frameworks'
