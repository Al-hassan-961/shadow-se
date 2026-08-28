// SPDX-License-Identifier: MIT
// Shadow SE - asynchronous breadth-first crawler with a worker pool.
#pragma once

#include "shadowse/document.hpp"
#include "shadowse/fetcher.hpp"
#include "shadowse/inverted_index.hpp"

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
        std::chrono::milliseconds requestDelay{0};          // politeness delay
    };

    using OnDocument = std::function<void(const Document&)>;
    using OnError = std::function<void(const std::string& url, const std::string& error)>;

    Crawler(InvertedIndex& index, std::shared_ptr<Fetcher> fetcher, Options opts);
    ~Crawler();

    Crawler(const Crawler&) = delete;
    Crawler& operator=(const Crawler&) = delete;

    void setCallbacks(OnDocument onDoc, OnError onErr);

    // Adds a URL to the crawl frontier (auto-starts workers on first call).
    // Returns false when the page cap has been reached or the URL is a dup.
    bool enqueue(const std::string& url, std::uint32_t depth = 0);

    // Stops accepting new URLs; workers drain the remaining queue and exit.
    void stop();

    std::size_t pendingCount() const;
    std::size_t crawledCount() const;
    bool running() const;

private:
    void workerLoop();
    void process(const std::string& url, std::uint32_t depth);

    InvertedIndex& index_;
    std::shared_ptr<Fetcher> fetcher_;
    Options opts_;
    OnDocument onDoc_;
    OnError onErr_;

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<std::pair<std::string, std::uint32_t>> queue_;
    std::unordered_set<std::string> seenUrls_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::size_t pending_ = 0;
    std::size_t crawled_ = 0;
};

// --- HTML extraction helpers (exposed for tests) -----------------------------

std::string htmlExtractTitle(const std::string& html);
std::string htmlExtractDescription(const std::string& html);
std::string htmlStripTags(const std::string& html);
// Absolute-URL resolution of links found in `html` against `baseUrl`.
std::vector<std::string> htmlExtractLinks(const std::string& html, const std::string& baseUrl);

} // namespace shadowse
