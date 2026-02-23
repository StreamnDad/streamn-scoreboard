#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Read version from CMakeLists.txt
VERSION="$(grep -m1 'project(.*VERSION' "${PROJECT_DIR}/CMakeLists.txt" \
  | sed 's/.*VERSION[[:space:]]*\([0-9][0-9.]*\).*/\1/')"

if [ -z "${VERSION}" ]; then
  echo "Error: Could not read version from CMakeLists.txt"
  exit 1
fi

PLUGIN_NAME="streamn-obs-scoreboard"
BUILD_DIR="${PROJECT_DIR}/build"
STAGING_DIR="${PROJECT_DIR}/build/pkg-staging"
BUNDLE_DIR="${STAGING_DIR}/obs-studio/plugins/${PLUGIN_NAME}.plugin"
MACOS_DIR="${BUNDLE_DIR}/Contents/MacOS"
PKG_OUTPUT="${PROJECT_DIR}/${PLUGIN_NAME}-${VERSION}-macos.pkg"

echo "Building ${PLUGIN_NAME} v${VERSION} release..."

# Configure and build release
cmake --preset default \
  -DOBS_INCLUDE_DIR="${OBS_INCLUDE_DIR:-}" \
  -DOBS_LIBRARY="${OBS_LIBRARY:-}" \
  ${OBS_SOURCE_DIR:+-DOBS_SOURCE_DIR="${OBS_SOURCE_DIR}"} \
  ${SIMDE_INCLUDE_DIR:+-DSIMDE_INCLUDE_DIR="${SIMDE_INCLUDE_DIR}"} \
  -S "${PROJECT_DIR}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" --config RelWithDebInfo

# Find built artifact
ARTIFACT=""
if [ -f "${BUILD_DIR}/${PLUGIN_NAME}.so" ]; then
  ARTIFACT="${BUILD_DIR}/${PLUGIN_NAME}.so"
elif [ -f "${BUILD_DIR}/${PLUGIN_NAME}.dylib" ]; then
  ARTIFACT="${BUILD_DIR}/${PLUGIN_NAME}.dylib"
fi

if [ -z "${ARTIFACT}" ]; then
  echo "Error: Built plugin artifact not found in ${BUILD_DIR}"
  echo "Expected ${PLUGIN_NAME}.so or ${PLUGIN_NAME}.dylib"
  exit 1
fi

# Assemble .plugin bundle in staging directory
rm -rf "${STAGING_DIR}"
mkdir -p "${MACOS_DIR}"

cp "${ARTIFACT}" "${MACOS_DIR}/${PLUGIN_NAME}"
chmod +x "${MACOS_DIR}/${PLUGIN_NAME}"
codesign --force --sign - "${MACOS_DIR}/${PLUGIN_NAME}"

cat > "${BUNDLE_DIR}/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>${PLUGIN_NAME}</string>
  <key>CFBundleIdentifier</key>
  <string>com.streamn.${PLUGIN_NAME}</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>${PLUGIN_NAME}</string>
  <key>CFBundlePackageType</key>
  <string>BNDL</string>
  <key>CFBundleShortVersionString</key>
  <string>${VERSION}</string>
  <key>CFBundleSupportedPlatforms</key>
  <array>
    <string>MacOSX</string>
  </array>
  <key>CFBundleVersion</key>
  <string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key>
  <string>11.0</string>
</dict>
</plist>
EOF

# Build .pkg installer
# Install to ~/Library/Application Support/obs-studio/plugins/
pkgbuild \
  --root "${STAGING_DIR}" \
  --identifier "com.streamn.${PLUGIN_NAME}" \
  --version "${VERSION}" \
  --install-location "${HOME}/Library/Application Support" \
  "${PKG_OUTPUT}"

echo ""
echo "Package created: ${PKG_OUTPUT}"
echo "  Version: ${VERSION}"
echo "  Install location: ~/Library/Application Support/obs-studio/plugins/${PLUGIN_NAME}.plugin"
