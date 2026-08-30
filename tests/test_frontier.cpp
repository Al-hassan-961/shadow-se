// SPDX-License-Identifier: MIT
// Shadow SE - advanced crawler frontier tests (priority, politeness, robots, dedup).
#include "shadowse/crawler.hpp"
#include "shadowse/fetcher.hpp"
#include "shadowse/inverted_index.hpp"

#include "test_framework.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using shadowse::Crawler;
using shadowse::Document;
using shadowse::Fetcher;
using shadowse::FetchResult;
using shadowse::InvertedIndex;
using shadowse::SourceType;

namespace {

bool waitFor(const std::function<bool()>& pred, int timeoutMs = 8000) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

// Records the order (and timestamps) of non-robots page fetches.
class RecordingFetcher : public Fetcher {
public:
    std::map<std::string, std::string> robotsBodies;  // origin -> robots.txt body
    std::chrono::milliseconds perFetchSleep{0};
    bool blockRobots = false;

    FetchResult fetch(const std::string& url) override {
        if (url.find("/robots.txt") != std::string::npos) {
            if (blockRobots) {
                FetchResult r;
                r.error = "robots fetch denied";
                return r;
            }
            const std::string origin = url.substr(0, url.find("/robots.txt"));
            const auto it = robotsBodies.find(origin);
            FetchResult r;
            r.ok = true;
            r.html = it == robotsBodies.end() ? "User-agent: *\nAllow: /\n" : it->second;
            return r;
        }
        if (perFetchSleep.count() > 0) {
            std::this_thread::sleep_for(perFetchSleep);
        }
        {
            std::lock_guard<std::mutex> lock(mtx_);
            order_.push_back(url);
            times_.push_back(std::chrono::steady_clock::now());
        }
        FetchResult r;
        r.ok = true;
        r.html = "<html><head><title>Page</title></head><body>content " + url +
                 "</body></html>";
        return r;
    }
    std::string name() const override { return "recording"; }

    std::vector<std::string> order() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return order_;
    }
    std::vector<std::chrono::steady_clock::time_point> times() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return times_;
    }

private:
    mutable std::mutex mtx_;
    std::vector<std::string> order_;
    std::vector<std::chrono::steady_clock::time_point> times_;
};

std::size_t pos(const std::vector<std::string>& v, const std::string& s) {
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == s) return i;
    }
    return static_cast<std::size_t>(-1);
}

} // namespace

TEST(frontier_priority_scheduling) {
    InvertedIndex index;
    auto fetcher = std::make_shared<RecordingFetcher>();
    fetcher->perFetchSleep = std::chrono::milliseconds(30);  // keep all enqueued
    Crawler::Options opts;
    opts.workerCount = 1;
    opts.obeyRobots = false;
    opts.domainDelay = std::chrono::milliseconds(0);
    opts.maxDepth = 0;
    Crawler c(index, fetcher, opts);

    // Enqueue low-priority first; priority must override FIFO for items that
    // are queued together.
    c.enqueue("http://a.example/low", 0, 10);   // lowest priority
    c.enqueue("http://b.example/high", 0, 0);   // highest priority
    c.enqueue("http://c.example/mid", 0, 5);
    CHECK(waitFor([&] { return c.crawledCount() >= 3; }));
    c.stop();

    const auto order = fetcher->order();
    CHECK_EQ(order.size(), 3u);
    // high (pri 0) before mid (pri 5) before low (pri 10).
    CHECK(pos(order, "http://b.example/high") < pos(order, "http://c.example/mid"));
    CHECK(pos(order, "http://c.example/mid") < pos(order, "http://a.example/low"));
}

TEST(frontier_domain_politeness) {
    InvertedIndex index;
    auto fetcher = std::make_shared<RecordingFetcher>();
    Crawler::Options opts;
    opts.workerCount = 1;
    opts.obeyRobots = false;
    opts.domainDelay = std::chrono::milliseconds(250);
    opts.maxDepth = 0;
    Crawler c(index, fetcher, opts);

    c.enqueue("http://site.example/a");
    c.enqueue("http://site.example/b");
    CHECK(waitFor([&] { return c.crawledCount() >= 2; }));
    c.stop();

    const auto times = fetcher->times();
    CHECK_EQ(times.size(), 2u);
    const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(times[1] - times[0]);
    CHECK(gap.count() >= 200);  // >= domainDelay with a small slack
}

TEST(frontier_robots_compliance) {
    InvertedIndex index;
    auto fetcher = std::make_shared<RecordingFetcher>();
    fetcher->robotsBodies["http://site.example"] =
        "User-agent: *\nDisallow: /secret/\n";
    Crawler::Options opts;
    opts.workerCount = 1;
    opts.obeyRobots = true;
    opts.domainDelay = std::chrono::milliseconds(0);
    opts.maxDepth = 0;
    Crawler c(index, fetcher, opts);

    std::vector<std::string> errors;
    c.setCallbacks([](const Document&) {},
                   [&](const std::string& url, const std::string& e) { errors.push_back(url); });

    const std::uint64_t before = index.documentCount();
    c.enqueue("http://site.example/secret/page");   // disallowed
    c.enqueue("http://site.example/public");        // allowed
    CHECK(waitFor([&] { return c.crawledCount() >= 2; }));
    c.stop();

    // The disallowed URL must not be indexed; an error was reported for it.
    CHECK_EQ(index.documentCount(), before + 1);
    bool blockedReported = false;
    for (const auto& u : errors) {
        if (u.find("secret") != std::string::npos) blockedReported = true;
    }
    CHECK(blockedReported);
}

TEST(frontier_dedup) {
    InvertedIndex index;
    auto fetcher = std::make_shared<RecordingFetcher>();
    Crawler::Options opts;
    opts.workerCount = 1;
    opts.obeyRobots = false;
    opts.domainDelay = std::chrono::milliseconds(0);
    opts.maxDepth = 0;
    Crawler c(index, fetcher, opts);

    CHECK(c.enqueue("http://site.example/page"));
    CHECK(!c.enqueue("http://site.example/page"));  // duplicate rejected
    CHECK(waitFor([&] { return c.crawledCount() >= 1; }));
    c.stop();
    CHECK_EQ(fetcher->order().size(), 1u);
}

TEST(frontier_page_cap) {
    InvertedIndex index;
    auto fetcher = std::make_shared<RecordingFetcher>();
    Crawler::Options opts;
    opts.workerCount = 1;
    opts.obeyRobots = false;
    opts.domainDelay = std::chrono::milliseconds(0);
    opts.maxDepth = 0;
    opts.maxPages = 2;
    Crawler c(index, fetcher, opts);

    CHECK(c.enqueue("http://site.example/a"));
    CHECK(c.enqueue("http://site.example/b"));
    CHECK(!c.enqueue("http://site.example/c"));  // cap reached
    CHECK(waitFor([&] { return c.crawledCount() >= 2; }));
    c.stop();
    CHECK_EQ(fetcher->order().size(), 2u);
}
