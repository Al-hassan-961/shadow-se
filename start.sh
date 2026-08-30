#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Shadow SE - start the whole website with ONE command.
#
#   ./start.sh            # build (if needed) + start web UI + admin + JSON API,
#                         # open your browser, and show an interactive menu
#   ./start.sh onion      # ... and also bring up the stealth onion service
#   ./start.sh run        # start and drop straight into the terminal UI
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
ARG="${1:-}"

mkdir -p "$LOG_DIR"
: > "$PIDFILE"

# ---- build if needed --------------------------------------------------------
if [ ! -x "$BUILD/shadow-se-web" ] || [ ! -x "$BUILD/shadow-se-admin" ] || \
   [ ! -x "$BUILD/shadow-se-gateway" ] || [ ! -x "$BUILD/shadow-se" ]; then
    echo "[*] Building project..."
    cmake -S "$ROOT" -B "$BUILD" -DSHADOWSE_BUILD_TESTS=ON >/dev/null
    cmake --build "$BUILD" -j"$(nproc)" >/dev/null
fi
echo "[+] Build ready."

# ---- start services (idempotent) ---------------------------------------------
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

if [ "$ARG" = "onion" ]; then
    echo "[*] Bringing up the stealth onion service..."
    bash "$ROOT/onion/setup.sh"
fi
sleep 1

# ---- URLs ---------------------------------------------------------------------
WEB_URL="http://127.0.0.1:$WEB_PORT/"
ADMIN_URL="$(grep -m1 'ADMIN DASHBOARD' "$LOG_DIR/admin.log" 2>/dev/null | awk '{print $4}' || true)"
GATEWAY_URL="http://127.0.0.1:$GATEWAY_PORT/status"

open_url() {
    local url="$1"
    if command -v termux-open-url >/dev/null 2>&1; then termux-open-url "$url" >/dev/null 2>&1 || true; return; fi
    if command -v xdg-open >/dev/null 2>&1; then xdg-open "$url" >/dev/null 2>&1 || true; return; fi
    if command -v python3 >/dev/null 2>&1; then python3 -m webbrowser "$url" >/dev/null 2>&1 || true; return; fi
    echo "  (no browser opener found - open the URL yourself)"
}

banner() {
    echo "=============================================================="
    echo "  Shadow SE site is running:"
    echo "    Web UI : $WEB_URL"
    [ -n "$ADMIN_URL" ] && echo "    Admin  : $ADMIN_URL"
    echo "    JSON   : $GATEWAY_URL"
    echo "  Stop with: ./stop.sh"
    echo "=============================================================="
}

# ---- non-interactive mode (piped/scripted): report URLs and exit --------------
if [ ! -t 0 ]; then
    banner
    echo "[+] Site is running in the background. Use ./stop.sh to stop it."
    exit 0
fi

# ---- interactive mode -----------------------------------------------------------
banner

# `./start.sh run` drops straight into the terminal UI.
if [ "$ARG" = "run" ]; then
    echo "[*] Opening the website and starting the terminal UI..."
    open_url "$WEB_URL"
    "$BUILD/shadow-se" --"$MODE"
    echo
    echo "[*] Terminal UI closed. Stopping the site..."
    bash "$ROOT/stop.sh"
    exit 0
fi

echo "[*] Opening the website in your browser..."
open_url "$WEB_URL"

while true; do
    echo
    echo "  What would you like to do?"
    echo "    1) Open the website (web UI)"
    echo "    2) Open the admin dashboard"
    echo "    3) Open the JSON API"
    echo "    4) Run the terminal UI (search / crawl)"
    echo "    5) Show the URLs again"
    echo "    0) Stop the site and exit"
    printf "  choice> "
    read -r choice || break
    case "$choice" in
        1) open_url "$WEB_URL" ;;
        2) if [ -n "$ADMIN_URL" ]; then open_url "$ADMIN_URL"; else echo "  (admin URL not found in log)"; fi ;;
        3) open_url "$GATEWAY_URL" ;;
        4) "$BUILD/shadow-se" --"$MODE" ;;
        5) banner ;;
        0) break ;;
        *) echo "  (unknown choice)" ;;
    esac
done

echo
echo "[*] Stopping the site..."
bash "$ROOT/stop.sh"
