// SPDX-License-Identifier: MIT
// Shadow SE - UTF-8 aware tokenizer with case folding.
#pragma once

#include <string>
#include <vector>

namespace shadowse {

// Splits UTF-8 text into lowercased alphanumeric tokens.
// Token characters cover ASCII, Latin-1/Latin Extended, Greek, Cyrillic and CJK.
// Every other byte (spaces, punctuation, emoji, malformed UTF-8) is a separator.
std::vector<std::string> tokenize(const std::string& text);

// Decodes a single UTF-8 code point starting at `text[offset]`.
// Returns the code point and advances `offset` past its bytes (1..4).
// Returns 0xFFFFFFFF for invalid leading bytes (advanced by 1).
char32_t decodeUtf8(const std::string& text, std::size_t& offset);

// Unicode-aware case folding for the ranges the tokenizer accepts.
// CJK and most scripts are returned unchanged (they have no case).
char32_t foldCase(char32_t cp);

// True when `cp` is treated as part of a token.
bool isTokenCharacter(char32_t cp);

} // namespace shadowse
