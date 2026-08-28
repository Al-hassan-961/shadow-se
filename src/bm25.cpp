// SPDX-License-Identifier: MIT
// Shadow SE - BM25 ranking model over the inverted index.
#include "shadowse/bm25.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace shadowse {

BM25Ranker::BM25Ranker(double k1, double b) : k1_(k1), b_(b) {}

std::vector<ScoredDocument> BM25Ranker::rank(const InvertedIndex& index,
                                             const std::vector<std::string>& queryTokens,
                                             std::size_t limit) const {
    std::vector<ScoredDocument> results;
    if (queryTokens.empty()) {
        return results;
    }

    const double N = static_cast<double>(index.documentCount());
    const double avgdl = index.averageDocLength();
    if (N == 0.0 || avgdl <= 0.0) {
        return results;
    }

    // Deduplicate query terms (each term contributes at most once).
    std::vector<std::string> terms = queryTokens;
    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());

    std::unordered_map<std::uint64_t, double> accum;

    for (const std::string& term : terms) {
        const std::vector<Posting>* postings = index.postings(term);
        if (postings == nullptr) {
            continue;
        }
        const double n = static_cast<double>(postings->size());
        const double idf = std::log(1.0 + (N - n + 0.5) / (n + 0.5));
        for (const Posting& p : *postings) {
            const double dl = static_cast<double>(index.docLength(p.docId));
            const double denom = p.termFreq + k1_ * (1.0 - b_ + b_ * dl / avgdl);
            const double tf = static_cast<double>(p.termFreq) * (k1_ + 1.0) / denom;
            accum[p.docId] += idf * tf;
        }
    }

    results.reserve(accum.size());
    for (const auto& [docId, score] : accum) {
        const Document* doc = index.document(docId);
        if (doc == nullptr) {
            continue;
        }
        results.push_back(ScoredDocument{*doc, score});
    }

    std::sort(results.begin(), results.end(), [](const ScoredDocument& a, const ScoredDocument& b) {
        if (a.score != b.score) {
            return a.score > b.score;  // descending score
        }
        return a.doc.id < b.doc.id;    // stable tie-break
    });

    if (results.size() > limit) {
        results.resize(limit);
    }
    return results;
}

} // namespace shadowse
