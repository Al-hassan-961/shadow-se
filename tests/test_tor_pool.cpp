// SPDX-License-Identifier: MIT
// Original author: Al-hassan shehade
// Shadow SE - pooled Tor proxy manager tests (Phase 1).
#include "shadowse/tor_proxy_manager.hpp"

#include "test_framework.hpp"

#include <string>
#include <vector>

using shadowse::CircuitScope;
using shadowse::TorEndpoint;
using shadowse::TorProxyManager;
using shadowse::makePooledTorProxyManager;

namespace {

TorProxyManager::Options optsWith(const std::vector<TorEndpoint>& eps) {
    TorProxyManager::Options o;
    o.proxies = eps;
    return o;
}

} // namespace

TEST(tor_pool_load_balances_round_robin) {
    TorEndpoint a, b;
    a.host = "127.0.0.1"; a.port = 9001;
    b.host = "127.0.0.1"; b.port = 9002;
    auto mgr = makePooledTorProxyManager(optsWith({a, b}));
    const TorEndpoint p1 = mgr->selectProxy("http://site.example/");
    const TorEndpoint p2 = mgr->selectProxy("http://site.example/");
    // Round-robin across the two (initially healthy) endpoints.
    CHECK(p1.port != p2.port);
    CHECK_EQ(mgr->stats().proxyCount, 2u);
}

TEST(tor_pool_failover_skips_unhealthy) {
    TorEndpoint a, b;
    a.host = "127.0.0.1"; a.port = 9001;
    b.host = "127.0.0.1"; b.port = 9002;
    auto mgr = makePooledTorProxyManager(optsWith({a, b}));
    mgr->reportFailure("127.0.0.1", 9001, "connection refused");
    // The failed endpoint must no longer be selected.
    for (int i = 0; i < 6; ++i) {
        const TorEndpoint p = mgr->selectProxy("http://site.example/");
        CHECK_EQ(p.port, 9002);
    }
    // Marking it healthy again restores it to the pool.
    mgr->reportSuccess("127.0.0.1", 9001, std::chrono::milliseconds(5));
    bool saw9001 = false;
    for (int i = 0; i < 10; ++i) {
        if (mgr->selectProxy("http://site.example/").port == 9001) saw9001 = true;
    }
    CHECK(saw9001);
}

TEST(tor_pool_all_down_returns_empty) {
    TorEndpoint a;
    a.host = "127.0.0.1"; a.port = 9001;
    auto mgr = makePooledTorProxyManager(optsWith({a}));
    mgr->reportFailure("127.0.0.1", 9001, "timeout");
    const TorEndpoint p = mgr->selectProxy("http://site.example/");
    CHECK(p.port == 0 || p.host.empty());
}

TEST(tor_circuit_isolation_per_domain) {
    TorEndpoint a;
    a.host = "127.0.0.1"; a.port = 9001;
    auto mgr = makePooledTorProxyManager(optsWith({a}));
    const TorEndpoint u1 = mgr->selectProxy("http://a.example/page1");
    const TorEndpoint u2 = mgr->selectProxy("http://a.example/page2");
    const TorEndpoint u3 = mgr->selectProxy("http://b.example/page1");
    CHECK(!u1.username.empty());
    CHECK_EQ(u1.username, u2.username);   // same domain -> same circuit token
    CHECK(u1.username != u3.username);    // different domain -> fresh circuit
}

TEST(tor_circuit_isolation_none) {
    TorEndpoint a;
    a.host = "127.0.0.1"; a.port = 9001;
    auto o = optsWith({a});
    o.isolation = CircuitScope::None;
    auto mgr = makePooledTorProxyManager(o);
    CHECK(mgr->selectProxy("http://a.example/").username.empty());
}

TEST(tor_stats_counts) {
    TorEndpoint a, b;
    a.host = "127.0.0.1"; a.port = 9001;
    b.host = "127.0.0.1"; b.port = 9002;
    auto mgr = makePooledTorProxyManager(optsWith({a, b}));
    mgr->reportFailure("127.0.0.1", 9001, "x");
    const auto s = mgr->stats();
    CHECK_EQ(s.proxyCount, 2u);
    CHECK_EQ(s.healthyProxies, 1u);
    // Onion request counter increments for .onion selections.
    (void)mgr->selectProxy("http://z.onion/");
    CHECK_EQ(mgr->stats().onionRequests, 1u);
}
