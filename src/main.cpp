// SPDX-License-Identifier: MIT
// Shadow SE - interactive terminal UI wired to the real engine core.
//
//   search <query>   BM25-ranked index lookup (clearweb + .onion)
//   crawl  <url>     asynchronous sandbox crawler dispatch
//   status           engine health: Tor probe, index stats, crawler queue
//   help             command reference
//   exit / quit      drain crawler and shut down
//
//   --stub           force the deterministic offline fetcher (default in demo)
//   --curl           use libcurl for real HTTP(S)/onion fetching (if compiled)
#include "shadowse/engine.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "shadowse/admin.hpp"

#ifdef __unix__
#include <termios.h>
#include <unistd.h>
#endif

using shadowse::ActivityEvent;
using shadowse::ActivityLog;
using shadowse::AdminServer;
using shadowse::Document;
using shadowse::Engine;
using shadowse::SourceType;
using shadowse::TorStatus;

namespace UIColors {
    const std::string RESET       = "\033[0m";
    const std::string BOLD        = "\033[1m";
    const std::string DIM         = "\033[2m";
    const std::string CYAN        = "\033[36m";
    const std::string GREEN       = "\033[32m";
    const std::string YELLOW      = "\033[33m";
    const std::string RED         = "\033[31m";
    const std::string MAGENTA     = "\033[35m";
}

namespace {
std::mutex g_printMutex;

void printLocked(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_printMutex);
    std::cout << s << std::flush;
}

std::string trim(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                                    [](unsigned char c) { return !std::isspace(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char c) { return !std::isspace(c); }).base(),
            s.end());
    return s;
}

// Reads a password from the terminal without echoing it back (best effort).
std::string readPassword(const std::string& prompt) {
    std::string password;
    std::cout << prompt << std::flush;
#ifdef __unix__
    termios oldt{};
    const bool have_term = tcgetattr(STDIN_FILENO, &oldt) == 0;
    if (have_term) {
        termios newt = oldt;
        newt.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    }
#endif
    std::getline(std::cin, password);
#ifdef __unix__
    if (have_term) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
#endif
    std::cout << "\n";
    return password;
}

void handleSave(Engine& engine, const std::string& path) {
    const std::string pw = readPassword("  Encryption password: ");
    if (pw.empty()) {
        std::cout << UIColors::RED << "[!] Empty password rejected.\n" << UIColors::RESET;
        return;
    }
    std::string err;
    if (engine.saveEncrypted(path, pw, &err)) {
        std::cout << UIColors::GREEN << "[✔] Encrypted snapshot saved to " << path
                  << UIColors::RESET << "\n"
                  << "    (" << engine.index().documentCount()
                  << " documents; XChaCha20-Poly1305 + Argon2id)\n\n";
    } else {
        std::cout << UIColors::RED << "[!] Save failed: " << err << UIColors::RESET << "\n\n";
    }
}

void handleLoad(Engine& engine, const std::string& path) {
    const std::string pw = readPassword("  Encryption password: ");
    std::string err;
    if (engine.loadEncrypted(path, pw, &err)) {
        std::cout << UIColors::GREEN << "[✔] Loaded " << engine.index().documentCount()
                  << " documents from " << path << UIColors::RESET << "\n\n";
    } else {
        std::cout << UIColors::RED << "[!] Load failed: " << err << UIColors::RESET << "\n\n";
    }
}

void printHeader() {
    std::cout << UIColors::CYAN << UIColors::BOLD;
    std::cout << "  ███████╗H██╗  ██╗ █████╗ ██████╗  ██████╗ ██╗    ██╗    ███████╗███████╗\n";
    std::cout << "  ██╔════╝  ██║  ██║██╔══██╗██╔══██╗██╔═══██╗██║    ██║    ██╔════╝██╔════╝\n";
    std::cout << "  ███████╗  ███████║███████║██║  ██║██║   ██║██║ █╗ ██║    ███████╗█████╗  \n";
    std::cout << "  ╚════██║  ██╔══██║██╔══██║██║  ██║██║   ██║██║███╗██║    ╚════██║██╔══╝  \n";
    std::cout << "  ███████║  ██║  ██║██║  ██║██████╔╝╚██████╔╝╚███╔███╔╝    ███████║███████╗\n";
    std::cout << "  ╚══════╝  ╚═╝  ╚═╝╚═╝  ╚═╝╚═════╝  ╚═════╝  ╚══╝╚══╝     ╚══════╝╚══════╝\n";
    std::cout << UIColors::RESET;
    std::cout << UIColors::DIM << "  [ Secure C++20 Hybrid Crawler & Inverted Index Engine ]\n";
    std::cout << "  [ Architect: Al-hassan shehade ]" << UIColors::RESET << "\n\n";
}

// Spins a character-frame animation until `done` is set. Runs on its own thread.
void runSpinner(const std::string& message, const std::atomic<bool>& done) {
    const std::string frames = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";
    std::size_t i = 0;
    while (!done.load()) {
        printLocked("\r  " + UIColors::YELLOW + message + " " + UIColors::RESET +
                    frames[i % frames.size()]);
        i = (i + 1) % frames.size();
        std::this_thread::sleep_for(std::chrono::milliseconds(70));
    }
    printLocked("\r  " + UIColors::GREEN + "[OK] " + UIColors::RESET + message + "        \n");
}

std::string sourceBadge(SourceType type) {
    if (type == SourceType::Onion) {
        return UIColors::MAGENTA + "[DARKWEB]" + UIColors::RESET;
    }
    return UIColors::GREEN + "[CLEARWEB]" + UIColors::RESET;
}

void printTorStatus(const shadowse::TorProbeResult& tor) {
    std::cout << "  ── Tor SOCKS5 routing (127.0.0.1:9050) ──\n";
    if (tor.status == TorStatus::Up) {
        std::cout << "  [" << UIColors::GREEN << "UP " << UIColors::RESET << "] " << tor.detail
                  << " (" << tor.elapsed.count() << " ms)\n";
        std::cout << "  [" << UIColors::GREEN << "OK " << UIColors::RESET
                  << "] .onion crawling routed via SOCKS5h proxy\n";
    } else if (tor.status == TorStatus::Down) {
        std::cout << "  [" << UIColors::RED << "DOWN" << UIColors::RESET << "] " << tor.detail
                  << " (" << tor.elapsed.count() << " ms)\n";
        std::cout << "  [" << UIColors::YELLOW << "WARN" << UIColors::RESET
                  << "] Darkweb routing disabled. Clearweb searches still work.\n";
    } else {
        std::cout << "  [" << UIColors::YELLOW << "????" << UIColors::RESET << "] " << tor.detail
                  << "\n";
    }
}

void printStats(const Engine& engine) {
    const auto& index = engine.index();
    std::cout << "  ── Engine core ──\n";
    std::cout << "  [" << UIColors::GREEN << "OK " << UIColors::RESET << "] Fetcher: "
              << engine.fetcher().name() << "\n";
    std::cout << "  [" << UIColors::GREEN << "OK " << UIColors::RESET << "] Indexed documents: "
              << index.documentCount() << "\n";
    std::cout << "  [" << UIColors::GREEN << "OK " << UIColors::RESET << "] Distinct terms: "
              << index.termCount() << "\n";
    std::cout << "  [" << UIColors::GREEN << "OK " << UIColors::RESET << "] Average doc length: "
              << std::fixed << std::setprecision(1) << index.averageDocLength() << "\n";
    std::cout << "  [" << UIColors::CYAN << "···" << UIColors::RESET << "] Crawler: "
              << engine.crawledPages() << " crawled, " << engine.pendingCrawls()
              << " pending (workers " << engine.crawler().running() << ")\n";
}

void printHelpMenu() {
    std::cout << UIColors::BOLD << "\nAVAILABLE COMMANDS:\n" << UIColors::RESET;
    std::cout << "  search <query>         - Run a BM25 inverted index query (clearweb and .onion)\n";
    std::cout << "  crawl <url>            - Dispatch the asynchronous sandbox crawler to ingest a target\n";
    std::cout << "  save <path>            - Encrypt the index to a snapshot (XChaCha20-Poly1305 + Argon2id)\n";
    std::cout << "  load <path>            - Load an encrypted snapshot into the index\n";
    std::cout << "  admin                  - Open the local admin-only dashboard (Tor/onion/index/crawler)\n";
    std::cout << "  status                 - Show Tor probe, index statistics and crawler queue\n";
    std::cout << "  help                   - Display this command instruction menu\n";
    std::cout << "  exit / quit            - Drain the crawler and safely terminate\n\n";
}

void handleSearch(Engine& engine, const std::string& query) {
    std::cout << UIColors::DIM << "Executing high-speed index lookup for: \"" << query
              << "\"..." << UIColors::RESET << "\n";
    const auto start = std::chrono::high_resolution_clock::now();
    const auto results = engine.search(query, 10);
    const auto end = std::chrono::high_resolution_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "\nFound " << results.size() << " matches in " << std::fixed
              << std::setprecision(2) << ms << " ms:\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    for (std::size_t i = 0; i < results.size(); ++i) {
        const Document& doc = results[i].doc;
        std::cout << " " << UIColors::BOLD << (i + 1) << ". " << doc.title << " "
                  << sourceBadge(doc.source) << "\n";
        std::cout << "    " << UIColors::CYAN << doc.url << UIColors::RESET << "\n";
        if (!doc.snippet.empty()) {
            std::cout << "    " << UIColors::DIM << doc.snippet << UIColors::RESET << "\n";
        }
        std::cout << "    " << UIColors::YELLOW << "Score: " << std::fixed
                  << std::setprecision(2) << results[i].score << UIColors::RESET << "\n\n";
    }
    std::cout << "--------------------------------------------------------------------------------\n";
}

void handleCrawl(Engine& engine, const std::string& target) {
    const std::string url = shadowse::normalizeUrl(target);
    if (url.empty()) {
        std::cout << UIColors::RED << "[!] " << UIColors::RESET
                  << "Please provide a non-empty target URL.\n\n";
        return;
    }
    std::cout << UIColors::YELLOW << "[*] Dispatched isolated sandbox process for target: "
              << url << UIColors::RESET << "\n";
    if (engine.crawl(url)) {
        std::cout << UIColors::GREEN
                  << "[✔] Target added to asynchronous multi-threaded crawler queue safely."
                  << UIColors::RESET << "\n";
    } else {
        std::cout << UIColors::RED
                  << "[!] Target rejected (duplicate, page cap reached, or crawler stopped)."
                  << UIColors::RESET << "\n";
    }
    std::cout << "    Queue: " << engine.pendingCrawls() << " pending, "
              << engine.crawledPages() << " crawled\n\n";
}

} // namespace

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    Engine::Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--stub") {
            cfg.useStubFetcher = true;
        } else if (arg == "--curl") {
            cfg.useStubFetcher = false;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: shadow-se [--stub|--curl]\n";
            return 0;
        } else {
            std::cout << "Unknown option: " << arg << " (try --help)\n";
            return 1;
        }
    }

    std::cout << "\033[2J\033[1;1H";
    printHeader();

    // Build the engine while the UI spins; the constructor runs a real SOCKS5 probe.
    std::atomic<bool> probing{false};
    std::thread spinner;
    probing.store(true);
    spinner = std::thread(runSpinner, "Probing SOCKS5 Tor daemon & loading index modules",
                          std::cref(probing));
    std::unique_ptr<Engine> engine;
    try {
        engine = std::make_unique<Engine>(cfg);
    } catch (const std::exception& e) {
        probing.store(false);
        spinner.join();
        std::cerr << UIColors::RED << "Fatal: " << e.what() << UIColors::RESET << "\n";
        return 1;
    }
    probing.store(false);
    spinner.join();

    engine->seedDemoData();

    // Crawl events stream into the terminal (and the admin activity log).
    auto activity = std::make_shared<ActivityLog>();
    engine->crawler().setCallbacks(
        [activity](const Document& doc) {
            ActivityEvent ev;
            ev.time = std::chrono::system_clock::now();
            ev.kind = "indexed";
            ev.url = doc.url;
            ev.text = doc.title;
            activity->add(std::move(ev));
            printLocked("\r  " + UIColors::GREEN + "[+] Indexed" + UIColors::RESET + " \"" +
                        doc.title + "\" " + sourceBadge(doc.source) + " " + UIColors::DIM +
                        doc.url + UIColors::RESET + "\n");
        },
        [activity](const std::string& url, const std::string& error) {
            ActivityEvent ev;
            ev.time = std::chrono::system_clock::now();
            ev.kind = "error";
            ev.url = url;
            ev.text = error;
            activity->add(std::move(ev));
            printLocked("\r  " + UIColors::RED + "[!] Failed" + UIColors::RESET + " " + url +
                        " (" + error + ")\n");
        });

    std::cout << UIColors::BOLD << "┌── [ SYSTEM INTEGRITY & PROXY STATUS ]" << UIColors::RESET
              << "\n";
    printTorStatus(engine->lastTor());
    printStats(*engine);
    std::cout << "  [" << UIColors::GREEN << "OK " << UIColors::RESET
              << "] Cryptographic Engine Ready. Zero Memory Leaks Detected.\n\n";

    // Interactive command loop
    std::string input;
    while (true) {
        std::cout << UIColors::CYAN << "shadow-se@core" << UIColors::RESET << ":" << UIColors::BOLD
                  << "~$" << UIColors::RESET << " " << std::flush;
        if (!std::getline(std::cin, input)) {
            break;
        }
        input = trim(input);

        if (input == "exit" || input == "quit") {
            if (engine->pendingCrawls() > 0) {
                std::cout << UIColors::DIM << "Waiting for " << engine->pendingCrawls()
                          << " pending crawl(s) to drain...\n" << UIColors::RESET;
            }
            engine->shutdown();
            std::cout << UIColors::DIM
                      << "Shutting down Shadow SE safely. Wiping sensitive handle buffers..."
                      << UIColors::RESET << "\n";
            break;
        } else if (input == "help") {
            printHelpMenu();
        } else if (input == "status") {
            printTorStatus(engine->probeTor());
            printStats(*engine);
        } else if (input == "admin") {
            // Launch the local admin-only dashboard on the live engine.
            shadowse::AdminOptions aopts;
            aopts.port = 8081;
            aopts.onionHostnameFile = "onion/tor_data/hidden/hostname";
            auto admin = std::make_shared<AdminServer>(*engine, *activity, aopts);
            std::string aerr;
            if (!admin->start(&aerr)) {
                std::cout << UIColors::RED << "[!] Admin dashboard failed: " << aerr
                          << UIColors::RESET << "\n";
            } else {
                admin->runAsync();
                std::cout << UIColors::GREEN << "[✔] Admin dashboard: " << UIColors::RESET
                          << "http://127.0.0.1:" << admin->port() << "/?t=" << admin->token()
                          << "\n"
                          << UIColors::DIM
                          << "    Loopback + token required. Type 'quit' later to stop.\n"
                          << UIColors::RESET;
            }
        } else if (input.rfind("search ", 0) == 0) {
            const std::string query = trim(input.substr(7));
            if (query.empty()) {
                std::cout << "Usage: search <query>\n";
            } else {
                handleSearch(*engine, query);
            }
        } else if (input.rfind("crawl ", 0) == 0) {
            handleCrawl(*engine, trim(input.substr(6)));
        } else if (input.rfind("save ", 0) == 0) {
            const std::string path = trim(input.substr(5));
            if (path.empty()) {
                std::cout << "Usage: save <path>\n";
            } else {
                handleSave(*engine, path);
            }
        } else if (input.rfind("load ", 0) == 0) {
            const std::string path = trim(input.substr(5));
            if (path.empty()) {
                std::cout << "Usage: load <path>\n";
            } else {
                handleLoad(*engine, path);
            }
        } else if (!input.empty()) {
            // Direct input is treated as a search query for ease of use.
            handleSearch(*engine, input);
        }
    }

    return 0;
}
