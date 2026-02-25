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
PKG_SCRIPTS="/tmp/pkg-scripts"
PKG_OUTPUT="${PROJECT_DIR}/${PLUGIN_NAME}-${VERSION}-macos.pkg"

echo "Packaging ${PLUGIN_NAME} v${VERSION}..."

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
  echo "Run 'make build' first."
  exit 1
fi

# Package the binary and Info.plist alongside the postinstall script.
# Using --nopayload so pkgbuild doesn't try to interpret a .plugin bundle.
# The postinstall script finds these files via dirname "$0".
rm -rf "${PKG_SCRIPTS}"
mkdir -p "${PKG_SCRIPTS}"

cp "${ARTIFACT}" "${PKG_SCRIPTS}/${PLUGIN_NAME}"
chmod +x "${PKG_SCRIPTS}/${PLUGIN_NAME}"
codesign --force --sign - "${PKG_SCRIPTS}/${PLUGIN_NAME}"

cat > "${PKG_SCRIPTS}/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>${PLUGIN_NAME}</string>
  <key>CFBundleIdentifier</key>
  <string>com.streamndad.${PLUGIN_NAME}</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>${PLUGIN_NAME}</string>
  <key>CFBundlePackageType</key>
  <string>BNDL</string>
  <key>CFBundleShortVersionString</key>
  <string>${VERSION}</string>
  <key>CFBundleVersion</key>
  <string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key>
  <string>11.0</string>
</dict>
</plist>
EOF

cat > "${PKG_SCRIPTS}/postinstall" <<'SCRIPT'
#!/bin/bash
SCRIPT_DIR="$(dirname "$0")"

# Resolve the real user's home directory. The macOS installer sets $HOME to
# the invoking user's home even when running as root, but does NOT set
# $SUDO_USER. Fall back through $HOME, $SUDO_USER, then $USER.
if [ -n "${HOME}" ] && [ "${HOME}" != "/var/root" ]; then
  REAL_HOME="${HOME}"
elif [ -n "${SUDO_USER}" ]; then
  REAL_HOME=$(eval echo "~${SUDO_USER}")
else
  REAL_HOME=$(eval echo "~${USER}")
fi

DEST="${REAL_HOME}/Library/Application Support/obs-studio/plugins"
BUNDLE="${DEST}/streamn-obs-scoreboard.plugin"

mkdir -p "$DEST"
rm -rf "${DEST}/streamn-obs-scoreboard"
rm -rf "${BUNDLE}"

mkdir -p "${BUNDLE}/Contents/MacOS"
cp "${SCRIPT_DIR}/streamn-obs-scoreboard" "${BUNDLE}/Contents/MacOS/"
cp "${SCRIPT_DIR}/Info.plist" "${BUNDLE}/Contents/"

# Ensure the installing user owns the files, not root
OWNER=$(stat -f '%Su' "${REAL_HOME}")
chown -R "${OWNER}" "${BUNDLE}"
SCRIPT
chmod +x "${PKG_SCRIPTS}/postinstall"

pkgbuild \
  --nopayload \
  --scripts "${PKG_SCRIPTS}" \
  --identifier "com.streamndad.${PLUGIN_NAME}" \
  --version "${VERSION}" \
  "${PKG_OUTPUT}"

echo ""
echo "Package created: ${PKG_OUTPUT}"
echo "  Version: ${VERSION}"
echo "  Install: right-click the .pkg > Open"
echo "  Location: ~/Library/Application Support/obs-studio/plugins/${PLUGIN_NAME}.plugin/"
