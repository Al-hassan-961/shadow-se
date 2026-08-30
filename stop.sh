#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Shadow SE - stop every process started by ./start.sh.
# Sends SIGTERM, waits for graceful shutdown, then force-kills stragglers so a
# follow-up ./start.sh can rebind the ports immediately.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIDFILE="$ROOT/.shadow.pids"

pids=()
if [ -f "$PIDFILE" ]; then
    while read -r pid; do
        [ -n "$pid" ] && pids+=("$pid")
    done < "$PIDFILE"
fi

if [ ${#pids[@]} -eq 0 ]; then
    echo "[.] nothing to stop (no pid file)"
    exit 0
fi

for pid in "${pids[@]}"; do
    kill "$pid" 2>/dev/null && echo "[*] sent stop to $pid" || true
done

# Wait up to 6s for graceful shutdown.
deadline=$((SECONDS + 6))
while [ $SECONDS -lt $deadline ]; do
    alive=0
    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then alive=1; fi
    done
    [ $alive -eq 0 ] && break
    sleep 0.2
done

# Force-kill anything that refused to exit.
for pid in "${pids[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
        echo "[!] force killing $pid"
        kill -9 "$pid" 2>/dev/null || true
    fi
done

rm -f "$PIDFILE"
echo "[+] Shadow SE stopped."
