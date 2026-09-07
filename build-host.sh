#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
meson setup build-host
ninja -C build-host
