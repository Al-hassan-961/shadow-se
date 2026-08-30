// SPDX-License-Identifier: MIT
// Shadow SE - encrypted index snapshot (serialize documents -> AEAD at rest).
#pragma once

#include "shadowse/crypto.hpp"
#include "shadowse/document.hpp"

#include <string>
#include <vector>

namespace shadowse {

// Binary serialization of a document set (versioned, length-prefixed).
std::string serializeDocuments(const std::vector<Document>& docs);

// Parses serializeDocuments output; returns false with `err` set on bad data.
bool deserializeDocuments(const std::string& data, std::vector<Document>* out,
                          std::string* err);

// Encrypts `docs` with XChaCha20-Poly1305 (key derived from password via
// Argon2id) and writes salt|nonce|ciphertext to `path`.
bool saveEncryptedSnapshot(const std::string& path, const std::string& password,
                           const std::vector<Document>& docs, std::string* err);

// Reads and authenticates an encrypted snapshot, returning the documents.
// Fails if the password is wrong or the file was tampered with.
bool loadEncryptedSnapshot(const std::string& path, const std::string& password,
                           std::vector<Document>* docs, std::string* err);

} // namespace shadowse
