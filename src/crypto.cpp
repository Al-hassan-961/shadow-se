// SPDX-License-Identifier: MIT
// Shadow SE - authenticated encryption (XChaCha20-Poly1305) + Argon2id KDF.
#include "shadowse/crypto.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

#ifdef HAVE_SODIUM
#include <sodium.h>
#endif

namespace shadowse {

// ---------------------------------------------------------------------------
// Secure random + base64 (libsodium when available; portable fallback below).
// ---------------------------------------------------------------------------

ByteVec randomBytes(std::size_t n) {
    ByteVec out(n);
    if (n == 0) {
        return out;
    }
#ifdef HAVE_SODIUM
    if (sodium_init() < 0) {
        return {};
    }
    randombytes_buf(out.data(), out.size());
#else
    // Portable fallback - NOT cryptographically secure. Present only so the
    // code builds without libsodium; production builds require libsodium.
    std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
    for (std::size_t i = 0; i < n; ++i) {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        out[i] = static_cast<std::uint8_t>(seed >> (8 * (i % 8)));
    }
#endif
    return out;
}

static const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string toBase64(const std::uint8_t* data, std::size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= len) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(kBase64Alphabet[(v >> 18) & 63]);
        out.push_back(kBase64Alphabet[(v >> 12) & 63]);
        out.push_back(kBase64Alphabet[(v >> 6) & 63]);
        out.push_back(kBase64Alphabet[v & 63]);
        i += 3;
    }
    const std::size_t rem = len - i;
    if (rem == 1) {
        const std::uint32_t v = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kBase64Alphabet[(v >> 18) & 63]);
        out.push_back(kBase64Alphabet[(v >> 12) & 63]);
    } else if (rem == 2) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kBase64Alphabet[(v >> 18) & 63]);
        out.push_back(kBase64Alphabet[(v >> 12) & 63]);
        out.push_back(kBase64Alphabet[(v >> 6) & 63]);
    }
    return out;
}

namespace {
std::int8_t base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<std::int8_t>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<std::int8_t>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<std::int8_t>(c - '0' + 52);
    if (c == '-' || c == '+') return 62;
    if (c == '_' || c == '/') return 63;
    return -1;
}
} // namespace

ByteVec fromBase64(const std::string& b64) {
    ByteVec out;
    out.reserve(b64.size() / 4 * 3);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char c : b64) {
        if (c == '=') {
            break;
        }
        const std::int8_t v = base64Value(c);
        if (v < 0) {
            continue;  // skip whitespace/newlines
        }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// CryptoBox
// ---------------------------------------------------------------------------

CryptoBox::CryptoBox(ByteVec key) : key_(std::move(key)) {}

CryptoBox::~CryptoBox() {
#ifdef HAVE_SODIUM
    if (!key_.empty()) {
        sodium_memzero(key_.data(), key_.size());
    }
#endif
    key_.clear();
    key_.shrink_to_fit();
}

CryptoBox::CryptoBox(CryptoBox&& other) noexcept : key_(std::move(other.key_)) {}

CryptoBox& CryptoBox::operator=(CryptoBox&& other) noexcept {
    if (this != &other) {
        key_ = std::move(other.key_);
    }
    return *this;
}

CryptoBox CryptoBox::deriveFromPassword(const std::string& password, const ByteVec& salt) {
#ifdef HAVE_SODIUM
    if (sodium_init() < 0) {
        return CryptoBox{};
    }
    ByteVec key(kKeySize);
    const int rc = crypto_pwhash_argon2id(
        key.data(), key.size(), password.data(), password.size(), salt.data(),
        crypto_pwhash_argon2id_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_argon2id_MEMLIMIT_INTERACTIVE,
        crypto_pwhash_argon2id_ALG_ARGON2ID13);
    if (rc != 0) {
        return CryptoBox{};
    }
    return CryptoBox(std::move(key));
#else
    (void)password;
    (void)salt;
    return CryptoBox{};
#endif
}

CryptoBox CryptoBox::deriveFromPassword(const std::string& password, ByteVec* salt_out) {
    ByteVec salt = randomBytes(kSaltSize);
    if (salt.size() != kSaltSize) {
        return CryptoBox{};
    }
    CryptoBox box = deriveFromPassword(password, salt);
    if (box.initialized() && salt_out != nullptr) {
        *salt_out = std::move(salt);
    }
    return box;
}

EncryptedBlob CryptoBox::encrypt(std::string_view plaintext) const {
    EncryptedBlob blob;
    if (!initialized() || plaintext.empty()) {
        return blob;
    }
#ifdef HAVE_SODIUM
    blob.nonce = randomBytes(kNonceSize);
    if (blob.nonce.size() != kNonceSize) {
        return {};
    }
    blob.ciphertext.resize(plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long clen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        blob.ciphertext.data(), &clen,
        reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
        nullptr, 0,          // no additional authenticated data
        nullptr,             // no nonce prefix
        blob.nonce.data(), key_.data());
    blob.ciphertext.resize(clen);
#endif
    return blob;
}

bool CryptoBox::decrypt(const EncryptedBlob& blob, std::string* plaintext) const {
    if (!initialized() || plaintext == nullptr || blob.nonce.size() != kNonceSize) {
        return false;
    }
#ifdef HAVE_SODIUM
    if (blob.ciphertext.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        return false;
    }
    std::string out;
    out.resize(blob.ciphertext.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long mlen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char*>(out.data()), &mlen,
        nullptr,
        blob.ciphertext.data(), blob.ciphertext.size(),
        nullptr, 0,
        blob.nonce.data(), key_.data());
    if (rc != 0) {
        return false;  // authentication failed
    }
    out.resize(mlen);
    *plaintext = std::move(out);
    return true;
#else
    (void)blob;
    (void)plaintext;
    return false;
#endif
}

} // namespace shadowse
