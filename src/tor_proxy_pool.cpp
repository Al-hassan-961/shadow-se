// SPDX-License-Identifier: MIT
// Original author: Al-hassan shehade
// Shadow SE - pooled Tor proxy manager (Phase 1): multiple SOCKS5 endpoints,
// round-robin load balancing, failover with cooldowns, per-domain circuit
// isolation via SOCKS5 username/password, and configurable circuit rotation.
#include "shadowse/tor_proxy_manager.hpp"

#include "tor_internal.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

namespace shadowse {

// ---------------------------------------------------------------------------
// PooledTorProxyManager - failover, load balancing, circuit isolation.
// ---------------------------------------------------------------------------

class PooledTorProxyManager : public TorProxyManager {
public:
    explicit PooledTorProxyManager(const Options& o)
        : opts_(o),
          eps_(o.proxies),
          lastRotate_(std::chrono::steady_clock::now()) {}

    void addProxy(const TorEndpoint& ep) override {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& e : eps_) {
            if (e.sameEndpoint(ep)) return;
        }
        eps_.push_back(ep);
    }
    void removeProxy(const std::string& host, std::uint16_t port) override {
        std::lock_guard<std::mutex> lock(mtx_);
        eps_.erase(std::remove_if(eps_.begin(), eps_.end(),
                                  [&](const TorEndpoint& e) {
                                      return e.host == host && e.port == port;
                                  }),
                   eps_.end());
    }
    std::vector<TorEndpoint> proxies() const override {
        std::lock_guard<std::mutex> lock(mtx_);
        return eps_;
    }

    TorEndpoint selectProxy(const std::string& url) override {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mtx_);
        if (eps_.empty()) {
            return TorEndpoint::none();
        }
        if (detail::isOnionUrl(url)) {
            ++onionRequests_;
        }
        // Circuit rotation: every interval, reset health so the next selection
        // re-probes and the round-robin cursor advances to a fresh endpoint.
        if (opts_.circuitRotateInterval.count() > 0 &&
            now - lastRotate_ >= opts_.circuitRotateInterval) {
            for (auto& e : eps_) {
                e.healthy = true;  // force re-evaluation via cooldown logic below
            }
            rr_ = (rr_ + 1) % eps_.size();
            lastRotate_ = now;
        }

        // Build the candidate list: healthy endpoints, or failed ones whose
        // failure cooldown has expired (re-probed lazily on selection).
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < eps_.size(); ++i) {
            TorEndpoint& e = eps_[i];
            const bool cooled = now - e.lastProbe >= opts_.failureCooldown;
            if (e.healthy) {
                candidates.push_back(i);
            } else if (cooled) {
                candidates.push_back(i);  // allow a lazy re-probe attempt
            }
        }
        if (candidates.empty()) {
            return TorEndpoint::none();  // all proxies down (direct clearweb / onion error)
        }

        const std::size_t pick = candidates[rr_ % candidates.size()];
        ++rr_;
        TorEndpoint& chosen = eps_[pick];
        if (!chosen.healthy) {
            // Lazy re-probe of a cooled-down endpoint (bounded by probeTimeout).
            const TorProbeResult r = probeTor(chosen.host, chosen.port, opts_.probeTimeout);
            chosen.healthy = r.status == TorStatus::Up;
            chosen.lastProbe = now;
            if (!chosen.healthy) {
                return TorEndpoint::none();
            }
        }
        chosen.lastUsed = std::chrono::system_clock::now();
        TorEndpoint out = chosen;
        out.username = detail::circuitUsernameFor(url, opts_.isolation);
        out.password = out.username;  // Tor ignores the password; username isolates
        return out;
    }

    void reportSuccess(const std::string& host, std::uint16_t port,
                       std::chrono::milliseconds latency) override {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& e : eps_) {
            if (e.host == host && e.port == port) {
                e.healthy = true;
                e.consecutiveFailures = 0;
                e.lastLatency = latency;
                break;
            }
        }
    }
    void reportFailure(const std::string& host, std::uint16_t port,
                       const std::string& error) override {
        (void)error;
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& e : eps_) {
            if (e.host == host && e.port == port) {
                e.healthy = false;
                ++e.consecutiveFailures;
                e.lastProbe = std::chrono::steady_clock::now();  // start cooldown
                break;
            }
        }
    }

    TorProbeResult probe() override {
        std::vector<TorEndpoint> eps;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            eps = eps_;
        }
        const TorProbeResult result = detail::aggregateProbe(eps, opts_.probeTimeout);
        std::lock_guard<std::mutex> lock(mtx_);
        lastProbe_ = result;
        for (auto& e : eps_) {
            // Re-probe every endpoint to keep per-endpoint health accurate.
            const TorProbeResult r = probeTor(e.host, e.port, opts_.probeTimeout);
            e.healthy = r.status == TorStatus::Up;
            e.lastProbe = std::chrono::steady_clock::now();
        }
        return result;
    }
    TorProbeResult probeEndpoint(const TorEndpoint& ep) override {
        const TorProbeResult r = probeTor(ep.host, ep.port, opts_.probeTimeout);
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& e : eps_) {
            if (e.sameEndpoint(ep)) {
                e.healthy = r.status == TorStatus::Up;
                e.lastProbe = std::chrono::steady_clock::now();
            }
        }
        return r;
    }
    TorProbeResult lastProbe() const override {
        std::lock_guard<std::mutex> lock(mtx_);
        return lastProbe_;
    }

    void setControlEndpoint(const std::string&, std::uint16_t, const std::string&) override {}
    bool signalNewnym() override { return false; }
    std::string getInfo(const std::string&) override { return {}; }

    std::size_t maxRetries(const std::string& url) const override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (detail::isOnionUrl(url)) {
            return opts_.onionRetries;
        }
        // Clearweb: allow failover across the healthy pool (0 with a single proxy).
        std::size_t healthy = 0;
        for (const auto& e : eps_) {
            if (e.healthy) ++healthy;
        }
        return healthy > 0 ? healthy - 1 : 0;
    }
    std::chrono::milliseconds backoff(std::size_t attempt) const override {
        return detail::backoffMs(attempt, opts_.backoffBaseMs, opts_.backoffCapMs);
    }
    void setExcludeCountries(const std::vector<std::string>& cc) override {
        std::lock_guard<std::mutex> lock(mtx_);
        opts_.excludeCountries = cc;  // applied to Tor in Phase 3 (control port)
    }

    TorStats stats() const override {
        std::lock_guard<std::mutex> lock(mtx_);
        TorStats s;
        s.proxyCount = eps_.size();
        for (const auto& e : eps_) {
            if (e.healthy) ++s.healthyProxies;
        }
        s.onionRequests = onionRequests_;
        s.retries = retries_;
        return s;
    }

private:
    mutable std::mutex mtx_;
    Options opts_;
    std::vector<TorEndpoint> eps_;
    std::size_t rr_ = 0;
    std::chrono::steady_clock::time_point lastRotate_;
    TorProbeResult lastProbe_;
    std::uint64_t onionRequests_ = 0;
    std::uint64_t retries_ = 0;
};

std::unique_ptr<TorProxyManager> makePooledTorProxyManager(const TorProxyManager::Options& o) {
    return std::make_unique<PooledTorProxyManager>(o);
}

} // namespace shadowse
