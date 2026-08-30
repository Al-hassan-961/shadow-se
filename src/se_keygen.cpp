// SPDX-License-Identifier: MIT
// Shadow SE - Tor v3 onion client-authorization (stealth) key generator.
//
// Stealth onions only answer to clients that hold a matching x25519 keypair.
// Usage:
//   se-keygen --client <name>     generate one stealth client keypair
//   se-keygen --passphrase [N]    generate a strong random passphrase (default 32 chars)
//
// The client keypair is printed as base64 (standard alphabet, unpadded) so the
// service operator writes the PUBLIC key into:
//   <HiddenServiceDir>/authorized_clients/<name>.auth
//     descriptor:x25519:<public>
// and each authorized client installs the PRIVATE key on their own Tor host:
//   ClientOnionAuthDir/<onion-address>.auth_private
//     <onion-address-without-.onion>:descriptor:x25519:<private>
#include <cstdio>
#include <iostream>
#include <string>

#ifdef HAVE_SODIUM
#include <sodium.h>
#endif

namespace {

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char* kB32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

std::string b64Unpadded(const unsigned char* data, std::size_t len) {
    std::string out;
    std::size_t i = 0;
    while (i + 3 <= len) {
        const unsigned v = (static_cast<unsigned>(data[i]) << 16) |
                           (static_cast<unsigned>(data[i + 1]) << 8) |
                           static_cast<unsigned>(data[i + 2]);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
        out += kB64[v & 63];
        i += 3;
    }
    const std::size_t rem = len - i;
    if (rem == 1) {
        const unsigned v = static_cast<unsigned>(data[i]) << 16;
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
    } else if (rem == 2) {
        const unsigned v = (static_cast<unsigned>(data[i]) << 16) |
                           (static_cast<unsigned>(data[i + 1]) << 8);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
    }
    return out;
}

// RFC 4648 base32 without padding - Tor v3 client-auth keys use this.
std::string b32Unpadded(const unsigned char* data, std::size_t len) {
    std::string out;
    out.reserve((len * 8 + 4) / 5);
    std::uint64_t buffer = 0;
    int bits = 0;
    for (std::size_t i = 0; i < len; ++i) {
        buffer = (buffer << 8) | data[i];
        bits += 8;
        while (bits >= 5) {
            out += kB32[(buffer >> (bits - 5)) & 31];
            bits -= 5;
        }
    }
    if (bits > 0) {
        out += kB32[(buffer << (5 - bits)) & 31];
    }
    return out;
}

int genClient(const std::string& name) {
#ifdef HAVE_SODIUM
    if (sodium_init() < 0) {
        std::cerr << "libsodium init failed\n";
        return 1;
    }
    unsigned char priv[32];
    unsigned char pub[32];
    randombytes_buf(priv, sizeof(priv));
    crypto_scalarmult_curve25519_base(pub, priv);

    const std::string pubB32 = b32Unpadded(pub, sizeof(pub));
    const std::string privB32 = b32Unpadded(priv, sizeof(priv));

    std::printf("client name : %s\n", name.c_str());
    std::printf("public key  : %s\n", pubB32.c_str());
    std::printf("private key : %s\n", privB32.c_str());
    std::printf("public key  (base64): %s\n", b64Unpadded(pub, sizeof(pub)).c_str());
    std::printf("\nService side - write this into <HiddenServiceDir>/authorized_clients/%s.auth\n",
                name.c_str());
    std::printf("  descriptor:x25519:%s\n", pubB32.c_str());
    std::printf("\nClient side - install this private key in ClientOnionAuthDir\n");
    std::printf("  <onion-address>:descriptor:x25519:%s\n", privB32.c_str());
    std::printf("\nKeep the private key secret. Anyone holding it can access the onion.\n");
    sodium_memzero(priv, sizeof(priv));
    return 0;
#else
    (void)name;
    std::cerr << "se-keygen requires libsodium (build with HAVE_SODIUM).\n";
    return 1;
#endif
}

int genPassphrase(std::size_t n) {
#ifdef HAVE_SODIUM
    if (sodium_init() < 0) {
        std::cerr << "libsodium init failed\n";
        return 1;
    }
    const char* chars = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%^&*-_=+";
    const std::size_t clen = std::string(chars).size();
    std::string pw;
    pw.reserve(n);
    unsigned char buf[64];
    for (std::size_t done = 0; done < n; done += sizeof(buf)) {
        randombytes_buf(buf, sizeof(buf));
        for (std::size_t i = 0; i < sizeof(buf) && done + i < n; ++i) {
            pw += chars[buf[i] % clen];
        }
    }
    std::printf("%s\n", pw.c_str());
    return 0;
#else
    (void)n;
    std::cerr << "se-keygen requires libsodium (build with HAVE_SODIUM).\n";
    return 1;
#endif
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--client" && i + 1 < argc) {
            return genClient(argv[++i]);
        }
        if (a == "--passphrase") {
            std::size_t n = 32;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                n = static_cast<std::size_t>(std::atoi(argv[++i]));
            }
            return genPassphrase(n < 8 ? 8 : n);
        }
    }
    std::printf("Usage:\n");
    std::printf("  se-keygen --client <name>     generate a stealth onion client keypair\n");
    std::printf("  se-keygen --passphrase [N]    generate a strong random passphrase\n");
    return 0;
}
