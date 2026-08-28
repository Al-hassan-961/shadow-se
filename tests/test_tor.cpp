// SPDX-License-Identifier: MIT
// Shadow SE - Tor SOCKS5 probe tests (environment agnostic).
#include "shadowse/tor_proxy.hpp"

#include "test_framework.hpp"

TEST(tor_probe_returns_valid_status) {
    const shadowse::TorProbeResult result = shadowse::probeTor("127.0.0.1", 9050);
    // The daemon may or may not be running, but the probe must always
    // classify the result and never hang.
    CHECK(result.status == shadowse::TorStatus::Up ||
          result.status == shadowse::TorStatus::Down);
    CHECK(result.elapsed < std::chrono::milliseconds(5000));
    CHECK(!result.detail.empty());
}

TEST(tor_probe_invalid_host) {
    const shadowse::TorProbeResult result = shadowse::probeTor("not-an-ip", 9050);
    CHECK(result.status == shadowse::TorStatus::Down);
}
