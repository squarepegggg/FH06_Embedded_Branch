#!/bin/bash
# Build from NCS workspace (required for west build to work)
# Run from project root: ./build.sh

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NCS_ROOT="/opt/nordic/ncs/v3.1.1"
TOOLCHAIN="/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk"

export ZEPHYR_SDK_INSTALL_DIR="${TOOLCHAIN}"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

cd "${NCS_ROOT}"
west build -b nrf52dk/nrf52832 \
  -d "${SCRIPT_DIR}/build" \
  --no-sysbuild \
  "${SCRIPT_DIR}"

echo ""
echo "Build complete. Hex: ${SCRIPT_DIR}/build/zephyr/zephyr.hex"
echo "Flash with: ./jlink.sh"
