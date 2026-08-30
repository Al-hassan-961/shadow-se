// SPDX-License-Identifier: MIT
// Shadow SE - encrypted index snapshot (serialize documents -> AEAD at rest).
#include "shadowse/snapshot.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace shadowse {

namespace {

constexpr std::uint32_t kSnapshotVersion = 1;
constexpr char kMagic[6] = {'S', 'H', 'S', 'E', '1', '\0'};

void pushU32(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

void pushU8(std::string& out, std::uint8_t v) { out.push_back(static_cast<char>(v)); }

void pushStr(std::string& out, const std::string& s) {
    pushU32(out, static_cast<std::uint32_t>(s.size()));
    out.append(s);
}

struct Reader {
    const std::string& data;
    std::size_t pos = 0;

    bool readU8(std::uint8_t* out) {
        if (pos + 1 > data.size()) return false;
        *out = static_cast<std::uint8_t>(data[pos++]);
        return true;
    }
    bool readU32(std::uint32_t* out) {
        if (pos + 4 > data.size()) return false;
        *out = static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos])) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + 1])) << 8) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + 2])) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + 3])) << 24);
        pos += 4;
        return true;
    }
    bool readStr(std::string* out) {
        std::uint32_t len = 0;
        if (!readU32(&len)) return false;
        if (pos + len > data.size()) return false;
        out->assign(data.data() + pos, len);
        pos += len;
        return true;
    }
};

std::string serializeOne(const Document& d) {
    std::string out;
    pushStr(out, d.url);
    pushStr(out, d.title);
    pushStr(out, d.snippet);
    pushStr(out, d.content);
    pushU8(out, static_cast<std::uint8_t>(d.source));
    pushU32(out, d.depth);
    return out;
}

} // namespace

std::string serializeDocuments(const std::vector<Document>& docs) {
    std::string out;
    out.append(kMagic, sizeof(kMagic));
    pushU32(out, kSnapshotVersion);
    pushU32(out, static_cast<std::uint32_t>(docs.size()));
    for (const Document& d : docs) {
        out.append(serializeOne(d));
    }
    return out;
}

bool deserializeDocuments(const std::string& data, std::vector<Document>* out,
                          std::string* err) {
    if (out == nullptr) {
        if (err) *err = "null output";
        return false;
    }
    out->clear();
    Reader r{data, 0};
    if (r.pos + sizeof(kMagic) > data.size() ||
        std::memcmp(data.data(), kMagic, sizeof(kMagic)) != 0) {
        if (err) *err = "not a Shadow SE snapshot (bad magic)";
        return false;
    }
    r.pos += sizeof(kMagic);
    std::uint32_t version = 0;
    if (!r.readU32(&version) || version != kSnapshotVersion) {
        if (err) *err = "unsupported snapshot version";
        return false;
    }
    std::uint32_t count = 0;
    if (!r.readU32(&count)) {
        if (err) *err = "corrupt snapshot header";
        return false;
    }
    if (count > 10'000'000u) {
        if (err) *err = "snapshot declares an implausible document count";
        return false;
    }
    out->reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Document d;
        std::uint8_t source = 0;
        if (!r.readStr(&d.url) || !r.readStr(&d.title) || !r.readStr(&d.snippet) ||
            !r.readStr(&d.content) || !r.readU8(&source) || !r.readU32(&d.depth)) {
            if (err) *err = "corrupt snapshot body";
            return false;
        }
        d.source = (source == static_cast<std::uint8_t>(SourceType::Onion))
                       ? SourceType::Onion
                       : SourceType::ClearWeb;
        out->push_back(std::move(d));
    }
    return true;
}

bool saveEncryptedSnapshot(const std::string& path, const std::string& password,
                           const std::vector<Document>& docs, std::string* err) {
    ByteVec salt;
    CryptoBox box = CryptoBox::deriveFromPassword(password, &salt);
    if (!box.initialized() || salt.size() != kSaltSize) {
        if (err) *err = "key derivation failed (libsodium unavailable?)";
        return false;
    }
    const std::string plaintext = serializeDocuments(docs);
    const EncryptedBlob blob = box.encrypt(plaintext);
    if (blob.nonce.size() != kNonceSize || blob.ciphertext.empty()) {
        if (err) *err = "encryption failed";
        return false;
    }

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        if (err) *err = "cannot open " + path + " for writing";
        return false;
    }
    ofs.write(kMagic, sizeof(kMagic));
    ofs.write(reinterpret_cast<const char*>(salt.data()),
              static_cast<std::streamsize>(salt.size()));
    ofs.write(reinterpret_cast<const char*>(blob.nonce.data()),
              static_cast<std::streamsize>(blob.nonce.size()));
    const std::uint64_t clen = blob.ciphertext.size();
    ofs.write(reinterpret_cast<const char*>(&clen), sizeof(clen));
    ofs.write(reinterpret_cast<const char*>(blob.ciphertext.data()),
              static_cast<std::streamsize>(blob.ciphertext.size()));
    ofs.close();
    if (!ofs) {
        if (err) *err = "failed to write " + path;
        return false;
    }
    return true;
}

bool loadEncryptedSnapshot(const std::string& path, const std::string& password,
                           std::vector<Document>* docs, std::string* err) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        if (err) *err = "cannot open " + path + " for reading";
        return false;
    }
    std::string file((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());
    if (file.size() < sizeof(kMagic) + kSaltSize + kNonceSize + sizeof(std::uint64_t)) {
        if (err) *err = "file too short to be an encrypted snapshot";
        return false;
    }
    if (std::memcmp(file.data(), kMagic, sizeof(kMagic)) != 0) {
        if (err) *err = "not an encrypted Shadow SE snapshot (bad magic)";
        return false;
    }
    std::size_t p = sizeof(kMagic);
    ByteVec salt(file.begin() + static_cast<std::ptrdiff_t>(p),
                 file.begin() + static_cast<std::ptrdiff_t>(p + kSaltSize));
    p += kSaltSize;
    ByteVec nonce(file.begin() + static_cast<std::ptrdiff_t>(p),
                  file.begin() + static_cast<std::ptrdiff_t>(p + kNonceSize));
    p += kNonceSize;
    std::uint64_t clen = 0;
    std::memcpy(&clen, file.data() + p, sizeof(clen));
    p += sizeof(clen);
    if (clen > file.size() - p) {
        if (err) *err = "snapshot length field is inconsistent";
        return false;
    }
    ByteVec ciphertext(file.begin() + static_cast<std::ptrdiff_t>(p),
                       file.begin() + static_cast<std::ptrdiff_t>(p + clen));

    const CryptoBox box = CryptoBox::deriveFromPassword(password, salt);
    if (!box.initialized()) {
        if (err) *err = "key derivation failed";
        return false;
    }
    std::string plaintext;
    if (!box.decrypt(EncryptedBlob{std::move(nonce), std::move(ciphertext)}, &plaintext)) {
        if (err) *err = "decryption failed (wrong password or tampered file)";
        return false;
    }
    if (!deserializeDocuments(plaintext, docs, err)) {
        return false;
    }
    return true;
}

} // namespace shadowse
