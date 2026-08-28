// SPDX-License-Identifier: MIT
// Shadow SE - fetcher abstraction (network HTTP client or deterministic stub).
#pragma once

#include <string>

namespace shadowse {

struct FetchResult {
    bool ok = false;
    std::string html;   // raw response body
    std::string error;  // populated when !ok
};

class Fetcher {
public:
    virtual ~Fetcher() = default;
    virtual FetchResult fetch(const std::string& url) = 0;
    virtual std::string name() const = 0;
};

// Deterministic offline fetcher: generates stable sample HTML for any URL so
// the whole crawl pipeline can be exercised without network access or Tor.
class StubFetcher : public Fetcher {
public:
    FetchResult fetch(const std::string& url) override;
    std::string name() const override { return "stub"; }
};

// libcurl backed fetcher with optional SOCKS5h routing (for .onion targets).
// Compiled only when CMake finds libcurl (HAVE_CURL).
class CurlFetcher : public Fetcher {
public:
    explicit CurlFetcher(std::string socks5Proxy = "", long timeoutMs = 10000);
    ~CurlFetcher() override;
    FetchResult fetch(const std::string& url) override;
    std::string name() const override { return "libcurl"; }

private:
    std::string proxy_;
    long timeoutMs_;
};

} // namespace shadowse
