// SPDX-License-Identifier: MIT
// Shadow SE - admin-only loopback dashboard.
//
// A small HTTP server that shows what is happening on the engine/site: Tor
// status, the onion address, index + crawler stats, and a live activity feed.
// It binds to 127.0.0.1 only and requires an admin token, so it is reachable
// by the operator on their own machine and nobody else.
#pragma once

#include "shadowse/engine.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace shadowse {

// A single recorded event (e.g. a page was indexed or a crawl failed).
struct ActivityEvent {
    std::chrono::system_clock::time_point time;
    std::string kind;  // "indexed" | "error" | "info"
    std::string url;
    std::string text;  // title (indexed) or error message (error)
};

// Bounded, thread-safe log of recent activity.
class ActivityLog {
public:
    explicit ActivityLog(std::size_t capacity = 200);
    void add(ActivityEvent ev);
    std::vector<ActivityEvent> recent() const;
    std::size_t size() const;

private:
    mutable std::mutex mtx_;
    std::deque<ActivityEvent> events_;
    std::size_t cap_;
};

struct AdminOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 8081;
    std::string onionHostnameFile;  // path to the <HiddenServiceDir>/hostname file
    std::string torHost = "127.0.0.1";
    std::uint16_t torPort = 9050;
    std::string token;              // required to view; empty -> auto-generated
    std::size_t refreshSeconds = 5;
    std::string pageTitle = "Shadow SE - Admin Dashboard";
};

class AdminServer {
public:
    AdminServer(Engine& engine, ActivityLog& log, AdminOptions opts);
    ~AdminServer();

    bool start(std::string* err);
    void runForever();
    void runAsync();
    void stop();

    std::uint16_t port() const { return port_; }
    const std::string& token() const { return token_; }

private:
    void handleClient(int fd);
    std::string renderDashboard(bool authorized) const;

    Engine& engine_;
    ActivityLog& log_;
    AdminOptions opts_;
    std::string token_;
    int listenFd_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> running_{false};
    std::thread serveThread_;
};

// Hex helper for the admin token (exposed for tests).
std::string toHex(const std::uint8_t* data, std::size_t len);

} // namespace shadowse
