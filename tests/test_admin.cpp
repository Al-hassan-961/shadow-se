// SPDX-License-Identifier: MIT
// Shadow SE - admin dashboard tests (token auth + activity log + HTTP).
#include "shadowse/admin.hpp"
#include "shadowse/engine.hpp"

#include "test_framework.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using shadowse::ActivityEvent;
using shadowse::ActivityLog;
using shadowse::AdminServer;
using shadowse::Engine;

namespace {

std::string httpGet(std::uint16_t port, const std::string& path) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return {};
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return {};
    }
    const std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    ::send(fd, req.data(), req.size(), 0);
    std::string resp;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);
    return resp;
}

} // namespace

TEST(admin_activity_log) {
    ActivityLog log(3);
    CHECK_EQ(log.size(), 0u);
    log.add(ActivityEvent{std::chrono::system_clock::now(), "indexed", "http://a/", "A"});
    log.add(ActivityEvent{std::chrono::system_clock::now(), "indexed", "http://b/", "B"});
    log.add(ActivityEvent{std::chrono::system_clock::now(), "error", "http://c/", "fail"});
    log.add(ActivityEvent{std::chrono::system_clock::now(), "indexed", "http://d/", "D"});
    const auto events = log.recent();
    CHECK_EQ(events.size(), 3u);  // bounded to capacity
    CHECK_EQ(events.front().url, "http://b/");
    CHECK_EQ(events.back().url, "http://d/");
}

TEST(admin_token_auth_and_dashboard) {
    Engine::Config cfg;
    cfg.useStubFetcher = true;
    Engine engine(cfg);
    engine.seedDemoData();
    ActivityLog log;
    log.add(ActivityEvent{std::chrono::system_clock::now(), "indexed", "http://x.onion/", "X"});

    shadowse::AdminOptions opts;
    opts.port = 0;  // ephemeral
    opts.token = "test-token-123";
    AdminServer server(engine, log, opts);
    std::string err;
    CHECK(server.start(&err));
    CHECK(server.port() != 0);
    CHECK_EQ(server.token(), "test-token-123");
    server.runAsync();

    // Without token -> 401.
    const std::string denied = httpGet(server.port(), "/");
    CHECK(denied.rfind("HTTP/1.1 401", 0) == 0);
    CHECK(denied.find("Unauthorized") != std::string::npos);

    // Wrong token -> 401.
    const std::string wrong = httpGet(server.port(), "/?t=wrong");
    CHECK(wrong.rfind("HTTP/1.1 401", 0) == 0);

    // Correct token -> 200 dashboard with live data.
    const std::string ok = httpGet(server.port(), "/?t=test-token-123");
    CHECK(ok.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(ok.find("Admin Dashboard") != std::string::npos);
    CHECK(ok.find("http://x.onion/") != std::string::npos);  // activity feed
    CHECK(ok.find("Indexed documents") != std::string::npos);
    CHECK(ok.find("Onion address") != std::string::npos);
    CHECK(ok.find("noindex") != std::string::npos);          // privacy meta

    server.stop();
}
