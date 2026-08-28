// SPDX-License-Identifier: MIT
// Shadow SE - UTF-8 aware tokenizer with case folding.
#include "shadowse/tokenizer.hpp"

#include <cstddef>

namespace shadowse {

char32_t decodeUtf8(const std::string& text, std::size_t& offset) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t len = text.size();
    if (offset >= len) {
        return 0xFFFFFFFF;
    }
    const unsigned char c0 = s[offset];
    if (c0 < 0x80) {  // ASCII fast path
        ++offset;
        return static_cast<char32_t>(c0);
    }
    std::size_t nbytes = 0;
    char32_t cp = 0;
    if ((c0 & 0xE0) == 0xC0) {
        nbytes = 2;
        cp = c0 & 0x1F;
    } else if ((c0 & 0xF0) == 0xE0) {
        nbytes = 3;
        cp = c0 & 0x0F;
    } else if ((c0 & 0xF8) == 0xF0) {
        nbytes = 4;
        cp = c0 & 0x07;
    } else {
        ++offset;  // invalid lead byte: treat as separator
        return 0xFFFFFFFF;
    }
    if (offset + nbytes > len) {
        ++offset;
        return 0xFFFFFFFF;  // truncated sequence
    }
    for (std::size_t i = 1; i < nbytes; ++i) {
        const unsigned char c = s[offset + i];
        if ((c & 0xC0) != 0x80) {
            ++offset;
            return 0xFFFFFFFF;  // bad continuation byte
        }
        cp = (cp << 6) | (c & 0x3F);
    }
    // Reject overlong encodings and surrogate halves.
    if ((nbytes == 2 && cp < 0x80) || (nbytes == 3 && cp < 0x800) ||
        (nbytes == 4 && cp < 0x10000) || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
        ++offset;
        return 0xFFFFFFFF;
    }
    offset += nbytes;
    return cp;
}

bool isTokenCharacter(char32_t cp) {
    if (cp == 0xFFFFFFFF) {
        return false;
    }
    if (cp < 0x80) {  // ASCII alphanumerics only
        return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9');
    }
    // Latin-1 Supplement (exclude multiplication/division signs).
    if (cp >= 0x00C0 && cp <= 0x00FF && cp != 0x00D7 && cp != 0x00F7) return true;
    // Latin Extended-A/B, IPA Extensions, Spacing Modifier Letters, Combining Diacriticals.
    if (cp >= 0x0100 && cp <= 0x02FF) return true;
    // Greek and Coptic.
    if ((cp >= 0x0370 && cp <= 0x03FF) || (cp >= 0x1F00 && cp <= 0x1FFF)) return true;
    // Cyrillic.
    if (cp >= 0x0400 && cp <= 0x04FF) return true;
    // Hebrew, Arabic, Devanagari, Thai, etc. (broad Indic/Middle-Eastern blocks).
    if ((cp >= 0x0590 && cp <= 0x05FF) || (cp >= 0x0600 && cp <= 0x06FF) ||
        (cp >= 0x0900 && cp <= 0x0DFF)) return true;
    // Hiragana, Katakana, CJK Unified + Extension A, Hangul.
    if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF)) return true;
    return false;
}

// Encodes a code point as UTF-8 and appends it to a token buffer.
void appendCodepoint(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

char32_t foldCase(char32_t cp) {
    if (cp >= 'A' && cp <= 'Z') {
        return static_cast<char32_t>(cp + 0x20);
    }
    if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) {
        return static_cast<char32_t>(cp + 0x20);  // À..Þ -> à..þ
    }
    if (cp >= 0x0100 && cp <= 0x017F && (cp & 1) == 0) {
        return static_cast<char32_t>(cp + 1);  // Latin Extended-A uppercase pairs
    }
    if (cp >= 0x0180 && cp <= 0x024F && (cp & 1) == 0 && cp != 0x01C4 && cp != 0x01C5 && cp != 0x01C6 &&
        cp != 0x01C7 && cp != 0x01C8 && cp != 0x01C9 && cp != 0x01CA && cp != 0x01CB && cp != 0x01CC) {
        return static_cast<char32_t>(cp + 1);  // rough Latin Extended-B pairs
    }
    if (cp >= 0x0391 && cp <= 0x03A9 && cp != 0x03A2) {
        return static_cast<char32_t>(cp + 0x20);  // Greek capitals
    }
    if (cp >= 0x0410 && cp <= 0x042F) {
        return static_cast<char32_t>(cp + 0x20);  // Cyrillic capitals
    }
    return cp;
}

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    std::size_t offset = 0;
    const std::size_t len = text.size();

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };

    while (offset < len) {
        const char32_t cp = decodeUtf8(text, offset);
        if (isTokenCharacter(cp)) {
            appendCodepoint(current, foldCase(cp));
        } else {
            flush();
        }
    }
    flush();
    return tokens;
}

} // namespace shadowse
