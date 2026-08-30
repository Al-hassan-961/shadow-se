// SPDX-License-Identifier: MIT
// Shadow SE - standalone privacy-hardened web search front end.
//
// Usage:
//   shadow-se-web [--port 8080] [--stub|--curl] [--load <snapshot> --password <pw>]
//                 [--no-seed] [--title <title>]
//
// Binds to 127.0.0.1 only. Expose it exclusively through a Tor onion service;
// see the onion/ directory for a stealth (client-auth) setup.
#include "shadowse/engine.hpp"
#include "shadowse/web_server.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using shadowse::Engine;
using shadowse::TorStatus;
using shadowse::WebServer;

namespace {
std::atomic<bool> g_stop{false};

void onSignal(int) {
    g_stop.store(true);
}

} // namespace

int main(int argc, char** argv) {
    Engine::Config cfg;
    std::uint16_t port = 8080;
    std::string loadPath, password;
    std::string title = "Shadow SE";
    bool seed = true;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            return {};
        };
        if (a == "--port") {
            const std::string v = next();
            port = static_cast<std::uint16_t>(std::stoi(v));
        } else if (a == "--stub") {
            cfg.useStubFetcher = true;
        } else if (a == "--curl") {
            cfg.useStubFetcher = false;
        } else if (a == "--load") {
            loadPath = next();
        } else if (a == "--password") {
            password = next();
        } else if (a == "--title") {
            title = next();
        } else if (a == "--no-seed") {
            seed = false;
        } else if (a == "--help" || a == "-h") {
            std::cout << "Usage: shadow-se-web [--port N] [--stub|--curl] "
                         "[--load SNAPSHOT --password PW] [--no-seed] [--title T]\n";
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
        seed = false;
        std::cout << "[+] Loaded " << engine.index().documentCount()
                  << " documents from encrypted snapshot.\n";
    }
    if (seed) {
        engine.seedDemoData();
    }

    WebServer::Options wopts;
    wopts.port = port;
    wopts.pageTitle = title;
    WebServer server(engine, wopts);
    std::string err;
    if (!server.start(&err)) {
        std::cerr << "Failed to start web server: " << err << "\n";
        return 1;
    }

    std::cout << "[*] Shadow SE web front end listening on http://127.0.0.1:"
              << server.port() << "/\n";
    std::cout << "    Privacy: no cookies, no logs, strict CSP, no trackers.\n";
    if (engine.lastTor().status == TorStatus::Up) {
        std::cout << "    Tor SOCKS5: up (onion routing available).\n";
    } else {
        std::cout << "    Tor SOCKS5: down - clearweb fetching only.\n";
    }
    std::cout << "    Press Ctrl+C to stop.\n";

    server.runAsync();
    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    server.stop();
    engine.shutdown();
    std::cout << "\n[*] Shutting down web server.\n";
    return 0;
}
