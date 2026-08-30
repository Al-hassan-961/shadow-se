// SPDX-License-Identifier: MIT
// Shadow SE - compact Bloom filter for duplicate URL pre-filtering.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace shadowse {

// Space-efficient probabilistic membership filter. `maybeContains` never
// returns a false negative but may return false positives (rate is chosen at
// construction). Used by the crawler as a cheap duplicate-URL pre-filter; an
// exact set still confirms candidates that the filter flags.
class BloomFilter {
public:
    BloomFilter(std::size_t expectedItems, double falsePositiveRate = 0.01);

    void insert(const std::string& key);
    bool maybeContains(const std::string& key) const;
    void clear();

    std::size_t bitCount() const { return m_; }
    std::size_t hashCount() const { return k_; }

private:
    static std::uint64_t fnv1a(const std::string& s);

    std::vector<std::uint64_t> bits_;
    std::size_t m_ = 0;  // number of bits
    std::size_t k_ = 0;  // number of hash functions
};

} // namespace shadowse
