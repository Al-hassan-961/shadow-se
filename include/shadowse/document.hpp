// SPDX-License-Identifier: MIT
// Shadow SE - document model shared across the engine.
#pragma once

#include <cstdint>
#include <string>

namespace shadowse {

// Origin of a document: the clear web or a Tor hidden service (.onion).
enum class SourceType { ClearWeb = 0, Onion = 1 };

struct Document {
    std::uint64_t id = 0;      // assigned by the index on ingestion
    std::string url;           // canonical URL
    std::string title;         // <title> / heading
    std::string snippet;       // meta description or first bytes of text
    std::string content;       // full extracted page text (may be empty)
    SourceType source = SourceType::ClearWeb;
    std::uint32_t depth = 0;   // crawl depth (0 = seeded)

    bool isOnion() const { return source == SourceType::Onion; }

    // All text that participates in indexing (title carries extra weight).
    std::string indexableText() const {
        std::string out;
        out.reserve(title.size() + snippet.size() + content.size() + 2);
        out += title;
        out.push_back('\n');
        out += snippet;
        out.push_back('\n');
        out += content;
        return out;
    }
};

} // namespace shadowse
