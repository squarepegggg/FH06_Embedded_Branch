#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

# Free port 8888 if something is already using it (e.g. previous run)
if command -v lsof >/dev/null 2>&1; then
  lsof -ti:8888 | xargs kill -9 2>/dev/null || true
  sleep 1
fi

URL="http://localhost:8888/ble_dashboard.html"

# Best-effort browser open. Even if this fails, still run the server.
if command -v open >/dev/null 2>&1; then
  open "${URL}" >/dev/null 2>&1 || echo "Auto-open failed. Open ${URL} manually."
fi

exec python3 -m http.server 8888
