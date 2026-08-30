// SPDX-License-Identifier: MIT
// Shadow SE - admin dashboard binary.
//
// Runs the public web UI (loopback) for the onion site AND a local
// token-protected admin dashboard that shows live Tor/onion/index/crawler
// status and a real-time activity feed. Both bind to 127.0.0.1 only.
//
// Usage:
//   shadow-se-admin [--port 8081] [--web-port 8080] [--token TOKEN]
//                   [--onion-dir DIR] [--stub|--curl] [--load SNAP --password PW]
#include "shadowse/admin.hpp"
#include "shadowse/crypto.hpp"
#include "shadowse/engine.hpp"
#include "shadowse/web_server.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using shadowse::ActivityEvent;
using shadowse::ActivityLog;
using shadowse::AdminServer;
using shadowse::Document;
using shadowse::Engine;
using shadowse::SourceType;
using shadowse::WebServer;

namespace {
std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop.store(true); }
} // namespace

int main(int argc, char** argv) {
    Engine::Config cfg;
    std::uint16_t adminPort = 8081;
    std::uint16_t webPort = 8080;
    std::string token, loadPath, password;
    std::string onionDir = "onion";
    bool serveWeb = true;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            return {};
        };
        if (a == "--port") {
            adminPort = static_cast<std::uint16_t>(std::stoi(next()));
        } else if (a == "--web-port") {
            webPort = static_cast<std::uint16_t>(std::stoi(next()));
        } else if (a == "--token") {
            token = next();
        } else if (a == "--onion-dir") {
            onionDir = next();
        } else if (a == "--no-web") {
            serveWeb = false;
        } else if (a == "--stub") {
            cfg.useStubFetcher = true;
        } else if (a == "--curl") {
            cfg.useStubFetcher = false;
        } else if (a == "--load") {
            loadPath = next();
        } else if (a == "--password") {
            password = next();
        } else if (a == "--help" || a == "-h") {
            std::cout << "Usage: shadow-se-admin [--port 8081] [--web-port 8080] "
                         "[--token T] [--onion-dir DIR] [--stub|--curl] "
                         "[--load SNAP --password PW]\n";
            return 0;
        } else {
            std::cerr << "Unknown option: " << a << " (try --help)\n";
            return 1;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    Engine engine(cfg);
    if (!loadPath.empty()) {
        std::string err;
        if (!engine.loadEncrypted(loadPath, password, &err)) {
            std::cerr << "Failed to load encrypted snapshot: " << err << "\n";
            return 1;
        }
        std::cout << "[+] Loaded " << engine.index().documentCount()
                  << " documents from encrypted snapshot.\n";
    } else {
        engine.seedDemoData();
    }

    ActivityLog activity;
    engine.crawler().setCallbacks(
        [&activity](const Document& doc) {
            ActivityEvent ev;
            ev.time = std::chrono::system_clock::now();
            ev.kind = "indexed";
            ev.url = doc.url;
            ev.text = doc.title;
            activity.add(std::move(ev));
        },
        [&activity](const std::string& url, const std::string& error) {
            ActivityEvent ev;
            ev.time = std::chrono::system_clock::now();
            ev.kind = "error";
            ev.url = url;
            ev.text = error;
            activity.add(std::move(ev));
        });

    // Public web UI (serves the onion) - optional: the launcher runs it as a
    // separate process, so use --no-web to avoid double-binding the port.
    std::unique_ptr<WebServer> web;
    if (serveWeb) {
        WebServer::Options wopts;
        wopts.port = webPort;
        wopts.pageTitle = "Shadow SE";
        web = std::make_unique<WebServer>(engine, wopts);
        std::string err;
        if (!web->start(&err)) {
            std::cerr << "Failed to start web UI on port " << webPort << ": " << err << "\n";
            return 1;
        }
        web->runAsync();
    }

    // Admin dashboard.
    shadowse::AdminOptions aopts;
    aopts.port = adminPort;
    aopts.token = token;
    aopts.onionHostnameFile = onionDir + "/tor_data/hidden/hostname";
    AdminServer admin(engine, activity, aopts);
    {
        std::string err;
        if (!admin.start(&err)) {
            std::cerr << "Failed to start admin dashboard: " << err << "\n";
            return 1;
        }
        admin.runAsync();
    }

    std::cout << "==============================================================\n";
    if (web) {
        std::cout << "  Public web UI (onion) : http://127.0.0.1:" << web->port() << "/\n";
    } else {
        std::cout << "  Public web UI         : running separately (see start.sh)\n";
    }
    std::cout << "  ADMIN DASHBOARD       : http://127.0.0.1:" << admin.port()
              << "/?t=" << admin.token() << "\n";
    std::cout << "  Admin token           : " << admin.token() << "\n";
    std::cout << "  (loopback only + token required; Ctrl+C to stop)\n";
    std::cout << "==============================================================\n";
    std::cout << std::flush;  // ensure the token URL is in the log promptly

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    admin.stop();
    if (web) {
        web->stop();
    }
    engine.shutdown();
    std::cout << "\n[*] Admin dashboard stopped.\n";
    return 0;
}
