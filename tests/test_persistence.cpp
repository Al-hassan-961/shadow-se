// SPDX-License-Identifier: MIT
// Shadow SE - disk persistence tests (plain binary snapshot round-trip).
#include "shadowse/engine.hpp"
#include "shadowse/snapshot.hpp"

#include "test_framework.hpp"

#include <cstdio>
#include <string>
#include <vector>

using shadowse::Document;
using shadowse::SourceType;
using shadowse::loadSnapshot;
using shadowse::saveSnapshot;

namespace {

std::string tempPath(const char* name) {
    static int counter = 0;
    return "/data/data/com.termux/files/home/Shadow SE/build/tmp_" +
           std::string(name) + "_" + std::to_string(counter++);
}

std::vector<Document> sampleDocs() {
    std::vector<Document> docs;
    Document a;
    a.url = "http://shadow77ivq2cc3x.onion/index/vault";
    a.title = "DeepNet Secured Vault // Index Node 04";
    a.snippet = "Encrypted storage node telemetry";
    a.content = "Onion vault with historical metadata over the tor network.";
    a.source = SourceType::Onion;
    a.depth = 0;
    Document b;
    b.url = "https://shadow-se.internal/intel/feed-2026";
    b.title = "Shadow SE Threat Intelligence Feed";
    b.content = "Clearweb telemetry and IOC correlation matrices.";
    b.source = SourceType::ClearWeb;
    b.depth = 3;
    docs.push_back(a);
    docs.push_back(b);
    return docs;
}

} // namespace

TEST(persistence_snapshot_roundtrip) {
    const std::vector<Document> docs = sampleDocs();
    const std::string path = tempPath("plain");
    std::string err;
    CHECK(saveSnapshot(path, docs, &err));

    std::vector<Document> loaded;
    CHECK(loadSnapshot(path, &loaded, &err));
    CHECK_EQ(loaded.size(), 2u);
    CHECK_EQ(loaded[0].url, docs[0].url);
    CHECK_EQ(loaded[0].content, docs[0].content);
    CHECK(loaded[0].isOnion());
    CHECK_EQ(loaded[1].depth, 3u);
    std::remove(path.c_str());
}

TEST(persistence_missing_and_corrupt) {
    std::string err;
    std::vector<Document> docs;
    CHECK(!loadSnapshot("/nonexistent/path.bin", &docs, &err));
    CHECK(!err.empty());

    const std::string path = tempPath("corrupt");
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        std::fwrite("this is not a snapshot", 1, 21, f);
        std::fclose(f);
    }
    err.clear();
    CHECK(!loadSnapshot(path, &docs, &err));
    CHECK(!err.empty());
    std::remove(path.c_str());
}

TEST(persistence_engine_state_roundtrip) {
    shadowse::Engine::Config cfg;
    cfg.useStubFetcher = true;
    shadowse::Engine engine(cfg);
    engine.seedDemoData();
    const std::uint64_t before = engine.index().documentCount();
    CHECK(before > 0);

    const std::string path = tempPath("state");
    std::string err;
    CHECK(engine.saveState(path, &err));

    // A fresh engine loads the persisted state.
    shadowse::Engine engine2(cfg);
    CHECK(engine2.loadState(path, &err));
    CHECK_EQ(engine2.index().documentCount(), before);

    // The loaded index is actually searchable.
    const auto results = engine2.search("onion");
    CHECK(!results.empty());
    std::remove(path.c_str());
}
