// SPDX-License-Identifier: MIT
// Shadow SE - robots.txt fetching, parsing and per-URL compliance checking.
#include "shadowse/robots_txt.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace shadowse {

namespace {

std::string lowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string trimWs(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Returns text up to the first '#' (comment) or ':' boundary... actually the
// value may legitimately contain ':' so only strip '#' comments.
std::string stripComment(const std::string& line) {
    const std::size_t h = line.find('#');
    return h == std::string::npos ? line : line.substr(0, h);
}

} // namespace

RobotsTxt::RobotsTxt(std::string userAgent) : ua_(std::move(userAgent)) {}

std::string RobotsTxt::originOf(const std::string& url) const {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos) return {};
    const std::size_t hostStart = scheme + 3;
    const std::size_t end = url.find_first_of("/?#", hostStart);
    const std::string host = url.substr(hostStart, end == std::string::npos ? std::string::npos
                                                                            : end - hostStart);
    if (host.empty()) return {};
    return url.substr(0, scheme + 3) + lowerAscii(host);
}

std::string RobotsTxt::pathOf(const std::string& url) const {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos) return "/";
    const std::size_t hostStart = scheme + 3;
    const std::size_t slash = url.find('/', hostStart);
    if (slash == std::string::npos) return "/";
    // Drop query/fragment.
    const std::size_t q = url.find_first_of("?#", slash);
    return url.substr(slash, q == std::string::npos ? std::string::npos : q - slash);
}

void RobotsTxt::parseBody(const std::string& body, DomainRules* out) const {
    const std::string ourUA = lowerAscii(ua_);
    bool inMatchingGroup = false;
    std::vector<std::pair<bool, std::string>> group;
    auto flushGroup = [&]() {
        if (inMatchingGroup) {
            out->rules.insert(out->rules.end(), group.begin(), group.end());
            group.clear();
        }
        inMatchingGroup = false;
    };

    std::istringstream stream(body);
    std::string line;
    while (std::getline(stream, line)) {
        line = trimWs(stripComment(line));
        if (line.empty()) continue;
        const std::string lower = lowerAscii(line);
        const std::string key = trimWs(lower.substr(0, lower.find(':')));
        const std::string value = trimWs(line.substr(lower.find(':') + 1));

        if (key == "user-agent") {
            flushGroup();
            const std::string agent = lowerAscii(value);
            inMatchingGroup = (agent == "*") || (ourUA.find(agent) != std::string::npos) ||
                              (agent.find(ourUA) != std::string::npos);
        } else if (key == "allow") {
            if (inMatchingGroup && !value.empty()) {
                group.emplace_back(true, value);
            }
        } else if (key == "disallow") {
            if (inMatchingGroup && !value.empty()) {
                group.emplace_back(false, value);
            }
            // An empty Disallow is a no-op (means allow all).
        }
    }
    flushGroup();
    out->loaded = true;
}

void RobotsTxt::loadDomain(Fetcher& fetcher, const std::string& url) {
    const std::string origin = originOf(url);
    if (origin.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& [o, rules] : domains_) {
            if (o == origin && rules.loaded) return;
        }
    }
    FetchResult res = fetcher.fetch(origin + "/robots.txt");
    DomainRules rules;
    if (res.ok) {
        parseBody(res.html, &rules);
    } else {
        rules.loaded = true;  // fail-open: treat as allowed
    }
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& [o, r] : domains_) {
        if (o == origin) {
            r = std::move(rules);
            return;
        }
    }
    domains_.emplace_back(origin, std::move(rules));
}

bool RobotsTxt::hasRules(const std::string& url) const {
    const std::string origin = originOf(url);
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& [o, r] : domains_) {
        if (o == origin) return r.loaded && !r.rules.empty();
    }
    return false;
}

bool RobotsTxt::allowed(const std::string& url) const {
    const std::string origin = originOf(url);
    std::vector<std::pair<bool, std::string>> rules;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& [o, r] : domains_) {
            if (o == origin) {
                rules = r.rules;
                break;
            }
        }
    }
    if (rules.empty()) return true;  // no policy -> allow

    const std::string path = pathOf(url);
    // The longest matching rule decides; Allow wins ties.
    std::size_t bestLen = 0;
    bool bestAllow = true;
    bool matched = false;
    for (const auto& [allow, rulePath] : rules) {
        if (rulePath.size() >= bestLen && path.compare(0, rulePath.size(), rulePath) == 0) {
            if (rulePath.size() > bestLen) {
                bestLen = rulePath.size();
                bestAllow = allow;
                matched = true;
            } else if (rulePath.size() == bestLen) {
                // Tie: Allow overrides Disallow.
                if (allow) bestAllow = true;
                matched = true;
            }
        }
    }
    if (!matched) return true;
    return bestAllow;
}

std::size_t RobotsTxt::cachedOrigins() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return domains_.size();
}

} // namespace shadowse
