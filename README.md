# Shadow SE — Secure Search Engine

High-performance terminal interface and engine core for **clearweb** and
**darkweb (.onion)** search, crawling, and indexing.

- **Repository:** https://github.com/Al-hassan-961/shadow-se
- **Author:** Al-hassan shehade
- **Language:** C++20, thread-based concurrency
- **Build:** CMake ≥ 3.16 (verified with Clang 21 on Termux/Android and Linux)

> ⚠️ **Responsibility notice.** Crawl only content you are authorized to access.
> Tor usage must comply with the laws of your jurisdiction. This project ships
> with an offline **stub fetcher** by default so the pipeline can be studied and
> tested without touching the network.

---

## Quick start

```bash
git clone https://github.com/Al-hassan-961/shadow-se.git
cd shadow-se

cmake -S . -B build -DSHADOWSE_BUILD_TESTS=ON
cmake --build build -j"$(nproc)"

./build/shadow-se --stub     # offline demo (default, no network needed)
./build/shadow-se --curl     # real HTTP/HTTPS fetching (needs libcurl build)
```

Try these once the prompt appears:

```text
shadow-se@core:~$ search onion          # BM25-ranked darkweb results
shadow-se@core:~$ crawl example.com     # async crawl, watch [+] events stream in
shadow-se@core:~$ status                # Tor probe + index/crawler stats
shadow-se@core:~$ help
shadow-se@core:~$ quit
```

## Features

| Component | Description |
| --- | --- |
| **Inverted index** | Thread-safe in-memory index; title tokens weighted 3×; URL-keyed document replacement; un-index on removal. |
| **BM25 ranking** | Classic Robertson–Walker BM25 (`k1 = 1.2`, `b = 0.75`) with OR query semantics. |
| **Tokenizer** | UTF-8 aware: ASCII, Latin-1/Latin Extended, Greek, Cyrillic and CJK tokens with case folding; malformed UTF-8 tolerated. |
| **Tor routing** | Real SOCKS5 greeting handshake against `127.0.0.1:9050` with a deadline; onion crawls are routed through a `socks5h://` proxy when Tor is up. |
| **Crawler** | Async worker pool with a bounded frontier, depth limits, URL dedup, page caps, and politeness delay. |
| **Fetchers** | `StubFetcher` (deterministic offline HTML) and `CurlFetcher` (libcurl, optional). |
| **Encryption** | Authenticated encryption at rest — **XChaCha20-Poly1305 AEAD** (256-bit, IND-CCA) + **Argon2id** key derivation via libsodium. `save`/`load` encrypted index snapshots. |
| **Web front end** | Privacy-hardened HTTP search UI: no cookies, no logs, strict CSP, no trackers, XSS-safe, loopback-only. |
| **Stealth onion** | v3 Tor hidden service with **client authorization** (only key-holders can reach it). Tooling + deploy script in `onion/`. |
| **Terminal UI** | Interactive shell with live crawl event stream and engine health panel. |

## Layout

```
include/shadowse/   public headers (document, tokenizer, index, bm25, tor, fetcher, crawler, engine, crypto, snapshot, web_server)
src/                implementations + main.cpp (terminal UI), web_main.cpp, se_keygen.cpp
onion/              stealth onion service tooling (setup.sh, torrc, CLIENT.md)
tests/              unit tests (self-contained framework, CTest integrated)
CMakeLists.txt      build configuration
```

## Build & test

```bash
cmake -S . -B build -DSHADOWSE_BUILD_TESTS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Options:

- `-DSHADOWSE_USE_CURL=OFF` — force the stub fetcher (no libcurl dependency).
- `-DSHADOWSE_ENABLE_CRYPTO=OFF` — build without libsodium encryption.
- `-DSHADOWSE_SANITIZE=ON` — build with ASan + UBSan.
- `-DSHADOWSE_BUILD_TESTS=OFF` — skip the test suite.

Binaries produced: `shadow-se` (terminal UI), `shadow-se-web` (web front end),
`se-keygen` (stealth onion keys), `shadow-se-tests`.

## Usage

```text
$ ./build/shadow-se [--stub | --curl]

  search <query>   Run a BM25 inverted index query (clearweb and .onion)
  crawl  <url>     Dispatch the asynchronous sandbox crawler to ingest a target
  save   <path>    Encrypt the index to a snapshot (XChaCha20-Poly1305 + Argon2id)
  load   <path>    Load an encrypted snapshot into the index
  status           Show Tor probe, index statistics and crawler queue
  help             Display the command menu
  exit / quit      Drain the crawler and safely terminate
```

Examples:

```text
shadow-se@core:~$ search onion
shadow-se@core:~$ search ".onion telemetry"
shadow-se@core:~$ crawl example.com
shadow-se@core:~$ crawl http://deadbeef1234.onion/index
shadow-se@core:~$ save snapshot.enc     # prompts for a passphrase
shadow-se@core:~$ load snapshot.enc
```

- Bare input is treated as a search query.
- `--curl` enables real network fetching (requires a libcurl build); `.onion`
  targets then require a running Tor daemon on `127.0.0.1:9050`. With `--stub`
  (default) every crawl produces deterministic sample content — ideal for
  demos and CI.
- `exit` drains pending crawls before terminating.

## Encryption at rest

`save <path>` encrypts the whole index with **XChaCha20-Poly1305** (a modern
256-bit authenticated cipher) and derives the key from your passphrase with
**Argon2id**. `load <path>` authenticates and restores it. The snapshot file is
opaque (no plaintext, random nonce + salt per save) and tampering is detected.

This is the honest way to do "strong encryption": AES-256 already provides
~256-bit security, so the meaningful upgrades are *authenticated* encryption
and a memory-hard key-derivation function — not a home-grown cipher (which
would be weaker). `CryptoBox` in `crypto.{hpp,cpp}` wraps libsodium.

## Web front end (privacy hardened)

```bash
./build/shadow-se-web --port 8080 --stub          # offline demo
./build/shadow-se-web --port 8080 --load snap.enc --password "$PW"
```

Serves a search page over the engine with **no cookies, no logs, and strict
privacy headers** (CSP, `no-referrer`, `Permissions-Policy`, `Clear-Site-Data`,
`X-Frame-Options`), HTML-escaped output, and binds to **loopback only**.
Expose it exclusively through a Tor onion service (below).

## Stealth onion service

A v3 Tor hidden service that only answers to clients holding a private key
(client authorization / "stealth"):

```bash
bash onion/setup.sh
```

The script builds the project, starts the web UI on `127.0.0.1:8080`, generates
a stealth client keypair, writes a hardened `onion/torrc`, starts Tor, and
prints the live `.onion` address plus the private key. Install that private key
on your client's Tor as described in `onion/CLIENT.md`; without it, the onion
returns `404`.

`se-keygen` generates additional stealth client keys and strong passphrases:

```bash
./build/se-keygen --client friend1        # a new authorized client
./build/se-keygen --passphrase 48         # a strong random passphrase
```

## Architecture

```text
┌────────────────────────────────────────────────────────────┐
│  Terminal UI (src/main.cpp)                                │
│  search / crawl / status commands                          │
└──────────────┬───────────────────────────────┬─────────────┘
               │                               │
       ┌───────▼────────┐             ┌────────▼─────────┐
       │ Engine facade  │             │ Crawler (pool)  │
       │ (engine.cpp)   │◄────────────│ - worker threads│
       └───────┬────────┘             │ - HTML parser   │
               │                      │ - URL frontier  │
       ┌───────▼────────┐             └────────┬─────────┘
       │ BM25Ranker     │                      │
       └───────┬────────┘               ┌──────▼──────┐
       ┌───────▼────────┐               │ Fetcher     │
       │ InvertedIndex  │◄──────────────│ - stub      │
       │ (mutex-guarded)│               │ - libcurl   │
       └────────────────┘               └──────┬──────┘
                                      ┌────────▼─────────┐
                                      │ Tor SOCKS5 probe │
                                      │ (127.0.0.1:9050) │
                                      └──────────────────┘
```

## Testing

The suite covers the tokenizer (ASCII/Unicode/CJK/case folding), index
semantics (weighting, replacement, removal), BM25 ordering, HTML extraction,
crawler behavior (dedup, caps, onion detection) and end-to-end engine flows.
Run with `ctest --test-dir build --output-on-failure`.

## License

MIT — see [LICENSE](LICENSE).
