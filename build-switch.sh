#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ -f build-switch/build.ninja ]]; then
  meson configure build-switch -Druntime=true
else
  meson setup build-switch --cross-file switch-cross.txt -Druntime=true
fi
ninja -C build-switch
/opt/devkitpro/tools/bin/nacptool --create 'KAWAXP' 'KAWAXP port work' '0.3.0' build-switch/kawaxp.nacp
/opt/devkitpro/tools/bin/elf2nro build-switch/kawaxp kawaxp.nro --nacp=build-switch/kawaxp.nacp
/opt/devkitpro/tools/bin/nacptool --create 'KAWAXP Resource Diagnostic' 'KAWAXP port work' '0.1.0' build-switch/probe.nacp
/opt/devkitpro/tools/bin/elf2nro build-switch/kawaxp-probe kawaxp-diagnostic.nro --nacp=build-switch/probe.nacp
