#!/usr/bin/env bash
# Build and run the T2 driver-in-the-loop test.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
"$HERE/fetch.sh"

MT="$HERE/vendor/meshtastic-firmware/src"
OUT="$HERE/vendor/build"
mkdir -p "$OUT"

"${CXX:-g++}" -std=gnu++17 -O1 -g -Wall -Wno-unused-parameter \
    -I "$HERE/shim/inc" -I "$MT/modules" -I "$MT" -I "$MT/mesh/generated" -I "$HERE/vendor/nanopb" -I "$ROOT/lib/senxx/src" \
    "$HERE/harness.cpp" \
    "$MT/modules/Telemetry/Sensor/SENXXSensor.cpp" "$MT/detect/ScanI2C.cpp" \
    "$ROOT/lib/senxx/src/emulator.cpp" "$ROOT/lib/senxx/src/encode.cpp" "$ROOT/lib/senxx/src/crc.cpp" \
    -o "$OUT/driver_harness"

"$OUT/driver_harness" "$@"
