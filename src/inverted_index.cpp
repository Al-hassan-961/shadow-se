// SPDX-License-Identifier: MIT
// Shadow SE - thread-safe in-memory inverted index with a document store.
#include "shadowse/inverted_index.hpp"

#include "shadowse/tokenizer.hpp"

#include <algorithm>
#include <cstdint>

namespace shadowse {

void InvertedIndex::indexText(std::uint64_t docId, const std::string& text,
                              std::unordered_map<std::string, std::uint32_t>& freqs) const {
    (void)docId;
    for (const std::string& tok : tokenize(text)) {
        ++freqs[tok];
    }
}

void InvertedIndex::removePostings(
    std::uint64_t docId, const std::vector<std::pair<std::string, std::uint32_t>>& terms) {
    for (const auto& [term, freq] : terms) {
        auto it = postings_.find(term);
        if (it == postings_.end()) {
            continue;
        }
        auto& list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [docId](const Posting& p) { return p.docId == docId; }),
                   list.end());
        if (list.empty()) {
            postings_.erase(it);
        }
    }
}

std::uint64_t InvertedIndex::addDocument(const Document& doc) {
    std::lock_guard<std::mutex> lock(mtx_);

    std::uint64_t id = 0;
    const auto urlIt = urlToId_.find(doc.url);
    if (urlIt != urlToId_.end()) {
        id = urlIt->second;
        removePostings(id, docTerms_[id]);
        totalTerms_ -= docLengths_[id];
        docLengths_.erase(id);
        docTerms_.erase(id);
    } else {
        id = nextId_++;
        urlToId_.emplace(doc.url, id);
    }

    // Weighted term frequency extraction: title 3x, snippet/content 1x.
    std::unordered_map<std::string, std::uint32_t> freqs;
    indexText(id, doc.title, freqs);
    std::unordered_map<std::string, std::uint32_t> titleFreqs;
    indexText(id, doc.title, titleFreqs);
    std::unordered_map<std::string, std::uint32_t> body;
    indexText(id, doc.snippet, body);
    indexText(id, doc.content, body);
    for (const auto& [term, freq] : body) {
        freqs[term] += freq;
    }
    for (const auto& [term, freq] : titleFreqs) {
        freqs[term] += 2 * freq;  // title tokens: 1 (base) + 2 (bonus) = 3x
    }
    for (const auto& [term, freq] : freqs) {
        postings_[term].push_back(Posting{id, freq});
        docTerms_[id].emplace_back(term, freq);
        totalTerms_ += freq;
    }

    docLengths_[id] = 0;
    for (const auto& [term, freq] : freqs) {
        docLengths_[id] += freq;
    }

    Document stored = doc;
    stored.id = id;
    docs_[id] = std::move(stored);
    return id;
}

void InvertedIndex::removeDocument(std::uint64_t docId) {
    std::lock_guard<std::mutex> lock(mtx_);
    const auto docIt = docs_.find(docId);
    if (docIt == docs_.end()) {
        return;
    }
    removePostings(docId, docTerms_[docId]);
    totalTerms_ -= docLengths_[docId];
    urlToId_.erase(docIt->second.url);
    docLengths_.erase(docId);
    docTerms_.erase(docId);
    docs_.erase(docIt);
}

const std::vector<Posting>* InvertedIndex::postings(std::string_view term) const {
    std::lock_guard<std::mutex> lock(mtx_);
    const auto it = postings_.find(std::string(term));
    return it == postings_.end() ? nullptr : &it->second;
}

const Document* InvertedIndex::document(std::uint64_t docId) const {
    std::lock_guard<std::mutex> lock(mtx_);
    const auto it = docs_.find(docId);
    return it == docs_.end() ? nullptr : &it->second;
}

std::uint64_t InvertedIndex::documentCount() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return static_cast<std::uint64_t>(docs_.size());
}

std::size_t InvertedIndex::termCount() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return postings_.size();
}

std::size_t InvertedIndex::docLength(std::uint64_t docId) const {
    std::lock_guard<std::mutex> lock(mtx_);
    const auto it = docLengths_.find(docId);
    return it == docLengths_.end() ? 0 : it->second;
}

double InvertedIndex::averageDocLength() const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (docs_.empty()) {
        return 0.0;
    }
    return static_cast<double>(totalTerms_) / static_cast<double>(docs_.size());
}

std::vector<Document> InvertedIndex::allDocuments() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<Document> out;
    out.reserve(docs_.size());
    for (const auto& [id, doc] : docs_) {
        (void)id;
        out.push_back(doc);
    }
    return out;
}

} // namespace shadowse
