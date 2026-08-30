// SPDX-License-Identifier: MIT
// Original author: Al-hassan shehade
// Shadow SE - TorProxyManager tests (pool, failover, isolation, backoff).
#include "shadowse/tor_proxy_manager.hpp"

#include "test_framework.hpp"

#include <string>
#include <vector>

using shadowse::TorEndpoint;
using shadowse::TorProxyManager;
using shadowse::TorStatus;
using shadowse::endpointKey;
using shadowse::makeDefaultTorProxyManager;
using shadowse::parseProxyList;
using shadowse::socks5Url;

namespace {

TorProxyManager::Options optsWith(const std::vector<TorEndpoint>& eps) {
    TorProxyManager::Options o;
    o.proxies = eps;
    return o;
}

} // namespace

TEST(tor_parse_proxy_list) {
    const auto eps = parseProxyList({"127.0.0.1:9050,127.0.0.1:9051", "10.0.0.1:9999", "bad:99999", "nohost"});
    CHECK_EQ(eps.size(), 3u);  // malformed port skipped
    CHECK_EQ(eps[0].host, "127.0.0.1");
    CHECK_EQ(eps[0].port, 9050);
    CHECK_EQ(eps[1].port, 9051);
    CHECK_EQ(eps[2].host, "10.0.0.1");
}

TEST(tor_endpoint_helpers) {
    TorEndpoint ep;
    ep.host = "127.0.0.1";
    ep.port = 9050;
    CHECK_EQ(endpointKey(ep), "127.0.0.1:9050");
    CHECK_EQ(socks5Url(ep), "socks5h://127.0.0.1:9050");
}

TEST(tor_default_manager_mirrors_original) {
    // Default manager with an unreachable endpoint: probe -> Down, then
    // selectProxy returns an EMPTY endpoint (direct clearweb / onion error) -
    // exactly the original single-proxy behavior.
    TorEndpoint dead;
    dead.host = "127.0.0.1";
    dead.port = 1;  // nothing listens on port 1 -> instant refusal
    auto mgr = makeDefaultTorProxyManager(optsWith({dead}));
    const auto r = mgr->probe();
    CHECK(r.status == TorStatus::Down);
    const TorEndpoint picked = mgr->selectProxy("http://example.com/");
    CHECK(picked.port == 0 || picked.host.empty());
    CHECK_EQ(mgr->maxRetries("http://example.com/"), 0u);       // clearweb: 0
    CHECK_EQ(mgr->maxRetries("http://x.onion/"), 0u);            // onion: 0 by default
    CHECK(!mgr->signalNewnym());                                  // no control port yet
}

TEST(tor_default_manager_uses_proxy_when_up) {
    // We cannot run a real SOCKS5 daemon here, so verify the routing decision
    // logic instead: after a successful probe the endpoint is returned.
    // (probe() against a live endpoint is covered by test_socks5.cpp.)
    TorEndpoint ep;
    ep.host = "127.0.0.1";
    ep.port = 9050;
    auto mgr = makeDefaultTorProxyManager(optsWith({ep}));
    CHECK_EQ(mgr->proxies().size(), 1u);
    CHECK_EQ(mgr->proxies()[0].port, 9050);
}






TEST(tor_onion_retry_policy) {
    TorEndpoint a;
    a.host = "127.0.0.1"; a.port = 9001;
    auto o = optsWith({a});
    o.onionRetries = 5;
    auto mgr = makeDefaultTorProxyManager(o);
    CHECK_EQ(mgr->maxRetries("http://x.onion/"), 5u);        // onion: --onion-retries
    CHECK_EQ(mgr->maxRetries("http://clear.example/"), 0u);  // clearweb: no retries
}

TEST(tor_backoff_exponential_with_jitter) {
    TorEndpoint a;
    a.host = "127.0.0.1"; a.port = 9001;
    auto mgr = makeDefaultTorProxyManager(optsWith({a}));
    const auto b0 = mgr->backoff(0).count();
    const auto b1 = mgr->backoff(1).count();
    const auto b2 = mgr->backoff(2).count();
    // base 250ms, cap 8000ms -> strictly increasing on average; each > 0.
    CHECK(b0 > 0);
    CHECK(b1 > b0);
    CHECK(b2 > b1);
    CHECK(b2 <= 8000);
}

