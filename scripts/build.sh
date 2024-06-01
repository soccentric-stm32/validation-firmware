#!/usr/bin/env bash
#
# build.sh - Configure and build HWVAL inside the stm32-dev container (CON-001)
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Usage:   scripts/build.sh            configure (if needed) + build
#          scripts/build.sh clean      rm -rf build, then configure + build
#
# Outputs: build/hwval.elf, build/hwval.bin, build/hwval.hex, build/hwval.map
#

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
SCRIPT_DIR="$ROOT/scripts"
DEV="$SCRIPT_DIR/docker-dev.sh"

# `python3` is the Microsoft Store alias stub on Windows, where it exits 9009;
# the gates below run as plain `python` there.
PYTHON=python3
if [[ ${OS:-} == Windows_NT ]]; then
    PYTHON=python
fi

if [[ "${1:-}" == "clean" ]]; then
    rm -rf build
fi

"$DEV" run cmake -S /work -B /work/build -G Ninja \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
"$DEV" run cmake --build /work/build
"$DEV" run arm-none-eabi-size /work/build/hwval.elf

# Build-time gates: NFR-003/004 footprint (from the .map) and COV-001
# (SRS 8 <-> test cases). COV-002 needs the SDK header via the container:
"$PYTHON" "$ROOT/scripts/check_footprint.py" build
"$PYTHON" "$ROOT/scripts/check_coverage.py" --no-cov2
"$PYTHON" "$ROOT/scripts/check_coverage.py" --no-cov1