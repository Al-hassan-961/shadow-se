// SPDX-License-Identifier: MIT
// Shadow SE - thread-safe in-memory inverted index with a document store.
#pragma once

#include "shadowse/document.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shadowse {

struct Posting {
    std::uint64_t docId;
    std::uint32_t termFreq;  // weighted term frequency within the document
};

class InvertedIndex {
public:
    InvertedIndex() = default;
    InvertedIndex(const InvertedIndex&) = delete;
    InvertedIndex& operator=(const InvertedIndex&) = delete;

    // Adds or replaces a document (replacement is keyed by URL).
    // Title tokens count 3x toward term frequency. Returns the document id.
    std::uint64_t addDocument(const Document& doc);

    // Removes a document by id. No-op when the id is unknown.
    void removeDocument(std::uint64_t docId);

    // Removes all documents and postings.
    void clear();

    // Atomically replaces the entire index contents (used when loading an
    // encrypted snapshot).
    void replaceAll(const std::vector<Document>& docs);

    // Postings list for a term, or nullptr when the term is not indexed.
    const std::vector<Posting>* postings(std::string_view term) const;

    const Document* document(std::uint64_t docId) const;

    std::uint64_t documentCount() const;
    std::size_t termCount() const;        // distinct indexed terms
    std::size_t docLength(std::uint64_t docId) const;
    double averageDocLength() const;      // mean weighted doc length

    std::vector<Document> allDocuments() const;

private:
    std::uint64_t addDocumentLocked(const Document& doc);
    void indexText(std::uint64_t docId, const std::string& text,
                   std::unordered_map<std::string, std::uint32_t>& freqs) const;
    void removePostings(std::uint64_t docId,
                        const std::vector<std::pair<std::string, std::uint32_t>>& terms);

    mutable std::mutex mtx_;
    std::unordered_map<std::string, std::vector<Posting>> postings_;
    std::unordered_map<std::string, std::uint64_t> urlToId_;
    std::unordered_map<std::uint64_t, Document> docs_;
    // Per-document term contributions, needed to un-index on replacement.
    std::unordered_map<std::uint64_t, std::vector<std::pair<std::string, std::uint32_t>>> docTerms_;
    std::unordered_map<std::uint64_t, std::size_t> docLengths_;
    std::uint64_t nextId_ = 1;
    std::uint64_t totalTerms_ = 0;  // sum of weighted lengths across documents
};

} // namespace shadowse
