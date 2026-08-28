// SPDX-License-Identifier: MIT
// Shadow SE - crawler and HTML extraction tests.
#include "shadowse/crawler.hpp"
#include "shadowse/fetcher.hpp"
#include "shadowse/inverted_index.hpp"

#include "test_framework.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using shadowse::Crawler;
using shadowse::Document;
using shadowse::InvertedIndex;
using shadowse::StubFetcher;

namespace {

bool waitFor(const std::function<bool()>& pred, int timeoutMs = 5000) {
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

TEST(html_title_extraction) {
    CHECK_EQ(shadowse::htmlExtractTitle("<html><head><title>My Page &amp; Co</title></head></html>"),
             "My Page &amp; Co");
    CHECK(shadowse::htmlExtractTitle("<html><body>no title</body></html>").empty());
}

TEST(html_description_extraction) {
    const std::string html = "<meta name=\"description\" content=\"A short summary here\">";
    CHECK_EQ(shadowse::htmlExtractDescription(html), "A short summary here");
    CHECK(shadowse::htmlExtractDescription("<html></html>").empty());
}

TEST(html_strip_tags) {
    const std::string text = shadowse::htmlStripTags("<p>Hello <b>world</b>!</p><br><p>Second.</p>");
    CHECK_EQ(text, "Hello world! Second.");
}

TEST(html_link_extraction_and_resolution) {
    const std::string html =
        "<a href=\"/docs/a\">A</a> <a href=\"docs/b\">B</a> "
        "<a href=\"https://other.example/x\">C</a> <a href=\"//proto.example/y\">D</a> "
        "<a href=\"mailto:x@y.z\">E</a>";
    const auto links = shadowse::htmlExtractLinks(html, "http://base.example/root/page.html");
    CHECK_EQ(links.size(), 4u);
    CHECK_EQ(links[0], "http://base.example/docs/a");
    CHECK_EQ(links[1], "http://base.example/root/docs/b");
    CHECK_EQ(links[2], "https://other.example/x");
    CHECK_EQ(links[3], "http://proto.example/y");
}

TEST(crawler_indexes_stub_pages) {
    InvertedIndex index;
    Crawler::Options opts;
    opts.workerCount = 2;
    opts.maxDepth = 1;
    auto crawler = std::make_unique<Crawler>(index, std::make_shared<StubFetcher>(), opts);

    CHECK(crawler->enqueue("http://example.com/root"));
    CHECK(!crawler->enqueue("http://example.com/root"));  // duplicate rejected
    CHECK(waitFor([&] { return crawler->crawledCount() >= 1; }));

    const Document* root = nullptr;
    for (const Document& d : index.allDocuments()) {
        if (d.url == "http://example.com/root") {
            root = &d;
        }
    }
    CHECK(root != nullptr);
    CHECK_EQ(root->title, "Stub Index: example.com");
    CHECK(root->content.find("telemetry") != std::string::npos);

    // Depth 1: root + /docs/a + /docs/b + /mirror = 4 pages, then dedup stops it.
    crawler->stop();
    CHECK(waitFor([&] { return crawler->pendingCount() == 0; }));
    CHECK_EQ(crawler->crawledCount(), 4u);
    CHECK_EQ(index.documentCount(), 4u);
}

TEST(crawler_onion_source_detection) {
    InvertedIndex index;
    Crawler::Options opts;
    opts.workerCount = 1;
    opts.maxDepth = 0;  // never follow links
    auto crawler = std::make_unique<Crawler>(index, std::make_shared<StubFetcher>(), opts);
    CHECK(crawler->enqueue("http://deadbeef1234.onion/index"));
    CHECK(waitFor([&] { return crawler->crawledCount() >= 1; }));
    crawler->stop();

    const Document* doc = nullptr;
    for (const Document& d : index.allDocuments()) {
        if (d.url == "http://deadbeef1234.onion/index") {
            doc = &d;
        }
    }
    CHECK(doc != nullptr);
    CHECK(doc->isOnion());
}

TEST(crawler_cap_is_respected) {
    InvertedIndex index;
    Crawler::Options opts;
    opts.workerCount = 1;
    opts.maxDepth = 2;
    opts.maxPages = 3;
    auto crawler = std::make_unique<Crawler>(index, std::make_shared<StubFetcher>(), opts);
    CHECK(crawler->enqueue("http://example.com/root"));
    // The remaining budget (3 - 1 = 2) gets consumed by link discovery.
    crawler->stop();
    CHECK(waitFor([&] { return crawler->pendingCount() == 0; }));
    CHECK(crawler->crawledCount() <= 3u);
    CHECK_EQ(index.documentCount(), crawler->crawledCount());
}
