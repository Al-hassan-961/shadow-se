// SPDX-License-Identifier: MIT
// Shadow SE - malformed UTF-8 handling tests.
#include "shadowse/tokenizer.hpp"

#include "test_framework.hpp"

#include <string>
#include <vector>

using shadowse::decodeUtf8;
using shadowse::tokenize;

// Builds a std::string from raw bytes.
static std::string bytes(std::initializer_list<unsigned char> bs) {
    return std::string(reinterpret_cast<const char*>(bs.begin()), bs.size());
}

static constexpr char32_t kInvalid = 0xFFFFFFFF;

TEST(utf8_valid_decoding) {
    std::size_t off = 0;
    CHECK(decodeUtf8("A", off) == static_cast<char32_t>('A'));  // ASCII
    off = 0;
    CHECK(decodeUtf8(bytes({0xC3, 0xA9}), off) == char32_t{0xE9});      // é (2-byte)
    off = 0;
    CHECK(decodeUtf8(bytes({0xE2, 0x82, 0xAC}), off) == char32_t{0x20AC});  // € (3-byte)
    off = 0;
    CHECK(decodeUtf8(bytes({0xF0, 0x9F, 0x98, 0x80}), off) == char32_t{0x1F600});  // emoji
}

TEST(utf8_truncated_sequence_is_invalid) {
    std::size_t off = 0;
    CHECK(decodeUtf8(bytes({0xC3}), off) == kInvalid);          // 2-byte, 1 left
    off = 0;
    CHECK(decodeUtf8(bytes({0xE2, 0x82}), off) == kInvalid);    // 3-byte, 2 left
    off = 0;
    CHECK(decodeUtf8(bytes({0xF0, 0x9F, 0x98}), off) == kInvalid);
}

TEST(utf8_overlong_encoding_is_invalid) {
    std::size_t off = 0;
    CHECK(decodeUtf8(bytes({0xC0, 0x80}), off) == kInvalid);       // overlong NUL
    off = 0;
    CHECK(decodeUtf8(bytes({0xE0, 0x80, 0x80}), off) == kInvalid); // overlong NUL
}

TEST(utf8_surrogates_and_out_of_range_rejected) {
    std::size_t off = 0;
    CHECK(decodeUtf8(bytes({0xED, 0xA0, 0x80}), off) == kInvalid);       // UTF-8 surrogate
    off = 0;
    CHECK(decodeUtf8(bytes({0xF4, 0x90, 0x80, 0x80}), off) == kInvalid);  // > U+10FFFF
    off = 0;
    CHECK(decodeUtf8(bytes({0xF5, 0x80, 0x80, 0x80}), off) == kInvalid);
}

TEST(utf8_bad_continuation_and_lone_continuation) {
    std::size_t off = 0;
    CHECK(decodeUtf8(bytes({0xC3, 0x41}), off) == kInvalid);  // bad continuation
    off = 0;
    CHECK(decodeUtf8(bytes({0x80}), off) == kInvalid);         // lone continuation byte
}

TEST(utf8_invalid_bytes_are_separators_not_crash) {
    // Overlong NUL (0xC0 0x80) + a lone continuation byte between abc and xyz.
    const std::string bad = "abc\xC0\x80\x80xyz";
    const std::vector<std::string> t = tokenize(bad);
    CHECK_EQ(t.size(), 2u);
    CHECK_EQ(t[0], "abc");
    CHECK_EQ(t[1], "xyz");
}

TEST(utf8_mixed_corruption_fuzz) {
    unsigned char seeds[][4] = {
        {0xFF, 0xC0, 0x00, 0x41}, {0x80, 0x81, 0x82, 0x20},
        {0xC2, 0x20, 0xE2, 0x82}, {0x00, 0x7F, 0x80, 0xFF},
        {0xE0, 0x80, 0xAF, 0x00},
    };
    for (auto& s : seeds) {
        std::string data(reinterpret_cast<const char*>(s), sizeof(s));
        (void)tokenize(data);  // must not throw / hang
    }
    CHECK(true);
}
