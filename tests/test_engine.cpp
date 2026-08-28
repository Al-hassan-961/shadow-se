// SPDX-License-Identifier: MIT
// Shadow SE - end-to-end engine tests.
#include "shadowse/engine.hpp"

#include "test_framework.hpp"

#include <chrono>
#include <functional>
#include <thread>

using shadowse::Engine;

namespace {

bool waitFor(const std::function<bool()>& pred, int timeoutMs = 8000) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return pred();
}

} // namespace

TEST(engine_seeded_search) {
    Engine::Config cfg;
    cfg.useStubFetcher = true;
    Engine engine(cfg);
    engine.seedDemoData();

    // "onion" appears in both onion documents.
    const auto onion = engine.search("onion");
    CHECK(onion.size() >= 2u);
    for (const auto& r : onion) {
        CHECK(r.doc.isOnion());
    }

    // Case-insensitive: "SHADOW" matches seeded clearweb docs.
    const auto shadow = engine.search("SHADOW");
    CHECK(!shadow.empty());

    // Garbage query returns nothing.
    CHECK(engine.search("zzzz-no-such-term").empty());

    // ".onion" tokenizes to "onion" - same results.
    const auto dotOnion = engine.search(".onion");
    CHECK_EQ(dotOnion.size(), onion.size());
}

TEST(engine_crawl_grows_index) {
    Engine::Config cfg;
    cfg.useStubFetcher = true;
    Engine engine(cfg);
    engine.seedDemoData();
    const std::uint64_t seeded = engine.index().documentCount();

    CHECK(engine.crawl("http://example.com"));
    CHECK(waitFor([&] { return engine.pendingCrawls() == 0 && engine.crawledPages() >= 4; }));
    engine.shutdown();

    // Stub pages: root (depth 0) -> /docs/a, /docs/b, https://mirror (depth 1)
    // -> https://docs/a, https://docs/b (depth 2; scheme variants are distinct
    // URLs). Total: 4 + 2 = 6 crawled pages, all indexed.
    CHECK_EQ(engine.crawledPages(), 6u);
    CHECK_EQ(engine.index().documentCount(), seeded + 6);
    CHECK_EQ(engine.pendingCrawls(), 0u);
}

TEST(engine_search_finds_crawled_content) {
    Engine::Config cfg;
    cfg.useStubFetcher = true;
    Engine engine(cfg);
    engine.seedDemoData();

    CHECK(engine.crawl("http://news.example"));
    CHECK(waitFor([&] { return engine.crawledPages() >= 1; }));
    engine.shutdown();

    // Stub pages always mention "telemetry" in their body text.
    const auto results = engine.search("telemetry");
    bool foundCrawled = false;
    for (const auto& r : results) {
        if (r.doc.url == "http://news.example") {
            foundCrawled = true;
        }
    }
    CHECK(foundCrawled);
}

TEST(engine_normalize_url) {
    CHECK_EQ(shadowse::normalizeUrl("example.com/x"), "http://example.com/x");
    CHECK_EQ(shadowse::normalizeUrl("https://a.b/c"), "https://a.b/c");
    CHECK_EQ(shadowse::normalizeUrl("  "), "");
}
