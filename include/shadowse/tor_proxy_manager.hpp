// SPDX-License-Identifier: MIT
// Original author: Al-hassan shehade
// Shadow SE - Tor proxy manager: SOCKS5 pool, failover, circuit isolation.
//
// This is the single seam between the engine/fetcher and the Tor network.
// The fetcher asks the manager for a proxy endpoint per request; the crawler,
// BM25 ranking, inverted index and query pipeline never see it.
//
// Two implementations:
//   makeDefaultTorProxyManager - exactly the original single-proxy behavior
//                                (one endpoint, probe once, direct when down).
//   makePooledTorProxyManager  - multiple endpoints, round-robin load
//                                balancing, failover with cooldowns, per-domain
//                                circuit isolation via SOCKS5 username/password.
#pragma once

#include "shadowse/tor_proxy.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace shadowse {

// One Tor SOCKS5 endpoint. The SOCKS5 username/password fields are Tor's
// stream-isolation extension: a unique username forces a fresh circuit.
struct TorEndpoint {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9050;
    std::string username;   // circuit-isolation credential
    std::string password;

    // Runtime health state (managed by TorProxyManager).
    bool healthy = true;
    std::uint32_t consecutiveFailures = 0;
    std::chrono::milliseconds lastLatency{0};
    std::chrono::system_clock::time_point lastUsed{};
    std::chrono::steady_clock::time_point lastProbe{};  // failure/cooldown clock

    bool sameEndpoint(const TorEndpoint& o) const { return host == o.host && port == o.port; }

    // Sentinel for "no usable proxy": callers fall back to direct clearweb
    // fetching, and reject .onion targets (mirroring the original behavior).
    static TorEndpoint none() {
        TorEndpoint e;
        e.host.clear();
        e.port = 0;
        return e;
    }
    bool isNone() const { return host.empty() || port == 0; }
};

// Builds "host:port" / "socks5h://host:port" strings.
std::string endpointKey(const TorEndpoint& ep);
std::string socks5Url(const TorEndpoint& ep);

// Parses "host:port[,host:port...]" specs into endpoints (skips malformed).
std::vector<TorEndpoint> parseProxyList(const std::vector<std::string>& specs);

// Which isolation scope generates fresh circuits.
enum class CircuitScope { PerDomain = 0, PerRequest = 1, PerBatch = 1, None = 2 };

// Snapshot of Tor health for UIs and the Prometheus /metrics endpoint.
struct TorStats {
    std::size_t proxyCount = 0;
    std::size_t healthyProxies = 0;
    std::size_t activeCircuits = 0;              // control-port GETINFO (Phase 2)
    std::chrono::milliseconds avgOnionResolution{0};
    std::uint64_t onionRequests = 0;
    std::uint64_t onionFailures = 0;
    std::uint64_t retries = 0;
    std::chrono::system_clock::time_point lastNewnym{};
};

class TorProxyManager {
public:
    virtual ~TorProxyManager() = default;

    // ---- endpoints ---------------------------------------------------------
    virtual void addProxy(const TorEndpoint& ep) = 0;
    virtual void removeProxy(const std::string& host, std::uint16_t port) = 0;
    virtual std::vector<TorEndpoint> proxies() const = 0;

    // ---- per-request selection (load-balanced, isolation-aware) ------------
    // Returns the endpoint to use for `url`, or an empty endpoint when no
    // usable proxy exists (callers then fall back to direct clearweb fetching,
    // and reject .onion URLs - mirroring the original behavior).
    virtual TorEndpoint selectProxy(const std::string& url) = 0;

    // ---- outcome reporting -> failover + health ----------------------------
    virtual void reportSuccess(const std::string& host, std::uint16_t port,
                               std::chrono::milliseconds latency) = 0;
    virtual void reportFailure(const std::string& host, std::uint16_t port,
                               const std::string& error) = 0;

    // ---- liveness ------------------------------------------------------------
    virtual TorProbeResult probe() = 0;
    virtual TorProbeResult probeEndpoint(const TorEndpoint& ep) = 0;
    virtual TorProbeResult lastProbe() const = 0;

    // ---- control port (Phase 2; no-ops until then) ---------------------------
    virtual void setControlEndpoint(const std::string& host, std::uint16_t port,
                                    const std::string& password) = 0;
    virtual bool signalNewnym() = 0;                       // SIGNAL NEWNYM
    virtual std::string getInfo(const std::string& key) = 0;  // GETINFO

    // ---- onion resilience -----------------------------------------------------
    // Additional attempts to make for a URL (onion: --onion-retries; clearweb:
    // pool failover tries). 0 reproduces today's single-shot behavior.
    virtual std::size_t maxRetries(const std::string& url) const = 0;
    virtual std::chrono::milliseconds backoff(std::size_t attempt) const = 0;

    // ---- exit geography (Phase 3; no-ops until then) --------------------------
    virtual void setExcludeCountries(const std::vector<std::string>& cc) = 0;

    // ---- metrics ----------------------------------------------------------------
    virtual TorStats stats() const = 0;

    // Options mirror today's exact behavior by default (zero breakage).
    struct Options {
        std::vector<TorEndpoint> proxies;              // default: {127.0.0.1:9050}
        std::chrono::seconds circuitRotateInterval{0}; // 0 = never auto-rotate
        CircuitScope isolation = CircuitScope::PerDomain;
        std::size_t onionRetries = 0;                  // 0 = single shot
        std::chrono::milliseconds probeTimeout{1500};
        std::chrono::milliseconds failureCooldown{5000};  // re-probe interval
        double backoffBaseMs = 250.0;                  // exponential base
        double backoffCapMs = 8000.0;                  // exponential cap
        std::vector<std::string> excludeCountries;     // empty = no filtering
    };
};

// Default implementation: byte-for-byte the original single-proxy behavior.
std::unique_ptr<TorProxyManager> makeDefaultTorProxyManager(const TorProxyManager::Options& o);
// Phase 1 implementation: pool with failover + load balancing + isolation.
std::unique_ptr<TorProxyManager> makePooledTorProxyManager(const TorProxyManager::Options& o);

} // namespace shadowse
