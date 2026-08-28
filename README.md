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
| **Terminal UI** | Interactive shell with live crawl event stream and engine health panel. |

## Layout

```
include/shadowse/   public headers (document, tokenizer, index, bm25, tor, fetcher, crawler, engine)
src/                implementations + main.cpp (terminal UI)
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
- `-DSHADOWSE_SANITIZE=ON` — build with ASan + UBSan.
- `-DSHADOWSE_BUILD_TESTS=OFF` — skip the test suite.

## Usage

```text
$ ./build/shadow-se [--stub | --curl]

  search <query>   Run a BM25 inverted index query (clearweb and .onion)
  crawl  <url>     Dispatch the asynchronous sandbox crawler to ingest a target
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
```

- Bare input is treated as a search query.
- `--curl` enables real network fetching (requires a libcurl build); `.onion`
  targets then require a running Tor daemon on `127.0.0.1:9050`. With `--stub`
  (default) every crawl produces deterministic sample content — ideal for
  demos and CI.
- `exit` drains pending crawls before terminating.

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
