// SPDX-License-Identifier: MIT
// Shadow SE - BM25 mathematical edge-case tests.
#include "shadowse/bm25.hpp"
#include "shadowse/inverted_index.hpp"
#include "shadowse/tokenizer.hpp"

#include "test_framework.hpp"

#include <cmath>
#include <string>

using shadowse::BM25Ranker;
using shadowse::Document;
using shadowse::InvertedIndex;
using shadowse::ScoredDocument;
using shadowse::SourceType;
using shadowse::tokenize;

namespace {

Document makeDoc(const std::string& url, const std::string& content) {
    Document d;
    d.url = url;
    d.content = content;
    d.source = SourceType::ClearWeb;
    return d;
}

std::vector<ScoredDocument> rankWith(InvertedIndex& index, const std::string& q,
                                     double k1 = 1.2, double b = 0.75) {
    return BM25Ranker(k1, b).rank(index, tokenize(q), 10);
}

} // namespace

TEST(bm25_empty_index_returns_empty) {
    InvertedIndex index;
    CHECK(rankWith(index, "anything").empty());
}

TEST(bm25_empty_query_returns_empty) {
    InvertedIndex index;
    index.addDocument(makeDoc("http://a.example/", "some content words here"));
    CHECK(rankWith(index, "").empty());
    CHECK(rankWith(index, "   ").empty());
}

TEST(bm25_term_in_every_document_small_but_finite_idf) {
    InvertedIndex index;
    index.addDocument(makeDoc("http://a.example/", "common common"));
    index.addDocument(makeDoc("http://b.example/", "common common"));
    const auto results = rankWith(index, "common");
    CHECK_EQ(results.size(), 2u);
    for (const auto& r : results) {
        CHECK(r.score > 0.0);                        // idf = ln(1 + 0.5/(N+0.5)) > 0
        CHECK(std::isfinite(r.score));
        CHECK(r.score < 1.0);                        // but small for a ubiquitous term
    }
}

TEST(bm25_rare_term_scores_higher_than_common_term) {
    InvertedIndex index;
    index.addDocument(makeDoc("http://a.example/", "common common common rare"));
    index.addDocument(makeDoc("http://b.example/", "common common common common common"));
    index.addDocument(makeDoc("http://c.example/", "common"));
    const auto rare = rankWith(index, "rare");
    CHECK(!rare.empty());
    CHECK_EQ(rare[0].doc.url, "http://a.example/");
    // The single 'rare' hit (high idf) must outweigh the many 'common' hits.
    const auto common = rankWith(index, "common");
    CHECK(!common.empty());
    CHECK(rare[0].score > common[0].score);
}

TEST(bm25_zero_length_document_does_not_break) {
    InvertedIndex index;
    // '!!!' tokenizes to nothing (empty document length).
    index.addDocument(makeDoc("http://a.example/", "!!!"));
    index.addDocument(makeDoc("http://b.example/", "real content words here"));
    const auto results = rankWith(index, "real");
    CHECK(!results.empty());
    CHECK(std::isfinite(results[0].score));
}

TEST(bm25_k1_zero_degenerates_to_binary_weight) {
    InvertedIndex index;
    // Two docs, one with the term twice, one once. With k1=0, tf = f/(f) = 1,
    // so both contribute equally and the score depends only on idf (equal).
    index.addDocument(makeDoc("http://a.example/", "term term"));
    index.addDocument(makeDoc("http://b.example/", "term"));
    const auto results = rankWith(index, "term", 0.0 /*k1*/, 0.0 /*b*/);
    CHECK_EQ(results.size(), 2u);
    // With k1=0 the per-doc tf factor is 1 for any f -> equal scores.
    CHECK(std::fabs(results[0].score - results[1].score) < 1e-9);
}

TEST(bm25_scores_stay_finite_and_nonnegative) {
    InvertedIndex index;
    for (int i = 0; i < 50; ++i) {
        index.addDocument(makeDoc("http://d" + std::to_string(i) + ".example/",
                                  "shared " + std::to_string(i)));
    }
    const auto results = rankWith(index, "shared 7 42");
    for (const auto& r : results) {
        CHECK(std::isfinite(r.score));
        CHECK(r.score >= 0.0);
    }
    CHECK(results.size() >= 2u);
}

TEST(bm25_results_sorted_descending) {
    InvertedIndex index;
    index.addDocument(makeDoc("http://a.example/", "cat"));
    index.addDocument(makeDoc("http://b.example/", "cat cat cat"));
    index.addDocument(makeDoc("http://c.example/", "cat cat"));
    const auto results = rankWith(index, "cat");
    CHECK_EQ(results.size(), 3u);
    for (std::size_t i = 1; i < results.size(); ++i) {
        CHECK(results[i - 1].score >= results[i].score);
    }
    CHECK_EQ(results[0].doc.url, "http://b.example/");  // highest term frequency
}
