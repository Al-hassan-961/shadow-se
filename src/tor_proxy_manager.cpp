// SPDX-License-Identifier: MIT
// Original author: Al-hassan shehade
// Shadow SE - Tor proxy manager implementations.
#include "shadowse/tor_proxy_manager.hpp"

#include "shadowse/tor_args.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <random>
#include <thread>

namespace shadowse {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string endpointKey(const TorEndpoint& ep) {
    return ep.host + ":" + std::to_string(ep.port);
}

std::string socks5Url(const TorEndpoint& ep) {
    return "socks5h://" + ep.host + ":" + std::to_string(ep.port);
}

std::vector<TorEndpoint> parseProxyList(const std::vector<std::string>& specs) {
    std::vector<TorEndpoint> out;
    auto trim = [](std::string s) {
        const std::size_t b = s.find_first_not_of(" \t\r\n");
        const std::size_t e = s.find_last_not_of(" \t\r\n");
        return b == std::string::npos ? std::string{} : s.substr(b, e - b + 1);
    };
    for (const std::string& spec : specs) {
        // Accept comma-separated "host:port,host:port" specs.
        std::size_t start = 0;
        for (;;) {
            const std::size_t comma = spec.find(',', start);
            const std::string piece =
                trim(spec.substr(start, comma == std::string::npos ? std::string::npos
                                                                   : comma - start));
            if (!piece.empty()) {
                const std::size_t colon = piece.rfind(':');
                if (colon == std::string::npos) {
                    goto next_piece;  // require "host:port"
                }
                TorEndpoint ep;
                ep.host = piece.substr(0, colon);
                try {
                    const long p = std::stol(piece.substr(colon + 1));
                    if (p < 1 || p > 65535) goto next_piece;
                    ep.port = static_cast<std::uint16_t>(p);
                } catch (...) {
                    goto next_piece;  // malformed port
                }
                if (ep.host.empty()) ep.host = "127.0.0.1";
                out.push_back(std::move(ep));
            }
        next_piece:
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    return out;
}

namespace detail {

std::string circuitUsernameFor(const std::string& url, CircuitScope scope) {
    if (scope == CircuitScope::None) return {};
    std::string token = url;
    if (scope == CircuitScope::PerDomain) {
        const std::size_t scheme = url.find("://");
        const std::size_t hostStart = scheme == std::string::npos ? 0 : scheme + 3;
        const std::size_t end = url.find_first_of("/?#", hostStart);
        token = url.substr(hostStart, end == std::string::npos ? std::string::npos : end - hostStart);
    }
    std::uint64_t h = 1469598103934665603ULL;  // FNV-1a 64-bit
    for (unsigned char c : token) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), "s-%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::chrono::milliseconds backoffMs(std::size_t attempt, double baseMs, double capMs) {
    const double exp = std::min(capMs, baseMs * std::pow(2.0, static_cast<double>(attempt)));
    thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_real_distribution<double> jitter(0.0, baseMs);
    const double total = std::min(capMs, exp + jitter(rng));
    return std::chrono::milliseconds(static_cast<long long>(total));
}

bool isOnionUrl(const std::string& url) {
    return url.find(".onion") != std::string::npos;
}

TorProbeResult aggregateProbe(const std::vector<TorEndpoint>& eps,
                              std::chrono::milliseconds timeout) {
    TorProbeResult best;
    if (eps.empty()) {
        best.status = TorStatus::Unknown;
        best.detail = "no Tor proxy configured";
        return best;
    }
    const auto start = std::chrono::steady_clock::now();
    bool anyUp = false;
    std::string lastDetail;
    for (const TorEndpoint& ep : eps) {
        const TorProbeResult r = probeTor(ep.host, ep.port, timeout);
        if (r.status == TorStatus::Up) {
            anyUp = true;
            lastDetail = r.detail;
            break;
        }
        lastDetail = r.detail;
    }
    best.status = anyUp ? TorStatus::Up : TorStatus::Down;
    best.detail = anyUp ? lastDetail : "all configured Tor proxies unreachable";
    best.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return best;
}

} // namespace detail

// ---------------------------------------------------------------------------
// DefaultTorProxyManager - original single-proxy behavior, preserved exactly.
// ---------------------------------------------------------------------------

class DefaultTorProxyManager : public TorProxyManager {
public:
    explicit DefaultTorProxyManager(const Options& o) : opts_(o) {
        if (!opts_.proxies.empty()) {
            ep_ = opts_.proxies.front();
        }
    }

    void addProxy(const TorEndpoint& ep) override {
        std::lock_guard<std::mutex> lock(mtx_);
        ep_ = ep;
        up_ = false;
    }
    void removeProxy(const std::string& host, std::uint16_t port) override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (ep_.host == host && ep_.port == port) {
            ep_ = TorEndpoint::none();
            up_ = false;
        }
    }
    std::vector<TorEndpoint> proxies() const override {
        std::lock_guard<std::mutex> lock(mtx_);
        return ep_.host.empty() ? std::vector<TorEndpoint>{} : std::vector<TorEndpoint>{ep_};
    }

    TorEndpoint selectProxy(const std::string& url) override {
        (void)url;
        std::lock_guard<std::mutex> lock(mtx_);
        // Mirror the original logic: use the proxy only when the last probe was
        // Up; otherwise return an empty endpoint (direct clearweb / onion error).
        if (ep_.host.empty() || ep_.port == 0 || !up_) {
            return TorEndpoint::none();
        }
        return ep_;
    }

    void reportSuccess(const std::string& host, std::uint16_t port,
                       std::chrono::milliseconds latency) override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (ep_.host == host && ep_.port == port) {
            ep_.consecutiveFailures = 0;
            ep_.lastLatency = latency;
        }
    }
    void reportFailure(const std::string& host, std::uint16_t port,
                       const std::string& error) override {
        (void)error;
        std::lock_guard<std::mutex> lock(mtx_);
        if (ep_.host == host && ep_.port == port) {
            ++ep_.consecutiveFailures;  // recorded, but routing is unchanged
        }
    }

    TorProbeResult probe() override {
        TorProbeResult result;
        TorEndpoint ep;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            ep = ep_;
        }
        if (ep.host.empty() || ep.port == 0) {
            result.status = TorStatus::Unknown;
            result.detail = "no Tor proxy configured";
        } else {
            result = probeTor(ep.host, ep.port, opts_.probeTimeout);
        }
        {
            std::lock_guard<std::mutex> lock(mtx_);
            up_ = result.status == TorStatus::Up;
            ep_.lastProbe = std::chrono::steady_clock::now();
            lastProbe_ = result;
        }
        return result;
    }
    TorProbeResult probeEndpoint(const TorEndpoint& ep) override {
        const TorProbeResult r = probeTor(ep.host, ep.port, opts_.probeTimeout);
        std::lock_guard<std::mutex> lock(mtx_);
        if (ep_.host == ep.host && ep_.port == ep.port) {
            up_ = r.status == TorStatus::Up;
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
        return detail::isOnionUrl(url) ? opts_.onionRetries : 0;
    }
    std::chrono::milliseconds backoff(std::size_t attempt) const override {
        return detail::backoffMs(attempt, opts_.backoffBaseMs, opts_.backoffCapMs);
    }
    void setExcludeCountries(const std::vector<std::string>&) override {}

    TorStats stats() const override {
        std::lock_guard<std::mutex> lock(mtx_);
        TorStats s;
        s.proxyCount = ep_.host.empty() ? 0 : 1;
        s.healthyProxies = (ep_.host.empty() || !up_) ? 0 : 1;
        s.avgOnionResolution = ep_.lastLatency;
        return s;
    }

private:
    mutable std::mutex mtx_;
    Options opts_;
    TorEndpoint ep_ = TorEndpoint::none();  // none() = no proxy (direct fetching)
    bool up_ = false;      // last probe result
    TorProbeResult lastProbe_;
};

std::unique_ptr<TorProxyManager> makeDefaultTorProxyManager(const TorProxyManager::Options& o) {
    return std::make_unique<DefaultTorProxyManager>(o);
}

bool parseTorFlag(Engine::Config& cfg, const std::string& arg, const std::string& value) {
    if (arg == "--tor-pool") {
        cfg.useProxyPool = true;
        return true;
    }
    if (arg == "--tor-proxy") {
        const std::vector<TorEndpoint> eps = parseProxyList({value});
        if (!eps.empty()) {
            cfg.torHost = eps.front().host;
            cfg.torPort = eps.front().port;
            cfg.torProxies.clear();
            for (std::size_t i = 1; i < eps.size(); ++i) {
                cfg.torProxies.push_back(endpointKey(eps[i]));
            }
        }
        return true;
    }
    if (arg == "--onion-retries") {
        if (!value.empty()) {
            try {
                cfg.onionRetries = std::stoul(value);
            } catch (...) {
                cfg.onionRetries = 0;
            }
        }
        return true;
    }
    if (arg == "--circuit-rotate") {
        if (!value.empty()) {
            try {
                cfg.circuitRotateInterval = std::chrono::seconds(std::stol(value));
            } catch (...) {
                cfg.circuitRotateInterval = std::chrono::seconds(0);
            }
        }
        return true;
    }
    return false;
}

} // namespace shadowse
