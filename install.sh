#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Shadow SE - install the `shadow-se` command onto your PATH so you can start
# the website from anywhere with a single word.
#
#   ./install.sh              # install to an on-PATH directory (auto-detected)
#   ./install.sh /custom/dir  # install to a specific directory
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -n "${1:-}" ]; then
    TARGET_DIR="$1"
else
    if [ -d "$HOME/.local/bin" ] && [ -w "$HOME/.local/bin" ]; then
        TARGET_DIR="$HOME/.local/bin"
    elif [ -d "$PREFIX/bin" ] && [ -w "$PREFIX/bin" ]; then
        TARGET_DIR="$PREFIX/bin"
    else
        TARGET_DIR="$HOME/.local/bin"
        mkdir -p "$TARGET_DIR"
    fi
fi

mkdir -p "$TARGET_DIR"
ln -sf "$ROOT/bin/shadow-se" "$TARGET_DIR/shadow-se"
chmod +x "$ROOT/bin/shadow-se"

echo "[+] Installed 'shadow-se' -> $TARGET_DIR/shadow-se"
if command -v shadow-se >/dev/null 2>&1; then
    echo "[+] shadow-se is on your PATH. Try:  shadow-se"
else
    echo "[!] $TARGET_DIR is not on your PATH. Add it with:"
    echo "      export PATH=\"$TARGET_DIR:\$PATH\""
fi
