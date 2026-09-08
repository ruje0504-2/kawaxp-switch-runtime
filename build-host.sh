#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ -f build-host/build.ninja ]]; then
  meson configure build-host -Druntime=true
else
  meson setup build-host -Druntime=true
fi
ninja -C build-host
meson test -C build-host --print-errorlogs
