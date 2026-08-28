// SPDX-License-Identifier: MIT
// Shadow SE - BM25 ranking tests.
#include "shadowse/bm25.hpp"
#include "shadowse/inverted_index.hpp"
#include "shadowse/tokenizer.hpp"

#include "test_framework.hpp"

using shadowse::BM25Ranker;
using shadowse::Document;
using shadowse::InvertedIndex;
using shadowse::ScoredDocument;
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

std::vector<ScoredDocument> search(InvertedIndex& index, const std::string& query,
                                   std::size_t limit = 10) {
    return BM25Ranker().rank(index, shadowse::tokenize(query), limit);
}

} // namespace

TEST(bm25_non_matching_documents_are_excluded) {
    InvertedIndex index;
    index.addDocument(makeDoc("http://a.example/", "fox", "the fox jumped"));
    index.addDocument(makeDoc("http://b.example/", "dog", "the dog barked"));
    const auto results = search(index, "fox");
    CHECK_EQ(results.size(), 1u);
    CHECK_EQ(results[0].doc.url, "http://a.example/");
    CHECK(results[0].score > 0.0);
}

TEST(bm25_title_weight_ranks_higher) {
    InvertedIndex index;
    index.addDocument(makeDoc("http://title.example/", "fox", ""));   // tf=3
    index.addDocument(makeDoc("http://body.example/", "", "fox"));    // tf=1
    const auto results = search(index, "fox");
    CHECK_EQ(results.size(), 2u);
    CHECK_EQ(results[0].doc.url, "http://title.example/");
    CHECK(results[0].score > results[1].score);
}

TEST(bm25_empty_index_and_query) {
    InvertedIndex index;
    CHECK(search(index, "").empty());
    index.addDocument(makeDoc("http://a.example/", "alpha", ""));
    CHECK(search(index, "zzzz-no-such-term").empty());
}

TEST(bm25_multi_term_or_semantics) {
    InvertedIndex index;
    index.addDocument(makeDoc("http://a.example/", "alpha", "alpha body"));
    index.addDocument(makeDoc("http://b.example/", "beta", "beta body"));
    index.addDocument(makeDoc("http://c.example/", "gamma", "gamma body"));
    const auto results = search(index, "alpha beta");
    // alpha (tf=3+1) should beat beta (tf=3+1): tie on tf, so doc id order wins.
    CHECK_EQ(results.size(), 2u);
    CHECK_EQ(results[0].doc.url, "http://a.example/");
    CHECK_EQ(results[1].doc.url, "http://b.example/");
}

TEST(bm25_limit_is_respected) {
    InvertedIndex index;
    for (int i = 0; i < 5; ++i) {
        index.addDocument(makeDoc("http://d" + std::to_string(i) + ".example/", "term", ""));
    }
    const auto results = search(index, "term", 3);
    CHECK_EQ(results.size(), 3u);
}
