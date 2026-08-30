// SPDX-License-Identifier: MIT
// Shadow SE - engine facade: index + ranking + Tor routing + crawler.
#pragma once

#include "shadowse/bm25.hpp"
#include "shadowse/crawler.hpp"
#include "shadowse/fetcher.hpp"
#include "shadowse/inverted_index.hpp"
#include "shadowse/tor_proxy.hpp"
#include "shadowse/tor_proxy_manager.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace shadowse {

class Engine {
public:
    struct Config {
        bool useStubFetcher = true;   // deterministic demo mode
        std::string torHost = "127.0.0.1";
        std::uint16_t torPort = 9050;
        std::vector<std::string> torProxies;          // extra "host:port" endpoints
        bool useProxyPool = false;                    // --tor-pool
        std::size_t onionRetries = 0;                 // --onion-retries
        std::chrono::seconds circuitRotateInterval{0}; // --circuit-rotate
        std::size_t crawlerWorkers = 2;
        std::size_t crawlerMaxPages = 64;
        std::uint32_t crawlerMaxDepth = 2;
    };

    explicit Engine(Config cfg);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Real SOCKS5 liveness probe of the local Tor daemon.
    TorProbeResult probeTor();

    // Tokenizes `query` and ranks the index (BM25).
    std::vector<ScoredDocument> search(const std::string& query, std::size_t limit = 10);

    // Asynchronous crawl; discovered pages are indexed by the worker pool.
    bool crawl(const std::string& url);

    std::size_t pendingCrawls() const;
    std::size_t crawledPages() const;
    bool crawlerBusy() const;

    // Loads the built-in demo corpus so the engine is useful out of the box.
    void seedDemoData();

    // Encrypts the current index to `path` (XChaCha20-Poly1305, Argon2id KDF).
    bool saveEncrypted(const std::string& path, const std::string& password,
                       std::string* err) const;
    // Replaces the index with the contents of an encrypted snapshot.
    bool loadEncrypted(const std::string& path, const std::string& password,
                       std::string* err);

    // Plain binary persistence (fast disk round-trip between runs).
    bool saveState(const std::string& path, std::string* err) const;
    bool loadState(const std::string& path, std::string* err);

    // Drains and joins the crawler.
    void shutdown();

    InvertedIndex& index() { return index_; }
    const InvertedIndex& index() const { return index_; }
    Crawler& crawler() { return *crawler_; }
    const Crawler& crawler() const { return *crawler_; }
    const Fetcher& fetcher() const { return *fetcher_; }
    TorProxyManager& torManager() { return *torManager_; }
    const TorProxyManager& torManager() const { return *torManager_; }
    const TorProbeResult& lastTor() const { return lastTor_; }

private:
    Config cfg_;
    InvertedIndex index_;
    std::shared_ptr<TorProxyManager> torManager_;  // shared with the fetcher
    std::shared_ptr<Fetcher> fetcher_;
    std::unique_ptr<Crawler> crawler_;
    TorProbeResult lastTor_;
};

// Normalizes user input into an absolute http(s) URL, e.g. "example.com/x" ->
// "http://example.com/x". Empty result means the input is unusable.
std::string normalizeUrl(const std::string& input);

} // namespace shadowse
