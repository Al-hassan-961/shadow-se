// SPDX-License-Identifier: MIT
// Shadow SE - robots.txt parsing and compliance tests.
#include "shadowse/robots_txt.hpp"

#include "test_framework.hpp"

#include <map>
#include <string>

using shadowse::Fetcher;
using shadowse::FetchResult;
using shadowse::RobotsTxt;

namespace {

// A controllable fetcher that serves canned responses per URL.
class FakeFetcher : public Fetcher {
public:
    std::map<std::string, std::string> bodies;
    std::map<std::string, std::string> failures;  // url -> error
    int robotsRequests = 0;

    FetchResult fetch(const std::string& url) override {
        FetchResult r;
        if (url.find("/robots.txt") != std::string::npos) {
            ++robotsRequests;
        }
        const auto errIt = failures.find(url);
        if (errIt != failures.end()) {
            r.error = errIt->second;
            return r;
        }
        const auto it = bodies.find(url);
        if (it != bodies.end()) {
            r.ok = true;
            r.html = it->second;
            return r;
        }
        r.ok = true;
        r.html = "<html><body>page</body></html>";
        return r;
    }
    std::string name() const override { return "fake"; }
};

} // namespace

TEST(robots_disallow_path) {
    FakeFetcher f;
    f.bodies["http://site.test/robots.txt"] = "User-agent: *\nDisallow: /admin/\n";
    RobotsTxt robots("ShadowSE/1.0");
    robots.loadDomain(f, "http://site.test/some/page");

    CHECK(robots.allowed("http://site.test/public"));
    CHECK(!robots.allowed("http://site.test/admin/panel"));
    CHECK(!robots.allowed("http://site.test/admin/"));
    CHECK(robots.hasRules("http://site.test/anything"));
}

TEST(robots_allow_overrides_disallow) {
    FakeFetcher f;
    f.bodies["http://site.test/robots.txt"] =
        "User-agent: *\nDisallow: /\nAllow: /public/\n";
    RobotsTxt robots;
    robots.loadDomain(f, "http://site.test/");

    CHECK(!robots.allowed("http://site.test/private"));
    CHECK(robots.allowed("http://site.test/public/open"));
}

TEST(robots_empty_disallow_is_ignored) {
    FakeFetcher f;
    f.bodies["http://site.test/robots.txt"] = "User-agent: *\nDisallow:\n";
    RobotsTxt robots;
    robots.loadDomain(f, "http://site.test/x");
    CHECK(robots.allowed("http://site.test/anything"));  // empty Disallow = allow all
}

TEST(robots_fail_open_on_fetch_error) {
    FakeFetcher f;
    f.failures["http://site.test/robots.txt"] = "connection refused";
    RobotsTxt robots;
    robots.loadDomain(f, "http://site.test/x");
    // Couldn't fetch the policy -> fail-open (allowed).
    CHECK(robots.allowed("http://site.test/anything"));
}

TEST(robots_unknown_origin_allowed) {
    FakeFetcher f;
    RobotsTxt robots;
    CHECK(robots.allowed("http://never-loaded.example/x"));  // no policy -> allow
}

TEST(robots_caches_once_per_domain) {
    FakeFetcher f;
    f.bodies["http://site.test/robots.txt"] = "User-agent: *\nDisallow: /private\n";
    RobotsTxt robots;
    robots.loadDomain(f, "http://site.test/a");
    robots.loadDomain(f, "http://site.test/b");  // same origin -> cached
    CHECK_EQ(f.robotsRequests, 1);
    CHECK_EQ(robots.cachedOrigins(), 1u);
}

TEST(robots_ua_specific_group) {
    FakeFetcher f;
    f.bodies["http://site.test/robots.txt"] =
        "User-agent: OtherBot\nDisallow: /\n\n"
        "User-agent: ShadowSE\nDisallow: /no-shadow\n";
    RobotsTxt robots("ShadowSE/1.0");
    robots.loadDomain(f, "http://site.test/x");
    // The OtherBot group must not affect ShadowSE.
    CHECK(robots.allowed("http://site.test/home"));
    CHECK(!robots.allowed("http://site.test/no-shadow/1"));
}
