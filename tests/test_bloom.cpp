// SPDX-License-Identifier: MIT
// Shadow SE - Bloom filter tests.
#include "shadowse/bloom_filter.hpp"

#include "test_framework.hpp"

#include <string>

using shadowse::BloomFilter;

TEST(bloom_basic_membership) {
    BloomFilter f(100);
    CHECK(!f.maybeContains("http://a.example/"));
    f.insert("http://a.example/");
    CHECK(f.maybeContains("http://a.example/"));
    f.insert("http://b.example/");
    CHECK(f.maybeContains("http://b.example/"));
    CHECK(f.maybeContains("http://a.example/"));  // still present
}

TEST(bloom_no_false_negatives_for_inserted) {
    BloomFilter f(50);
    for (int i = 0; i < 50; ++i) {
        f.insert("url-" + std::to_string(i));
    }
    for (int i = 0; i < 50; ++i) {
        CHECK(f.maybeContains("url-" + std::to_string(i)));  // never miss
    }
}

TEST(bloom_low_false_positive_rate) {
    BloomFilter f(100, 0.01);
    for (int i = 0; i < 100; ++i) {
        f.insert("k" + std::to_string(i));
    }
    // Most unrelated keys must NOT be reported as present.
    int falsePositives = 0;
    for (int i = 0; i < 1000; ++i) {
        if (f.maybeContains("unrelated" + std::to_string(i))) {
            ++falsePositives;
        }
    }
    CHECK(falsePositives < 100);  // ~1% of 1000 = ~10
}

TEST(bloom_clear) {
    BloomFilter f(100);
    f.insert("http://x.example/");
    CHECK(f.maybeContains("http://x.example/"));
    f.clear();
    CHECK(!f.maybeContains("http://x.example/"));
}

TEST(bloom_geometry) {
    BloomFilter f(1000);
    CHECK(f.bitCount() > 0);
    CHECK(f.hashCount() >= 1);
}
