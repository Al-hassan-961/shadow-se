// SPDX-License-Identifier: MIT
// Shadow SE - authenticated encryption (CryptoBox) tests.
#include "shadowse/crypto.hpp"

#include "test_framework.hpp"

#include <string>

using shadowse::ByteVec;
using shadowse::CryptoBox;
using shadowse::EncryptedBlob;
using shadowse::kNonceSize;
using shadowse::kSaltSize;

TEST(crypto_roundtrip) {
    CryptoBox box;
    CHECK(!box.initialized());
    box = CryptoBox::deriveFromPassword("hunter2-secret", ByteVec(kSaltSize, 1));
    CHECK(box.initialized());

    const std::string msg = "classified telemetry index #42";
    const EncryptedBlob blob = box.encrypt(msg);
    CHECK_EQ(blob.nonce.size(), kNonceSize);
    CHECK(!blob.ciphertext.empty());
    CHECK(blob.ciphertext.size() > msg.size());  // includes 16-byte auth tag

    std::string out;
    CHECK(box.decrypt(blob, &out));
    CHECK_EQ(out, msg);
}

TEST(crypto_wrong_key_fails) {
    const ByteVec salt(kSaltSize, 7);
    const auto box = CryptoBox::deriveFromPassword("right-password", salt);
    const auto other = CryptoBox::deriveFromPassword("wrong-password", salt);
    CHECK(box.initialized() && other.initialized());

    const EncryptedBlob blob = box.encrypt("secret data");
    std::string out;
    CHECK(!other.decrypt(blob, &out));  // authentication must fail
}

TEST(crypto_tamper_fails) {
    const auto box = CryptoBox::deriveFromPassword("pw", ByteVec(kSaltSize, 3));
    EncryptedBlob blob = box.encrypt("integrity matters");
    blob.ciphertext[5] ^= 0xFF;  // flip one bit
    std::string out;
    CHECK(!box.decrypt(blob, &out));
}

TEST(crypto_nonce_unique_per_encrypt) {
    const auto box = CryptoBox::deriveFromPassword("pw", ByteVec(kSaltSize, 9));
    const EncryptedBlob a = box.encrypt("same message");
    const EncryptedBlob b = box.encrypt("same message");
    CHECK(a.nonce != b.nonce);
    CHECK(a.ciphertext != b.ciphertext);
}

TEST(crypto_kdf_is_salted) {
    // Same password, different salt -> different key.
    const auto k1 = CryptoBox::deriveFromPassword("pw", ByteVec(kSaltSize, 1));
    const auto k2 = CryptoBox::deriveFromPassword("pw", ByteVec(kSaltSize, 2));
    const EncryptedBlob a = k1.encrypt("x");
    const EncryptedBlob b = k2.encrypt("x");
    CHECK(a.ciphertext != b.ciphertext);
    // And k1 cannot decrypt k2's blob.
    std::string out;
    CHECK(!k1.decrypt(b, &out));
}

TEST(crypto_random_salt_generation) {
    ByteVec salt1, salt2;
    auto box1 = CryptoBox::deriveFromPassword("pw", &salt1);
    auto box2 = CryptoBox::deriveFromPassword("pw", &salt2);
    CHECK(box1.initialized() && box2.initialized());
    CHECK_EQ(salt1.size(), kSaltSize);
    CHECK(salt1 != salt2);  // fresh random salt each time
}

TEST(crypto_base64_roundtrip) {
    const std::string data = "hello \xff world";
    const std::string b64 = shadowse::toBase64(
        reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    const ByteVec back = shadowse::fromBase64(b64);
    CHECK_EQ(std::string(back.begin(), back.end()), data);
}

TEST(crypto_random_bytes) {
    const ByteVec a = shadowse::randomBytes(32);
    const ByteVec b = shadowse::randomBytes(32);
    CHECK_EQ(a.size(), 32u);
    CHECK(a != b);
}
