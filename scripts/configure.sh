#!/usr/bin/env bash
set -euo pipefail

PRESET="${1:-default}"
shift || true

cmake --preset "${PRESET}" "$@"
