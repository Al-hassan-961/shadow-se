// SPDX-License-Identifier: MIT
// Shadow SE - compact Bloom filter for duplicate URL pre-filtering.
#include "shadowse/bloom_filter.hpp"

#include <cmath>
#include <functional>

namespace shadowse {

namespace {
constexpr double kLn2 = 0.6931471805599453;
constexpr double kLn2Sq = 0.4804530139182014;
} // namespace

std::uint64_t BloomFilter::fnv1a(const std::string& s) {
    std::uint64_t hash = 1469598103934665603ULL;  // FNV-1a 64-bit offset basis
    for (unsigned char c : s) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

BloomFilter::BloomFilter(std::size_t expectedItems, double falsePositiveRate) {
    if (expectedItems == 0) {
        expectedItems = 1;
    }
    if (falsePositiveRate <= 0.0) falsePositiveRate = 0.0001;
    if (falsePositiveRate >= 1.0) falsePositiveRate = 0.5;
    // m = -n * ln(p) / (ln2)^2 ; k = m/n * ln2
    const double mBits = std::ceil(-static_cast<double>(expectedItems) * std::log(falsePositiveRate) /
                                   kLn2Sq);
    m_ = static_cast<std::size_t>(mBits);
    if (m_ == 0) m_ = 1;
    const double k = kLn2 * static_cast<double>(m_) / static_cast<double>(expectedItems);
    k_ = static_cast<std::size_t>(std::max(1.0, std::round(k)));
    bits_.assign((m_ + 63) / 64, 0);
}

void BloomFilter::insert(const std::string& key) {
    const std::uint64_t h1 = std::hash<std::string>{}(key);
    std::uint64_t h2 = fnv1a(key);
    if (h2 == 0) h2 = 1;
    for (std::size_t i = 0; i < k_; ++i) {
        const std::size_t bit = static_cast<std::size_t>((h1 + static_cast<std::uint64_t>(i) * h2) % m_);
        bits_[bit / 64] |= (std::uint64_t{1} << (bit % 64));
    }
}

bool BloomFilter::maybeContains(const std::string& key) const {
    const std::uint64_t h1 = std::hash<std::string>{}(key);
    std::uint64_t h2 = fnv1a(key);
    if (h2 == 0) h2 = 1;
    for (std::size_t i = 0; i < k_; ++i) {
        const std::size_t bit = static_cast<std::size_t>((h1 + static_cast<std::uint64_t>(i) * h2) % m_);
        if ((bits_[bit / 64] & (std::uint64_t{1} << (bit % 64))) == 0) {
            return false;  // definitely not present
        }
    }
    return true;  // probably present
}

void BloomFilter::clear() {
    for (auto& b : bits_) b = 0;
}

} // namespace shadowse
