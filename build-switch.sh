#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
meson setup build-switch --cross-file switch-cross.txt
ninja -C build-switch
/opt/devkitpro/tools/bin/nacptool --create 'KAWAXP Resource Diagnostic' 'KAWAXP port work' '0.1.0' build-switch/probe.nacp
/opt/devkitpro/tools/bin/elf2nro build-switch/kawaxp-probe kawaxp-diagnostic.nro --nacp=build-switch/probe.nacp
