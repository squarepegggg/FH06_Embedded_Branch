#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Locate firmware hex ─────────────────────────────────────
HEX_FILE="${SCRIPT_DIR}/build/FH06_Embedded_Branch/zephyr/zephyr.hex"
if [[ ! -f "${HEX_FILE}" ]]; then
  HEX_FILE="${SCRIPT_DIR}/build/merged.hex"
fi

if [[ ! -f "${HEX_FILE}" ]]; then
  echo "Error: could not find firmware hex file."
  echo "Expected one of:"
  echo "  ${SCRIPT_DIR}/build/FH06_Embedded_Branch/zephyr/zephyr.hex"
  echo "  ${SCRIPT_DIR}/build/merged.hex"
  exit 1
fi

# ── Check required tools ────────────────────────────────────
for tool in JLinkExe JLinkRTTClient; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "Error: '${tool}' is not installed or not on PATH."
    exit 1
  fi
done

# ── Kill any lingering JLink processes ───────────────────────
pkill -9 -f "JLinkGDBServer" 2>/dev/null || true
pkill -9 -f "JLinkGUIServerExe" 2>/dev/null || true
pkill -9 -f "JLinkExe" 2>/dev/null || true
pkill -9 -f "JLinkRTTClient" 2>/dev/null || true
sleep 1

# ── Temp files ───────────────────────────────────────────────
FLASH_CMD="$(mktemp /tmp/jlink_flash_cmd.XXXXXX)"
RTT_CMD="$(mktemp /tmp/jlink_rtt_cmd.XXXXXX)"
FLASH_LOG="$(mktemp /tmp/jlink_flash_log.XXXXXX)"
RTT_LOG="$(mktemp /tmp/jlink_rtt_log.XXXXXX)"

cleanup() {
  # Kill the background JLinkExe that hosts RTT
  if [[ -n "${RTT_HOST_PID:-}" ]] && kill -0 "${RTT_HOST_PID}" 2>/dev/null; then
    kill -9 "${RTT_HOST_PID}" >/dev/null 2>&1 || true
    wait "${RTT_HOST_PID}" 2>/dev/null || true
  fi
  # Also kill any GUI server that JLinkExe may have spawned
  pkill -9 -f "JLinkGUIServerExe" 2>/dev/null || true
  rm -f "${FLASH_CMD}" "${RTT_CMD}" "${FLASH_LOG}" "${RTT_LOG}"
}
trap cleanup EXIT

# ── Step 1: Flash the firmware ───────────────────────────────
cat > "${FLASH_CMD}" <<EOF
r
h
loadfile ${HEX_FILE}
r
g
q
EOF

echo "═══════════════════════════════════════════════════════"
echo "  Flashing ${HEX_FILE##*/}..."
echo "═══════════════════════════════════════════════════════"
JLinkExe \
  -device nRF52832_xxAA \
  -if SWD \
  -speed 4000 \
  -autoconnect 1 \
  -CommandFile "${FLASH_CMD}" >"${FLASH_LOG}" 2>&1

if grep -Eq "Connecting to J-Link failed|Could not connect to J-Link|Cannot connect to J-Link|Error while programming flash" "${FLASH_LOG}"; then
  cat "${FLASH_LOG}"
  echo "Error: flashing failed."
  exit 1
fi
echo "✅ Flash complete."
echo ""

# ── Step 2: Start JLinkExe as a persistent RTT host ─────────
# JLinkExe with a long sleep keeps the debug session alive so RTT works.
# We connect, reset/go, then just idle with the connection open.
cat > "${RTT_CMD}" <<EOF
r
g
sleep 999999
q
EOF

echo "═══════════════════════════════════════════════════════"
echo "  Starting RTT host..."
echo "═══════════════════════════════════════════════════════"

JLinkExe \
  -device nRF52832_xxAA \
  -if SWD \
  -speed 4000 \
  -autoconnect 1 \
  -CommandFile "${RTT_CMD}" >"${RTT_LOG}" 2>&1 &
RTT_HOST_PID=$!

# Wait for JLinkExe to connect and start the target
sleep 2

if ! kill -0 "${RTT_HOST_PID}" 2>/dev/null; then
  cat "${RTT_LOG}"
  echo "Error: JLinkExe RTT host failed to start."
  exit 1
fi
echo "✅ RTT host running (PID ${RTT_HOST_PID})."
echo ""

# ── Step 3: Stream RTT output ───────────────────────────────
echo "═══════════════════════════════════════════════════════"
echo "  Streaming RTT predictions from PCB board..."
echo "  Press Ctrl+C to stop."
echo "═══════════════════════════════════════════════════════"
echo ""

JLinkRTTClient
