// SPDX-License-Identifier: MIT
// Shadow SE - tokenizer tests.
#include "shadowse/tokenizer.hpp"

#include "test_framework.hpp"

#include <string>
#include <vector>

using shadowse::tokenize;

TEST(tokenizer_basic_ascii) {
    const std::vector<std::string> t = tokenize("Hello, World!");
    CHECK_EQ(t.size(), 2u);
    CHECK_EQ(t[0], "hello");
    CHECK_EQ(t[1], "world");
}

TEST(tokenizer_punctuation_and_digits) {
    const std::vector<std::string> t = tokenize("C++20 std::format v3.1");
    CHECK_EQ(t.size(), 6u);
    CHECK_EQ(t[0], "c");
    CHECK_EQ(t[1], "20");
    CHECK_EQ(t[2], "std");
    CHECK_EQ(t[3], "format");
    CHECK_EQ(t[4], "v3");
    CHECK_EQ(t[5], "1");  // '.' separates, so "1" is its own token
}

TEST(tokenizer_url_splits) {
    const std::vector<std::string> t =
        tokenize("http://shadow77ivq2cc3x.onion/index/vault");
    CHECK_EQ(t.size(), 5u);
    CHECK_EQ(t[0], "http");
    CHECK_EQ(t[1], "shadow77ivq2cc3x");
    CHECK_EQ(t[2], "onion");
    CHECK_EQ(t[3], "index");
    CHECK_EQ(t[4], "vault");
}

TEST(tokenizer_unicode_latin) {
    const std::vector<std::string> t = tokenize("Résumé café");
    CHECK_EQ(t.size(), 2u);
    CHECK_EQ(t[0], "résumé");  // 'R' folded to 'r'
    CHECK_EQ(t[1], "café");
}

TEST(tokenizer_cjk_single_token) {
    const std::vector<std::string> t = tokenize("搜索引擎 索引");
    CHECK_EQ(t.size(), 2u);
    CHECK_EQ(t[0], "搜索引擎");
    CHECK_EQ(t[1], "索引");
}

TEST(tokenizer_underscore_is_separator) {
    const std::vector<std::string> t = tokenize("foo_bar");
    CHECK_EQ(t.size(), 2u);
    CHECK_EQ(t[0], "foo");
    CHECK_EQ(t[1], "bar");
}

TEST(tokenizer_empty_and_whitespace) {
    CHECK(tokenize("").empty());
    CHECK(tokenize("   \t\n  ").empty());
}

TEST(tokenizer_case_folding) {
    CHECK_EQ(tokenize("ShAdOw Se").size(), 2u);
    CHECK_EQ(tokenize("ShAdOw Se")[0], "shadow");
    CHECK_EQ(tokenize("ShAdOw Se")[1], "se");
}

TEST(tokenizer_malformed_utf8_tolerated) {
    const std::string bad = "abc\xFF\xFE def";
    const std::vector<std::string> t = tokenize(bad);
    CHECK_EQ(t.size(), 2u);
    CHECK_EQ(t[0], "abc");
    CHECK_EQ(t[1], "def");
}
