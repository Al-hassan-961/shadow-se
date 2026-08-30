# Connecting to the Shadow SE stealth onion

The onion service runs in **stealth mode** (v3 client authorization): it only
answers to clients that present a matching **x25519** private key. Everyone
else receives a `404 / Not Found`.

## On the client (your Tor Browser / Tor daemon)

You need two things:

1. The onion address printed by `onion/setup.sh` (e.g. `abc123....onion`).
2. The **private key** printed by `onion/setup.sh` (a base64 string).

### Option A — Tor Browser

1. Click the onion icon → **Onion Services Settings**.
2. Enable **"Authenticate to services with my client authorization keys"**.
3. Add a key entry:
   - **Service:** the onion address **without** `.onion`.
   - **Client authorization private key:** paste the private key base64 string.
4. Visit `http://<address>.onion`.

### Option B — a local Tor daemon (e.g. on Linux/Termux)

Create a directory for your client authorization keys:

```bash
mkdir -p ~/.tor/onion_auth && chmod 700 ~/.tor/onion_auth
```

Write the private key to a file named after the onion address (no `.onion`):

```bash
ADDR=abc123....onion          # replace with the actual address
PRIV="<the private key from setup.sh>"
printf '%s:descriptor:x25519:%s\n' "${ADDR%.onion}" "$PRIV" \
  > "$HOME/.tor/onion_auth/${ADDR%.onion}.auth_private"
chmod 600 "$HOME/.tor/onion_auth/${ADDR%.onion}.auth_private"
```

Then tell Tor where to find it in your `torrc`:

```
ClientOnionAuthDir /home/you/.tor/onion_auth
```

Restart Tor, then browse to `http://<address>.onion` (a Tor Browser, or
`proxychains curl http://<address>.onion/` with the SOCKS5 port).

## Security notes

- The **private key grants access to the site** — share it only with people you
  want to be able to open the onion. Losing it does not revoke it; to revoke,
  delete `authorized_clients/<name>.auth` on the server and add a new key.
- The service itself never sees client identities (Tor hides both directions).
- The web UI sets no cookies, keeps no logs, and sends strict privacy headers.
