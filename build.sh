#!/bin/bash
# Build and flash this Zephyr/NCS app (BLE observer that scans advertising).
#
# Usage (from repo root):
#   ./build.sh              # build + flash
#   ./build.sh --no-flash   # build only
#
# Override Nordic SDK / toolchain if your install paths differ:
#   NCS_ROOT=/opt/nordic/ncs/v3.2.3 TOOLCHAIN=/path/to/zephyr-sdk ./build.sh
#
# If CMake keeps failing after a branch switch (stale build/), force a clean configure:
#   WEST_BUILD_PRISTINE=1 ./build.sh
#
# Flash runner: west defaults to nrfutil on some NCS versions; if it is not installed,
# this script picks nrfjprog, then jlink, then nrfutil. Override explicitly:
#   WEST_FLASH_RUNNER=jlink ./build.sh

set -euo pipefail

pick_west_flash_runner() {
  if [[ -n "${WEST_FLASH_RUNNER:-}" ]]; then
    printf '%s' "${WEST_FLASH_RUNNER}"
    return
  fi
  if command -v nrfjprog >/dev/null 2>&1; then
    printf '%s' nrfjprog
  elif command -v JLinkExe >/dev/null 2>&1; then
    printf '%s' jlink
  elif command -v nrfutil >/dev/null 2>&1; then
    printf '%s' nrfutil
  else
    printf '%s' ''
  fi
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

NCS_ROOT="${NCS_ROOT:-/opt/nordic/ncs/v3.1.1}"
TOOLCHAIN="${TOOLCHAIN:-/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk}"

DO_FLASH=1
if [[ "${1:-}" == "--no-flash" ]]; then
  DO_FLASH=0
fi

export ZEPHYR_SDK_INSTALL_DIR="${TOOLCHAIN}"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

if [[ ! -d "${NCS_ROOT}" ]]; then
  echo "Error: NCS_ROOT is not a directory: ${NCS_ROOT}"
  echo "Set NCS_ROOT to your nRF Connect SDK install (contains zephyr/ and nrf/)."
  exit 1
fi

cd "${NCS_ROOT}"

PRISTINE=()
if [[ "${WEST_BUILD_PRISTINE:-0}" == "1" ]]; then
  PRISTINE=(-p always)
fi

echo "Building BLE advertising scanner (board: nrf52dk/nrf52832)…"
west build "${PRISTINE[@]}" -b nrf52dk/nrf52832 \
  -d "${BUILD_DIR}" \
  --no-sysbuild \
  "${SCRIPT_DIR}"

echo ""
echo "Build OK: ${BUILD_DIR}/zephyr/zephyr.hex"

if [[ "${DO_FLASH}" -eq 1 ]]; then
  runner="$(pick_west_flash_runner)"
  if [[ -z "${runner}" ]]; then
    echo "Error: no west flash runner found."
    echo "Install one of: nrfutil (nRF Util), nrfjprog (nRF Command Line Tools), or SEGGER J-Link (JLinkExe on PATH),"
    echo "or set WEST_FLASH_RUNNER (e.g. WEST_FLASH_RUNNER=jlink or nrfjprog)."
    echo "You can still flash the hex manually: ${BUILD_DIR}/zephyr/zephyr.hex (see ./jlink.sh)."
    exit 1
  fi
  echo "Flashing with west runner: ${runner}"
  west flash -d "${BUILD_DIR}" --runner "${runner}"
  echo "Done. Open RTT/log to see scan output (e.g. ./jlink.sh if you use J-Link + RTT)."
else
  echo "Skipped flash (--no-flash). Flash manually: west flash -d \"${BUILD_DIR}\""
fi
