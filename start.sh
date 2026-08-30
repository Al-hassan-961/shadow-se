#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Shadow SE - start the whole website with one command.
#
#   ./start.sh            # build if needed, then run web UI + admin + JSON API
#   ./start.sh onion      # ... and also bring up the stealth onion service
#
# Env overrides: WEB_PORT, ADMIN_PORT, GATEWAY_PORT, MODE (stub|curl).
# Stop everything with ./stop.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"
PIDFILE="$ROOT/.shadow.pids"
LOG_DIR="$ROOT/.shadow-logs"
: "${WEB_PORT:=8080}"
: "${ADMIN_PORT:=8081}"
: "${GATEWAY_PORT:=8090}"
: "${MODE:=stub}"
ONION="${1:-}"

mkdir -p "$LOG_DIR"
: > "$PIDFILE"

# ---- build if needed --------------------------------------------------------
if [ ! -x "$BUILD/shadow-se-web" ] || [ ! -x "$BUILD/shadow-se-admin" ] || \
   [ ! -x "$BUILD/shadow-se-gateway" ]; then
    echo "[*] Building project..."
    cmake -S "$ROOT" -B "$BUILD" -DSHADOWSE_BUILD_TESTS=ON >/dev/null
    cmake --build "$BUILD" -j"$(nproc)" >/dev/null
fi
echo "[+] Build ready."

# Start a binary if it is not already answering on its health URL.
start_if_down() {
    local name="$1" health="$2"; shift 2
    if curl -s -o /dev/null --max-time 1 "$health" 2>/dev/null; then
        echo "[+] $name already running ($health)"
        return
    fi
    echo "[*] Starting $name ..."
    nohup "$@" > "$LOG_DIR/$name.log" 2>&1 &
    echo $! >> "$PIDFILE"
}

start_if_down "web"     "http://127.0.0.1:$WEB_PORT/" \
    "$BUILD/shadow-se-web" --port "$WEB_PORT" --"$MODE"
start_if_down "admin"   "http://127.0.0.1:$ADMIN_PORT/healthz" \
    "$BUILD/shadow-se-admin" --no-web --port "$ADMIN_PORT" --web-port "$WEB_PORT" --"$MODE"
start_if_down "gateway" "http://127.0.0.1:$GATEWAY_PORT/status" \
    "$BUILD/shadow-se-gateway" --port "$GATEWAY_PORT" --"$MODE"

if [ "$ONION" = "onion" ]; then
    echo "[*] Bringing up the stealth onion service..."
    bash "$ROOT/onion/setup.sh"
fi

# ---- report ----------------------------------------------------------------
sleep 1
ADMIN_TOKEN_URL=""
if [ -f "$LOG_DIR/admin.log" ]; then
    ADMIN_TOKEN_URL="$(grep -m1 'ADMIN DASHBOARD' "$LOG_DIR/admin.log" | awk '{print $4}' || true)"
fi
echo ""
echo "=============================================================="
echo "  Shadow SE website is running:"
echo "    Web UI      : http://127.0.0.1:$WEB_PORT/"
[ -n "$ADMIN_TOKEN_URL" ] && echo "    Admin       : $ADMIN_TOKEN_URL"
echo "    JSON API    : http://127.0.0.1:$GATEWAY_PORT/status"
echo "  Stop everything with:  ./stop.sh"
echo "=============================================================="
