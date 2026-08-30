// SPDX-License-Identifier: MIT
// Shadow SE - minimal privacy-hardened HTTP search front end.
//
// Serves a small web UI over the engine. It is designed to block tracking:
//   - never sets cookies
//   - logs nothing (queries are never persisted)
//   - sends strict privacy/security headers (CSP, no-referrer, Clear-Site-Data,
//     Permissions-Policy interest-cohort, X-Frame-Options, nosniff)
//   - HTML-escapes all reflected input (XSS-safe)
//   - binds to loopback only; expose it exclusively through Tor.
#pragma once

#include "shadowse/engine.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace shadowse {

class WebServer {
public:
    struct Options {
        std::string host = "127.0.0.1";
        std::uint16_t port = 8080;
        std::size_t maxConns = 24;
        std::size_t maxRequestBytes = 16 * 1024;
        std::size_t maxQueryLength = 256;
        std::string pageTitle = "Shadow SE";
    };

    explicit WebServer(Engine& engine, Options opts);
    ~WebServer();

    // Binds + listens. Returns false with `err` on failure.
    bool start(std::string* err);

    // Blocks serving connections until stop() is called.
    void runForever();

    // Runs runForever() on a background thread until stop() is called.
    void runAsync();

    // Signals runForever() to return.
    void stop();

    std::uint16_t port() const { return port_; }

    // Escaping helper exposed for tests.
    static std::string escapeHtml(const std::string& s);

private:
    void handleClient(int fd);
    void serveRequest(const std::string& requestLine, const std::string& headers, int fd);

    Engine& engine_;
    Options opts_;
    int listenFd_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> active_{0};
    std::thread serveThread_;
};

} // namespace shadowse
