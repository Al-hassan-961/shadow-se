// SPDX-License-Identifier: MIT
// Shadow SE - simulated SOCKS5 timeout / failure recovery tests.
//
// Spins up a local TCP peer that either answers a valid SOCKS5 greeting,
// answers garbage, or stalls. Confirms probeTor classifies each correctly,
// never hangs, and recovers (Down -> Up) between runs.
#include "shadowse/tor_proxy.hpp"

#include "test_framework.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using shadowse::probeTor;
using shadowse::TorProbeResult;
using shadowse::TorStatus;

namespace {

enum class Mode { Stall, Garbage, Socks5 };

// A loopback peer that answers up to `maxConns` SOCKS5 handshakes.
struct FakeSocksServer {
    int fd = -1;
    std::uint16_t port = 0;
    std::thread t;
    Mode mode;
    int maxConns;

    explicit FakeSocksServer(Mode m, int conns = 4) : mode(m), maxConns(conns) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        ::listen(fd, 4);
        socklen_t len = sizeof(addr);
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
        t = std::thread([this] { serve(); });
    }
    ~FakeSocksServer() {
        ::close(fd);
        if (t.joinable()) t.join();
    }
    void serve() {
        for (int i = 0; i < maxConns; ++i) {
            const int c = ::accept(fd, nullptr, nullptr);
            if (c < 0) break;
            char buf[8];
            ::recv(c, buf, sizeof(buf), 0);  // read the SOCKS5 greeting
            if (mode == Mode::Garbage) {
                const char* g = "HTTP/1.0 200 OK\r\n\r\n";
                ::send(c, g, static_cast<int>(std::strlen(g)), 0);
            } else if (mode == Mode::Socks5) {
                const unsigned char ok[2] = {0x05, 0x00};
                ::send(c, reinterpret_cast<const char*>(ok), 2, 0);
            }
            // Stall: leave the connection open (no reply) so the probe's recv
            // blocks until its own receive timeout fires.
            std::this_thread::sleep_for(std::chrono::milliseconds(1800));
            ::close(c);
        }
    }
};

TorProbeResult probeUntilUp(const FakeSocksServer& server) {
    for (int i = 0; i < 3; ++i) {
        const auto r = probeTor("127.0.0.1", server.port, std::chrono::milliseconds(1500));
        if (r.status == TorStatus::Up) {
            return r;
        }
    }
    return probeTor("127.0.0.1", server.port, std::chrono::milliseconds(1500));
}

} // namespace

TEST(socks5_valid_greeting_reports_up) {
    FakeSocksServer server(Mode::Socks5);
    const auto result = probeUntilUp(server);
    CHECK(result.status == TorStatus::Up);
    CHECK(result.elapsed < std::chrono::milliseconds(5000));
}

TEST(socks5_garbage_reports_down) {
    FakeSocksServer server(Mode::Garbage);
    const auto result = probeTor("127.0.0.1", server.port, std::chrono::milliseconds(1500));
    CHECK(result.status == TorStatus::Down);
}

TEST(socks5_stall_times_out_without_hanging) {
    FakeSocksServer server(Mode::Stall);
    const auto result = probeTor("127.0.0.1", server.port, std::chrono::milliseconds(1500));
    CHECK(result.status == TorStatus::Down);  // recv times out, no reply
    CHECK(result.elapsed < std::chrono::milliseconds(5000));
}

TEST(socks5_recovers_after_failure) {
    // First: a broken peer -> Down.
    {
        FakeSocksServer bad(Mode::Garbage);
        const auto down = probeTor("127.0.0.1", bad.port, std::chrono::milliseconds(1500));
        CHECK(down.status == TorStatus::Down);
    }
    // Then: a healthy peer on a fresh port -> Up (the probe recovers).
    FakeSocksServer good(Mode::Socks5);
    const auto up = probeUntilUp(good);
    CHECK(up.status == TorStatus::Up);
}
