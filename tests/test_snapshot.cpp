// SPDX-License-Identifier: MIT
// Shadow SE - encrypted snapshot serialization tests.
#include "shadowse/snapshot.hpp"

#include "test_framework.hpp"

#include <cstdio>
#include <string>
#include <vector>

using shadowse::Document;
using shadowse::SourceType;
using shadowse::deserializeDocuments;
using shadowse::loadEncryptedSnapshot;
using shadowse::saveEncryptedSnapshot;
using shadowse::serializeDocuments;

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

TEST(snapshot_roundtrip_serialization) {
    const std::vector<Document> docs = sampleDocs();
    const std::string data = serializeDocuments(docs);
    std::vector<Document> out;
    std::string err;
    CHECK(deserializeDocuments(data, &out, &err));
    CHECK_EQ(out.size(), 2u);
    CHECK_EQ(out[0].url, docs[0].url);
    CHECK_EQ(out[0].title, docs[0].title);
    CHECK_EQ(out[0].content, docs[0].content);
    CHECK(out[0].isOnion());
    CHECK_EQ(out[1].url, docs[1].url);
    CHECK(!out[1].isOnion());
    CHECK_EQ(out[1].depth, 3u);
}

TEST(snapshot_rejects_garbage) {
    std::vector<Document> out;
    std::string err;
    CHECK(!deserializeDocuments("not a snapshot", &out, &err));
    CHECK(!err.empty());
}

TEST(snapshot_encrypted_save_load) {
    const std::vector<Document> docs = sampleDocs();
    const std::string path = tempPath("enc");
    std::string err;
    CHECK(saveEncryptedSnapshot(path, "correct-horse-battery", docs, &err));

    std::vector<Document> loaded;
    CHECK(loadEncryptedSnapshot(path, "correct-horse-battery", &loaded, &err));
    CHECK_EQ(loaded.size(), 2u);
    CHECK_EQ(loaded[0].url, docs[0].url);
    CHECK(loaded[0].isOnion());
    std::remove(path.c_str());
}

TEST(snapshot_wrong_password_fails) {
    const std::vector<Document> docs = sampleDocs();
    const std::string path = tempPath("wrongpw");
    std::string err;
    CHECK(saveEncryptedSnapshot(path, "correct", docs, &err));
    std::vector<Document> loaded;
    CHECK(!loadEncryptedSnapshot(path, "wrong", &loaded, &err));
    CHECK(!err.empty());
    std::remove(path.c_str());
}

TEST(snapshot_tampered_file_fails) {
    const std::vector<Document> docs = sampleDocs();
    const std::string path = tempPath("tamper");
    std::string err;
    CHECK(saveEncryptedSnapshot(path, "pw", docs, &err));

    // Corrupt a byte in the ciphertext region.
    {
        FILE* f = std::fopen(path.c_str(), "r+b");
        CHECK(f != nullptr);
        std::fseek(f, 6 + 16 + 24 + 8 + 3, SEEK_SET);  // magic|salt|nonce|len + offset
        const char flip = 'X';
        std::fwrite(&flip, 1, 1, f);
        std::fclose(f);
    }
    std::vector<Document> loaded;
    CHECK(!loadEncryptedSnapshot(path, "pw", &loaded, &err));
    std::remove(path.c_str());
}
