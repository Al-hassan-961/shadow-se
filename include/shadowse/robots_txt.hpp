// SPDX-License-Identifier: MIT
// Shadow SE - robots.txt fetching, parsing and per-URL compliance checking.
#pragma once

#include "shadowse/fetcher.hpp"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace shadowse {

// Fetches and caches robots.txt per origin, then checks whether a URL is
// permitted. If robots.txt cannot be fetched the domain is treated as allowed
// (fail-open), so an unreachable policy never over-blocks the crawler.
class RobotsTxt {
public:
    explicit RobotsTxt(std::string userAgent = "ShadowSE/1.0");

    // Fetches + caches the policy for the origin of `url` (idempotent).
    void loadDomain(Fetcher& fetcher, const std::string& url);

    // True when a URL is permitted by its domain's policy. Origins without a
    // loaded policy are allowed.
    bool allowed(const std::string& url) const;

    bool hasRules(const std::string& url) const;
    std::size_t cachedOrigins() const;

private:
    struct DomainRules {
        bool loaded = false;                                     // policy fetched?
        std::vector<std::pair<bool, std::string>> rules;         // (allow, path)
    };

    std::string originOf(const std::string& url) const;
    std::string pathOf(const std::string& url) const;
    void parseBody(const std::string& body, DomainRules* out) const;

    std::string ua_;
    mutable std::mutex mtx_;
    std::vector<std::pair<std::string, DomainRules>> domains_;  // order preserved
};

} // namespace shadowse
