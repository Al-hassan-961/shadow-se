// SPDX-License-Identifier: MIT
// Shadow SE - BM25 ranking model over the inverted index.
#pragma once

#include "shadowse/document.hpp"
#include "shadowse/inverted_index.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace shadowse {

struct ScoredDocument {
    Document doc;
    double score = 0.0;
};

// Classic Robertson-Walker BM25 with OR query semantics:
//   idf  = ln(1 + (N - n + 0.5) / (n + 0.5))
//   tf   = f * (k1 + 1) / (f + k1 * (1 - b + b * dl / avgdl))
class BM25Ranker {
public:
    explicit BM25Ranker(double k1 = 1.2, double b = 0.75);

    // Ranks documents against already-tokenized query terms.
    // Only documents matching at least one term are returned, sorted by
    // descending score (ties broken by ascending document id).
    std::vector<ScoredDocument> rank(const InvertedIndex& index,
                                     const std::vector<std::string>& queryTokens,
                                     std::size_t limit = 10) const;

private:
    double k1_;
    double b_;
};

} // namespace shadowse
