#!/bin/bash
# Build from NCS workspace (required for west build to work)
# Run from project root: ./build.sh
# For solar-optimized build (reduced logging): ./build.sh solar

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NCS_ROOT="/opt/nordic/ncs/v3.1.1"
TOOLCHAIN="/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk"

export ZEPHYR_SDK_INSTALL_DIR="${TOOLCHAIN}"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

# Optional: first argument "solar" → use build_solar and prj_solar.conf
SOLAR_BUILD=false
if [[ "${1:-}" == "solar" ]]; then
  SOLAR_BUILD=true
  BUILD_DIR="${SCRIPT_DIR}/build_solar"
else
  BUILD_DIR="${SCRIPT_DIR}/build"
fi

cd "${NCS_ROOT}"
if [[ "$SOLAR_BUILD" == true ]]; then
  west build -b nrf52dk/nrf52832 \
    -d "${BUILD_DIR}" \
    --no-sysbuild \
    "${SCRIPT_DIR}" \
    -- -DEXTRA_CONF_FILES="${SCRIPT_DIR}/prj_solar.conf"
else
  west build -b nrf52dk/nrf52832 \
    -d "${BUILD_DIR}" \
    --no-sysbuild \
    "${SCRIPT_DIR}"
fi

echo ""
echo "Build complete. Hex: ${BUILD_DIR}/zephyr/zephyr.hex"
echo "Flash with: ./jlink.sh"
if [[ "$SOLAR_BUILD" == true ]]; then
  echo "(Solar build: reduced log level)"
fi
