#!/usr/bin/env bash
set -euo pipefail

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required. Install from https://brew.sh"
  exit 1
fi

echo "Installing base build tools..."
brew install cmake ninja pkg-config simde qt@5

echo "Checking for OBS Studio..."
if [ ! -d "/Applications/OBS.app" ]; then
  echo "OBS.app not found in /Applications. Install OBS Studio if needed."
else
  echo "Found OBS.app"
fi

echo "Checking for libobs development package..."
if pkg-config --exists libobs; then
  echo "Found libobs via pkg-config"
else
  echo "libobs not found via pkg-config."
  echo "You can still configure with:"
  echo "  ./scripts/configure.sh default -DOBS_INCLUDE_DIR=/path/to/includes -DOBS_LIBRARY=/path/to/libobs.dylib"
fi

echo "Setup complete."
