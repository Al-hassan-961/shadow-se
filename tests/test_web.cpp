// SPDX-License-Identifier: MIT
// Shadow SE - web front end tests (escaping + loopback HTTP round trip).
#include "shadowse/engine.hpp"
#include "shadowse/web_server.hpp"

#include "test_framework.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using shadowse::Engine;
using shadowse::WebServer;

TEST(web_escape_html) {
    CHECK_EQ(WebServer::escapeHtml("<script>alert('x')</script>"),
             "&lt;script&gt;alert(&#39;x&#39;)&lt;/script&gt;");
    CHECK_EQ(WebServer::escapeHtml("a&b\"c"), "a&amp;b&quot;c");
    CHECK_EQ(WebServer::escapeHtml("plain"), "plain");
}

TEST(web_serves_private_search) {
    Engine::Config cfg;
    cfg.useStubFetcher = true;
    Engine engine(cfg);
    engine.seedDemoData();

    WebServer::Options opts;
    opts.port = 0;  // OS-assigned ephemeral port
    WebServer server(engine, opts);
    std::string err;
    CHECK(server.start(&err));
    CHECK(server.port() != 0);
    server.runAsync();

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server.port());
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0);

    const std::string req =
        "GET /?q=onion HTTP/1.1\r\nHost: 127.0.0.1\r\nDNT: 1\r\nConnection: close\r\n\r\n";
    ::send(fd, req.data(), req.size(), 0);

    std::string resp;
    char buf[2048];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);
    server.stop();

    CHECK(resp.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(resp.find("[DARKWEB]") != std::string::npos);
    CHECK(resp.find("shadow77ivq2cc3x.onion") != std::string::npos);
    // Privacy posture.
    CHECK(resp.find("Set-Cookie") == std::string::npos);
    CHECK(resp.find("Referrer-Policy: no-referrer") != std::string::npos);
    CHECK(resp.find("X-Frame-Options: DENY") != std::string::npos);
    CHECK(resp.find("Content-Security-Policy") != std::string::npos);
    CHECK(resp.find("Permissions-Policy") != std::string::npos);
}
