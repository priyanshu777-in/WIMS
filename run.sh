#!/usr/bin/env bash
# Build and launch the WIMS web dashboard.
# Usage: ./run.sh [port]   (default port 8080)
set -e
cd "$(dirname "$0")"
make -s

PORT="${1:-8080}"

# If something is already listening on the requested port...
if lsof -nP -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
  # ...and it's an already-running WIMS, just point the user at it.
  if curl -s -m 2 "http://localhost:$PORT/api/state" | grep -q '"ok"'; then
    echo ""
    echo "  WIMS is already running  ->  http://localhost:$PORT"
    echo "  To restart it:  pkill -f wims_server   then run ./run.sh again"
    echo ""
    exit 0
  fi
  # ...otherwise some other program is using it: pick the next free port.
  START="$PORT"
  while lsof -nP -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; do
    PORT=$((PORT + 1))
  done
  echo "Port $START was busy; using $PORT instead."
fi

echo ""
echo "  WIMS is starting  ->  http://localhost:$PORT"
echo "  Login: admin / admin123"
echo ""
exec ./wims_server "$PORT"
