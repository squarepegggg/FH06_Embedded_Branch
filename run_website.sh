#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

URL="http://localhost:8080/ble_dashboard.html"

# Best-effort browser open. Even if this fails, still run the server.
if command -v open >/dev/null 2>&1; then
  open "${URL}" >/dev/null 2>&1 || echo "Auto-open failed. Open ${URL} manually."
fi

exec python3 -m http.server 8080
