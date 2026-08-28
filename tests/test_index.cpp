// SPDX-License-Identifier: MIT
// Shadow SE - inverted index tests.
#include "shadowse/inverted_index.hpp"

#include "test_framework.hpp"

using shadowse::Document;
using shadowse::InvertedIndex;
using shadowse::Posting;
using shadowse::SourceType;

namespace {

Document makeDoc(const std::string& url, const std::string& title,
                 const std::string& content) {
    Document d;
    d.url = url;
    d.title = title;
    d.content = content;
    d.source = SourceType::ClearWeb;
    return d;
}

} // namespace

TEST(index_add_and_postings) {
    InvertedIndex index;
    const std::uint64_t id = index.addDocument(makeDoc("http://a.example/", "Alpha Page",
                                                       "alpha beta alpha"));
    CHECK_EQ(id, 1u);
    CHECK_EQ(index.documentCount(), 1u);

    const std::vector<Posting>* p = index.postings("alpha");
    CHECK(p != nullptr);
    CHECK_EQ(p->size(), 1u);
    CHECK_EQ((*p)[0].docId, id);
    // 2 body occurrences + 1 title occurrence weighted 3x = 5.
    CHECK_EQ((*p)[0].termFreq, 5u);

    const std::vector<Posting>* beta = index.postings("beta");
    CHECK(beta != nullptr);
    CHECK_EQ((*beta)[0].termFreq, 1u);
    CHECK(index.postings("gamma") == nullptr);
}

TEST(index_title_weight_three_x) {
    InvertedIndex index;
    index.addDocument(makeDoc("http://a.example/", "hello hello", ""));
    const std::vector<Posting>* p = index.postings("hello");
    CHECK(p != nullptr);
    CHECK_EQ((*p)[0].termFreq, 6u);  // 2 occurrences * 3x title weight
}

TEST(index_replace_by_url) {
    InvertedIndex index;
    index.addDocument(makeDoc("http://a.example/", "old title", "old body"));
    CHECK_EQ(index.documentCount(), 1u);
    CHECK(index.postings("old") != nullptr);

    index.addDocument(makeDoc("http://a.example/", "new title", "new body"));
    CHECK_EQ(index.documentCount(), 1u);
    CHECK(index.postings("old") == nullptr);
    CHECK(index.postings("new") != nullptr);
}

TEST(index_remove_document) {
    InvertedIndex index;
    const std::uint64_t id = index.addDocument(makeDoc("http://a.example/", "alpha", "beta"));
    index.addDocument(makeDoc("http://b.example/", "gamma", "delta"));
    CHECK_EQ(index.documentCount(), 2u);
    index.removeDocument(id);
    CHECK_EQ(index.documentCount(), 1u);
    CHECK(index.postings("alpha") == nullptr);
    CHECK(index.postings("gamma") != nullptr);
}

TEST(index_statistics) {
    InvertedIndex index;
    // Title-only doc: 1 token * 3 = length 3.
    index.addDocument(makeDoc("http://a.example/", "hello", ""));
    // Body-only doc: 1 token = length 1.
    index.addDocument(makeDoc("http://b.example/", "", "world"));
    CHECK_EQ(index.documentCount(), 2u);
    CHECK_EQ(index.termCount(), 2u);
    CHECK_EQ(index.docLength(1), 3u);
    CHECK_EQ(index.docLength(2), 1u);
    CHECK_EQ(index.averageDocLength(), 2.0);
}

TEST(index_all_documents) {
    InvertedIndex index;
    index.addDocument(makeDoc("http://a.example/", "one", ""));
    index.addDocument(makeDoc("http://b.example/", "two", ""));
    const auto docs = index.allDocuments();
    CHECK_EQ(docs.size(), 2u);
}
