// SPDX-License-Identifier: MIT
// Shadow SE - authenticated encryption (XChaCha20-Poly1305) + Argon2id KDF.
//
// Why this instead of a custom "stronger-than-AES" cipher?
//   AES-256 already provides ~256-bit security and is the state of the art.
//   "Stronger" is not a meaningful property beyond that; hand-rolled ciphers
//   are a well-known source of catastrophic vulnerabilities. The real upgrade
//   is *authenticated* encryption with a modern 256-bit cipher (IND-CCA
//   secure, tamper-evident) and a memory-hard key derivation function
//   (Argon2id). This module provides exactly that via libsodium.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace shadowse {

using ByteVec = std::vector<std::uint8_t>;

constexpr std::size_t kSaltSize = 16;   // Argon2id salt bytes
constexpr std::size_t kNonceSize = 24;  // XChaCha20-Poly1305 nonce bytes
constexpr std::size_t kKeySize = 32;    // 256-bit key

struct EncryptedBlob {
    ByteVec nonce;       // 24-byte random nonce
    ByteVec ciphertext;  // sealed ciphertext including the 16-byte auth tag
};

// Authenticated encryption box backed by libsodium.
class CryptoBox {
public:
    CryptoBox() = default;
    ~CryptoBox();
    CryptoBox(const CryptoBox&) = delete;
    CryptoBox& operator=(const CryptoBox&) = delete;
    CryptoBox(CryptoBox&&) noexcept;
    CryptoBox& operator=(CryptoBox&&) noexcept;

    bool initialized() const { return !key_.empty(); }

    // Derives a 256-bit key from a passphrase via Argon2id (moderate cost).
    // Returns an uninitialized box on failure (e.g. out of memory).
    static CryptoBox deriveFromPassword(const std::string& password, const ByteVec& salt);

    // Generates a fresh random salt, derives a key, and returns the salt too.
    static CryptoBox deriveFromPassword(const std::string& password, ByteVec* salt_out);

    // Encrypts plaintext with a freshly generated random nonce.
    EncryptedBlob encrypt(std::string_view plaintext) const;

    // Decrypts and authenticates. Returns false if the ciphertext was tampered
    // with or the key is wrong (constant-time authenticated failure).
    bool decrypt(const EncryptedBlob& blob, std::string* plaintext) const;

private:
    explicit CryptoBox(ByteVec key);
    ByteVec key_;
};

// Secure random bytes from the operating system CSPRNG.
ByteVec randomBytes(std::size_t n);

// Base64 (URL-safe, unpadded) helpers for storing keys/salts as text.
std::string toBase64(const std::uint8_t* data, std::size_t len);
ByteVec fromBase64(const std::string& b64);

} // namespace shadowse
