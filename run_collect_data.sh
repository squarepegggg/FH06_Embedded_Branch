#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

PORT="${1:-8889}"
URL="http://localhost:${PORT}/run_collect_data.html"

if command -v lsof >/dev/null 2>&1; then
  lsof -ti:"${PORT}" | xargs kill -9 2>/dev/null || true
  sleep 1
fi

if command -v open >/dev/null 2>&1; then
  open "${URL}" >/dev/null 2>&1 || echo "Auto-open failed. Open ${URL} manually."
fi

echo "Serving collect page at ${URL} (Ctrl+C to stop)"
exec python3 -m http.server "${PORT}"
