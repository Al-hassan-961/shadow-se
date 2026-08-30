// SPDX-License-Identifier: MIT
// Shadow SE - HTTP/JSON gateway binary.
//
// Exposes /search, /crawl and /status as JSON on the loopback interface so the
// engine can be driven from a browser or script.
//
// Usage:
//   shadow-se-gateway [--port 8090] [--stub|--curl] [--load SNAP --password PW]
#include "shadowse/engine.hpp"
#include "shadowse/tor_args.hpp"
#include "shadowse/gateway.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

namespace {
std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop.store(true); }
} // namespace

int main(int argc, char** argv) {
    shadowse::Engine::Config cfg;
    std::uint16_t port = 8090;
    std::string loadPath, password;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            return {};
        };
        if (a == "--port") {
            port = static_cast<std::uint16_t>(std::stoi(next()));
        } else if (a == "--stub") {
            cfg.useStubFetcher = true;
        } else if (a == "--curl") {
            cfg.useStubFetcher = false;
        } else if (a == "--tor-pool") {
            shadowse::parseTorFlag(cfg, a, "");
        } else if (a == "--tor-proxy" || a == "--onion-retries" ||
                   a == "--circuit-rotate") {
            shadowse::parseTorFlag(cfg, a, next());
        } else if (a == "--load") {
            loadPath = next();
        } else if (a == "--password") {
            password = next();
        } else if (a == "--help" || a == "-h") {
            std::cout << "Usage: shadow-se-gateway [--port 8090] [--stub|--curl] [--tor-pool] "
                         "[--tor-proxy host:port,...] [--onion-retries N] "
                         "[--circuit-rotate SECONDS] [--load SNAP --password PW]\n";
            return 0;
        } else {
            std::cerr << "Unknown option: " << a << " (try --help)\n";
            return 1;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    shadowse::Engine engine(cfg);
    if (!loadPath.empty()) {
        std::string err;
        if (!engine.loadEncrypted(loadPath, password, &err)) {
            std::cerr << "Failed to load encrypted snapshot: " << err << "\n";
            return 1;
        }
    } else {
        engine.seedDemoData();
    }

    shadowse::JsonGateway::Options gopts;
    gopts.port = port;
    shadowse::JsonGateway gateway(engine, gopts);
    std::string err;
    if (!gateway.start(&err)) {
        std::cerr << "Failed to start gateway: " << err << "\n";
        return 1;
    }
    gateway.runAsync();

    std::cout << "[*] Shadow SE JSON gateway on http://127.0.0.1:" << gateway.port() << "/\n";
    std::cout << "    GET  /search?q=...   POST /search {\"q\":\"...\"}\n";
    std::cout << "    GET  /crawl?url=...  POST /crawl {\"url\":\"...\"}\n";
    std::cout << "    GET  /status\n";
    std::cout << "    (loopback only; Ctrl+C to stop)\n";

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    gateway.stop();
    engine.shutdown();
    std::cout << "\n[*] Gateway stopped.\n";
    return 0;
}
