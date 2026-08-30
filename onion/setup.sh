#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Shadow SE - set up a stealth (v3 client-auth) onion service for the web UI.
#
# What this does:
#   1. Ensures the project is built (shadow-se-web + se-keygen).
#   2. Starts the privacy-hardened web UI on 127.0.0.1:8080 (loopback only).
#   3. Generates a stealth client keypair (x25519) for v3 client authorization.
#   4. Writes a hardened torrc and starts Tor with a fresh DataDirectory.
#   5. Prints the live .onion address and the client install instructions.
#
# After setup, ONLY clients that install the generated PRIVATE key (see
# onion/CLIENT.md) can reach the site. Everyone else gets a "Not Found".
#
# Usage: bash onion/setup.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ONION_DIR="$ROOT/onion"
TOR_DIR="$ONION_DIR/tor_data"
HIDDEN_DIR="$TOR_DIR/hidden"
AUTH_DIR="$HIDDEN_DIR/authorized_clients"
WEB_PORT="${WEB_PORT:-8080}"
CLIENT_NAME="${CLIENT_NAME:-shadowse}"

echo "[*] Shadow SE stealth onion setup"
echo "    project root : $ROOT"

# ---- 1. build -------------------------------------------------------------
if [ ! -x "$ROOT/build/shadow-se-web" ] || [ ! -x "$ROOT/build/se-keygen" ]; then
    echo "[*] Building project..."
    cmake -S "$ROOT" -B "$ROOT/build" -DSHADOWSE_BUILD_TESTS=ON >/dev/null
    cmake --build "$ROOT/build" -j"$(nproc)" >/dev/null
fi
echo "[+] Build present."

# ---- 2. start web UI (loopback only) ---------------------------------------
if curl -s -o /dev/null --max-time 2 "http://127.0.0.1:$WEB_PORT/" ; then
    echo "[+] Web UI already running on 127.0.0.1:$WEB_PORT"
else
    echo "[*] Starting web UI on 127.0.0.1:$WEB_PORT (stub fetcher, demo data)..."
    nohup "$ROOT/build/shadow-se-web" --port "$WEB_PORT" --stub \
        > "$ONION_DIR/web.log" 2>&1 &
    sleep 1
fi

# ---- 3. stealth client keypair ---------------------------------------------
mkdir -p "$AUTH_DIR"
chmod 700 "$AUTH_DIR"
KEY_FILE="$ONION_DIR/client.keys"
if [ ! -f "$KEY_FILE" ]; then
    echo "[*] Generating stealth client keypair..."
    "$ROOT/build/se-keygen" --client "$CLIENT_NAME" > "$KEY_FILE"
fi
PUB=$(grep -m1 'public key  : ' "$KEY_FILE" | awk '{print $4}')
PRIV=$(grep -m1 'private key : ' "$KEY_FILE" | awk '{print $4}')
if [ -z "$PUB" ] || [ -z "$PRIV" ]; then
    echo "[!] Could not parse client keys from $KEY_FILE" >&2
    exit 1
fi
if [ ! -f "$AUTH_DIR/$CLIENT_NAME.auth" ]; then
    echo "descriptor:x25519:$PUB" > "$AUTH_DIR/$CLIENT_NAME.auth"
    chmod 600 "$AUTH_DIR/$CLIENT_NAME.auth"
fi
echo "[+] Stealth client '$CLIENT_NAME' authorized."

# ---- 4. torrc + start Tor ----------------------------------------------------
mkdir -p "$TOR_DIR"
chmod 700 "$TOR_DIR"
cat > "$ONION_DIR/torrc" <<EOF
# Shadow SE stealth onion service - hardened config (auto-generated)
SOCKSPort 9050
Log notice file $TOR_DIR/tor.log
DataDirectory $TOR_DIR
ExitRelay 0
BridgeRelay 0
PublishServerDescriptor 0
SocksPolicy accept 127.0.0.1
SocksPolicy reject *
HiddenServiceDir $HIDDEN_DIR
HiddenServiceVersion 3
HiddenServicePort 80 127.0.0.1:$WEB_PORT
HiddenServiceEnableIntroDoSDefense 1
EOF

if ! pgrep -f "tor -f $ONION_DIR/torrc" >/dev/null 2>&1; then
    echo "[*] Starting Tor..."
    nohup tor -f "$ONION_DIR/torrc" > "$ONION_DIR/tor.out" 2>&1 &
    echo "$!" > "$ONION_DIR/tor.pid"
else
    echo "[+] Tor already running."
fi

# ---- 5. wait for the .onion address -----------------------------------------
HOSTNAME_FILE="$HIDDEN_DIR/hostname"
for i in $(seq 1 90); do
    if [ -s "$HOSTNAME_FILE" ]; then
        break
    fi
    sleep 1
done
if [ ! -s "$HOSTNAME_FILE" ]; then
    echo "[!] Tor did not publish an onion address yet. Check $ONION_DIR/tor.log" >&2
    echo "    (Tor may need a few minutes to bootstrap on a fresh identity.)" >&2
    exit 1
fi
ONION="$(tr -d '\r\n' < "$HOSTNAME_FILE")"

echo ""
echo "==============================================================="
echo "  Live stealth onion address :  http://$ONION"
echo "==============================================================="
echo ""
echo "  Only a client holding the private key can open this site."
echo ""
echo "  PRIVATE KEY (protect it):"
echo "    $PRIV"
echo ""
echo "  To browse it, install the private key on your client's Tor:"
echo "    see onion/CLIENT.md"
echo ""
echo "  Logs: $ONION_DIR/tor.log  |  web: $ONION_DIR/web.log"
