// SPDX-License-Identifier: MIT
// Shadow SE - asynchronous crawler with an advanced frontier.
//
// Frontier features:
//   - priority scheduling (lower number = higher priority; discovered links
//     are prioritized by depth),
//   - per-domain politeness delays,
//   - duplicate-URL filtering (exact set backed by a Bloom filter),
//   - robots.txt compliance (cached per origin, fail-open on fetch errors).
#pragma once

#include "shadowse/bloom_filter.hpp"
#include "shadowse/document.hpp"
#include "shadowse/fetcher.hpp"
#include "shadowse/inverted_index.hpp"
#include "shadowse/robots_txt.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace shadowse {

class Crawler {
public:
    struct Options {
        std::size_t workerCount = 2;
        std::size_t maxPages = 64;                          // hard cap on crawled pages
        std::uint32_t maxDepth = 2;                         // link-follow depth
        std::chrono::milliseconds requestDelay{0};          // global politeness delay
        std::chrono::milliseconds domainDelay{100};         // min gap between fetches per domain
        bool obeyRobots = true;                             // honor robots.txt
        std::string userAgent = "ShadowSE/1.0";
        std::size_t bloomExpected = 10000;                  // dedup Bloom filter capacity
    };

    using OnDocument = std::function<void(const Document&)>;
    using OnError = std::function<void(const std::string& url, const std::string& error)>;

    Crawler(InvertedIndex& index, std::shared_ptr<Fetcher> fetcher, Options opts);
    ~Crawler();

    Crawler(const Crawler&) = delete;
    Crawler& operator=(const Crawler&) = delete;

    void setCallbacks(OnDocument onDoc, OnError onErr);

    // Adds a URL to the frontier (auto-starts workers on first call).
    // `priority`: 0 is highest; -1 (default) uses `depth`. Returns false when
    // the page cap is reached or the URL is a duplicate.
    bool enqueue(const std::string& url, std::uint32_t depth = 0, int priority = -1);

    // Stops accepting new URLs; workers drain the remaining queue and exit.
    void stop();

    std::size_t pendingCount() const;
    std::size_t crawledCount() const;
    bool running() const;

private:
    struct Item {
        std::string url;
        std::uint32_t depth;
        int priority;
        std::uint64_t seq;
    };
    struct ItemCompare {
        bool operator()(const Item& a, const Item& b) const {
            if (a.priority != b.priority) return a.priority > b.priority;  // min first
            return a.seq > b.seq;                                          // FIFO tie-break
        }
    };

    void workerLoop();
    void process(const std::string& url, std::uint32_t depth);
    void politenessWait(const std::string& domain);
    std::string domainOf(const std::string& url) const;
    static bool isRobotsUrl(const std::string& url);

    InvertedIndex& index_;
    std::shared_ptr<Fetcher> fetcher_;
    Options opts_;
    OnDocument onDoc_;
    OnError onErr_;

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::priority_queue<Item, std::vector<Item>, ItemCompare> queue_;
    std::unordered_set<std::string> seenUrls_;  // authoritative dedup set
    BloomFilter bloom_;                         // fast-membership layer for dedup
    std::vector<std::thread> workers_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::size_t pending_ = 0;
    std::size_t crawled_ = 0;
    std::uint64_t seq_ = 0;

    RobotsTxt robots_;

    mutable std::mutex politenessMtx_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastFetch_;
};

// --- HTML extraction helpers (exposed for tests) -----------------------------

std::string htmlExtractTitle(const std::string& html);
std::string htmlExtractDescription(const std::string& html);
std::string htmlStripTags(const std::string& html);
// Absolute-URL resolution of links found in `html` against `baseUrl`.
std::vector<std::string> htmlExtractLinks(const std::string& html, const std::string& baseUrl);

} // namespace shadowse
