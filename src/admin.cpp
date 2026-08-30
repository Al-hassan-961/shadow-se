// SPDX-License-Identifier: MIT
// Shadow SE - admin-only loopback dashboard.
#include "shadowse/admin.hpp"

#include "shadowse/crypto.hpp"
#include "shadowse/tor_proxy.hpp"
#include "shadowse/web_server.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace shadowse {

// ---------------------------------------------------------------------------
// ActivityLog
// ---------------------------------------------------------------------------

ActivityLog::ActivityLog(std::size_t capacity) : cap_(capacity == 0 ? 1 : capacity) {}

void ActivityLog::add(ActivityEvent ev) {
    std::lock_guard<std::mutex> lock(mtx_);
    events_.push_back(std::move(ev));
    while (events_.size() > cap_) {
        events_.pop_front();
    }
}

std::vector<ActivityEvent> ActivityLog::recent() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return std::vector<ActivityEvent>(events_.begin(), events_.end());
}

std::size_t ActivityLog::size() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return events_.size();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string toHex(const std::uint8_t* data, std::size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0x0F]);
    }
    return out;
}

namespace {

std::string formatTime(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmv{};
#ifdef __unix__
    localtime_r(&t, &tmv);
#else
    localtime_s(&tmv, &t);
#endif
    char buf[40];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

std::string readOnionHostname(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::ifstream f(path);
    if (!f) {
        return {};
    }
    std::string line;
    std::getline(f, line);
    return line;
}

std::string badge(const std::string& kind) {
    if (kind == "indexed") {
        return "<span style=\"color:#3fb950\">[INDEXED]</span>";
    }
    if (kind == "error") {
        return "<span style=\"color:#f85149\">[ERROR]</span>";
    }
    return "<span style=\"color:#58a6ff\">[INFO]</span>";
}

void sendHtml(int fd, int status, const char* statusText, const std::string& body) {
    std::ostringstream head;
    head << "HTTP/1.1 " << status << " " << statusText << "\r\n"
         << "Content-Type: text/html; charset=utf-8\r\n"
         << "Cache-Control: no-store, no-cache, must-revalidate\r\n"
         << "X-Content-Type-Options: nosniff\r\n"
         << "X-Frame-Options: DENY\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n"
         << "\r\n";
    const std::string h = head.str();
    const char* data = h.data();
    std::size_t sent = 0;
    while (sent < h.size()) {
        const ssize_t n = ::send(fd, data + sent, h.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
    data = body.data();
    sent = 0;
    while (sent < body.size()) {
        const ssize_t n = ::send(fd, data + sent, body.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// AdminServer
// ---------------------------------------------------------------------------

AdminServer::AdminServer(Engine& engine, ActivityLog& log, AdminOptions opts)
    : engine_(engine), log_(log), opts_(std::move(opts)) {
    if (opts_.token.empty()) {
        const ByteVec bytes = randomBytes(16);
        token_ = toHex(bytes.data(), bytes.size());
    } else {
        token_ = opts_.token;
    }
}

bool AdminServer::start(std::string* err) {
#ifndef _WIN32
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        if (err) *err = std::string("socket(): ") + std::strerror(errno);
        return false;
    }
    int one = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only - admin is local
    addr.sin_port = htons(opts_.port);
    if (::bind(listenFd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (err) *err = std::string("bind(): ") + std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    if (::listen(listenFd_, 16) != 0) {
        if (err) *err = std::string("listen(): ") + std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    socklen_t len = sizeof(addr);
    ::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    running_.store(true);
    return true;
#else
    (void)err;
    return false;
#endif
}

void AdminServer::runForever() {
    while (running_.load()) {
        const int client = ::accept(listenFd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            if (!running_.load()) break;
            continue;
        }
        std::thread([this, client] {
            handleClient(client);
            ::close(client);
        }).detach();
    }
}

void AdminServer::runAsync() {
    std::thread([this] { runForever(); }).detach();
}

void AdminServer::stop() {
    running_.store(false);
#ifndef _WIN32
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
#endif
}

void AdminServer::handleClient(int fd) {
#ifndef _WIN32
    timeval tv{10, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    std::string buf;
    buf.reserve(1024);
    char chunk[1024];
    std::size_t headerEnd = std::string::npos;
    while (buf.size() < 8192) {
        const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        buf.append(chunk, static_cast<std::size_t>(n));
        headerEnd = buf.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
    }
    if (headerEnd == std::string::npos) return;

    const std::string head = buf.substr(0, headerEnd);
    const std::size_t lineEnd = head.find("\r\n");
    std::istringstream line(head.substr(0, lineEnd));
    std::string method, target;
    line >> method >> target;

    // Only the dashboard root is served.
    bool authorized = false;
    if (method == "GET" && target.rfind("/", 0) == 0 && target.find("..") == std::string::npos) {
        const std::size_t qmark = target.find('?');
        const std::string path = qmark == std::string::npos ? target : target.substr(0, qmark);
        if (path == "/") {
            // Extract t=<token>.
            std::string query = qmark == std::string::npos ? "" : target.substr(qmark + 1);
            std::size_t pos = 0;
            while (pos <= query.size()) {
                const std::size_t amp = query.find('&', pos);
                const std::string pair = query.substr(
                    pos, amp == std::string::npos ? std::string::npos : amp - pos);
                if (pair.rfind("t=", 0) == 0) {
                    std::string t = pair.substr(2);
                    // constant-time compare
                    bool same = t.size() == token_.size();
                    for (std::size_t i = 0; i < t.size() && i < token_.size(); ++i) {
                        same = same && (t[i] == token_[i]);
                    }
                    authorized = same;
                    break;
                }
                if (amp == std::string::npos) break;
                pos = amp + 1;
            }
        }
    }
    sendHtml(fd, authorized ? 200 : 401,
             authorized ? "OK" : "Unauthorized", renderDashboard(authorized));
}

std::string AdminServer::renderDashboard(bool authorized) const {
    if (!authorized) {
        return "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
               "<title>Shadow SE Admin</title></head>"
               "<body style=\"font-family:monospace;background:#0d1117;color:#c9d1d9\">"
               "<h1>401 Unauthorized</h1>"
               "<p>This dashboard is admin-only. Reopen it with "
               "<code>?t=TOKEN</code> (the token is printed when the admin "
               "server starts).</p></body></html>";
    }

    const TorProbeResult tor = probeTor(opts_.torHost, opts_.torPort);
    const std::string onion = readOnionHostname(opts_.onionHostnameFile);

    const auto& index = engine_.index();
    const std::size_t docs = index.documentCount();
    const std::size_t terms = index.termCount();
    const double avgdl = index.averageDocLength();
    const std::size_t crawled = engine_.crawledPages();
    const std::size_t pending = engine_.pendingCrawls();
    const bool crawlerRunning = engine_.crawler().running();

    std::ostringstream h;
    h << "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
      << "<title>" << WebServer::escapeHtml(opts_.pageTitle) << "</title>\n"
      << "<meta http-equiv=\"refresh\" content=\"" << opts_.refreshSeconds << "\">\n"
      << "<meta name=\"robots\" content=\"noindex, nofollow\">\n"
      << "<style>"
      << "body{font-family:ui-monospace,Menlo,Consolas,monospace;background:#0d1117;color:#c9d1d9;"
      << "max-width:900px;margin:2rem auto;padding:0 1rem;line-height:1.55}"
      << "h1{color:#58a6ff;font-size:1.25rem}h2{color:#8b949e;font-size:1rem;"
      << "border-bottom:1px solid #21262d;padding-bottom:.3rem}"
      << ".grid{display:flex;flex-wrap:wrap;gap:1rem}.card{background:#161b22;"
      << "border:1px solid #30363d;border-radius:8px;padding:.8rem 1rem;min-width:220px;flex:1}"
      << ".k{color:#8b949e;font-size:.8rem}.v{font-size:1.2rem;color:#e6edf3}"
      << ".up{color:#3fb950}.down{color:#f85149}.warn{color:#e3b341}"
      << ".onion{color:#bc8cff;word-break:break-all}"
      << "table{width:100%;border-collapse:collapse;font-size:.85rem}"
      << "td,th{padding:.35rem .5rem;text-align:left;border-bottom:1px solid #21262d}"
      << "a{color:#58a6ff}.refresh{color:#484f58;font-size:.8rem}"
      << "</style></head><body>\n"
      << "<h1>Shadow SE — Admin Dashboard</h1>\n"
      << "<p class=\"refresh\">auto-refreshes every " << opts_.refreshSeconds
      << "s · local admin only (127.0.0.1) · token-protected · <a href=\"/?t="
      << token_ << "\">refresh now</a></p>\n";

    // Tor + onion
    h << "<h2>Site &amp; Tor</h2>\n<div class=\"grid\">\n";
    if (tor.status == TorStatus::Up) {
        h << "<div class=\"card\"><div class=\"k\">Tor SOCKS5</div>"
          << "<div class=\"v up\">UP</div><div class=\"k\">" << tor.elapsed.count()
          << " ms</div></div>\n";
    } else if (tor.status == TorStatus::Down) {
        h << "<div class=\"card\"><div class=\"k\">Tor SOCKS5</div>"
          << "<div class=\"v down\">DOWN</div><div class=\"k\">darkweb routing off</div></div>\n";
    } else {
        h << "<div class=\"card\"><div class=\"k\">Tor SOCKS5</div>"
          << "<div class=\"v warn\">UNKNOWN</div></div>\n";
    }
    h << "<div class=\"card\"><div class=\"k\">Onion address (stealth)</div>\n";
    if (!onion.empty()) {
        h << "<div class=\"v onion\">" << WebServer::escapeHtml(onion) << "</div>"
          << "<div class=\"k\">client-auth required to open</div></div>\n";
    } else {
        h << "<div class=\"v down\">not configured</div>"
          << "<div class=\"k\">run onion/setup.sh</div></div>\n";
    }
    h << "<div class=\"card\"><div class=\"k\">Public web UI</div>"
      << "<div class=\"v up\">running</div><div class=\"k\">127.0.0.1:8080 → onion</div></div>\n";
    h << "</div>\n";

    // Engine
    h << "<h2>Engine core</h2>\n<div class=\"grid\">\n";
    h << "<div class=\"card\"><div class=\"k\">Indexed documents</div>"
      << "<div class=\"v\">" << docs << "</div></div>\n";
    h << "<div class=\"card\"><div class=\"k\">Distinct terms</div>"
      << "<div class=\"v\">" << terms << "</div></div>\n";
    h << "<div class=\"card\"><div class=\"k\">Average doc length</div>"
      << "<div class=\"v\">" << std::fixed << std::setprecision(1) << avgdl << "</div></div>\n";
    h << "<div class=\"card\"><div class=\"k\">Fetcher</div>"
      << "<div class=\"v\">" << WebServer::escapeHtml(engine_.fetcher().name()) << "</div></div>\n";
    h << "</div>\n";

    // Crawler
    h << "<h2>Crawler</h2>\n<div class=\"grid\">\n";
    h << "<div class=\"card\"><div class=\"k\">Crawled pages</div>"
      << "<div class=\"v\">" << crawled << "</div></div>\n";
    h << "<div class=\"card\"><div class=\"k\">Pending queue</div>"
      << "<div class=\"v\">" << pending << "</div></div>\n";
    h << "<div class=\"card\"><div class=\"k\">Workers</div>"
      << "<div class=\"v\">" << (crawlerRunning ? "running" : "idle") << "</div></div>\n";
    h << "</div>\n";

    // Activity
    const std::vector<ActivityEvent> events = log_.recent();
    h << "<h2>Recent activity (" << events.size() << ")</h2>\n<table>\n"
      << "<tr><th>time</th><th>type</th><th>url</th><th>detail</th></tr>\n";
    for (const auto& ev : events) {
        h << "<tr><td>" << formatTime(ev.time) << "</td>"
          << "<td>" << badge(ev.kind) << "</td>"
          << "<td>" << WebServer::escapeHtml(ev.url) << "</td>"
          << "<td>" << WebServer::escapeHtml(ev.text) << "</td></tr>\n";
    }
    h << "</table>\n";
    if (events.empty()) {
        h << "<p class=\"refresh\">No activity yet — run <code>crawl &lt;url&gt;</code> in "
             "the terminal.</p>\n";
    }

    h << "</body></html>\n";
    return h.str();
}

} // namespace shadowse
