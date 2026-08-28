// SPDX-License-Identifier: MIT
// Shadow SE - real SOCKS5 liveness probe for the local Tor daemon.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace shadowse {

enum class TorStatus { Unknown = 0, Up = 1, Down = 2 };

struct TorProbeResult {
    TorStatus status = TorStatus::Unknown;
    std::string detail;                                  // human-readable message
    std::chrono::milliseconds elapsed{0};
};

// Performs a real SOCKS5 greeting handshake against a local Tor daemon
// (non-blocking connect with a deadline). Never blocks longer than `timeout`.
TorProbeResult probeTor(const std::string& host = "127.0.0.1",
                        std::uint16_t port = 9050,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds(1500));

} // namespace shadowse
