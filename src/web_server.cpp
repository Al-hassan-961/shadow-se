// SPDX-License-Identifier: MIT
// Shadow SE - minimal privacy-hardened HTTP search front end.
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
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

namespace shadowse {

WebServer::WebServer(Engine& engine, Options opts) : engine_(engine), opts_(std::move(opts)) {}

WebServer::~WebServer() {
    stop();
}

std::string WebServer::escapeHtml(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

namespace {

bool urlDecode(const std::string& in, std::string* out) {
    out->clear();
    out->reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '+') {
            out->push_back(' ');
        } else if (c == '%') {
            if (i + 2 >= in.size()) {
                return false;
            }
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return -1;
            };
            const int hi = hex(in[i + 1]);
            const int lo = hex(in[i + 2]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            out->push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
        } else {
            out->push_back(c);
        }
    }
    return true;
}

const char* kPrivacyHeaders =
    "Cache-Control: no-store, no-cache, must-revalidate\r\n"
    "Pragma: no-cache\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "X-Frame-Options: DENY\r\n"
    "Referrer-Policy: no-referrer\r\n"
    "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; "
    "script-src 'none'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'; "
    "img-src 'none'; media-src 'none'; object-src 'none'\r\n"
    "Permissions-Policy: interest-cohort=(), browsing-topics=()\r\n"
    "Clear-Site-Data: \"cache\",\"cookies\",\"storage\"\r\n"
    "Cross-Origin-Opener-Policy: same-origin\r\n";

std::string renderPage(const std::string& title, const std::string& query,
                       const std::vector<ScoredDocument>& results) {
    std::ostringstream html;
    html << "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
         << "<title>" << WebServer::escapeHtml(title) << "</title>\n"
         << "<meta name=\"robots\" content=\"noindex, nofollow, noarchive\">\n"
         << "<style>"
         << "body{font-family:ui-monospace,Menlo,Consolas,monospace;background:#0d1117;color:#c9d1d9;"
         << "max-width:760px;margin:2rem auto;padding:0 1rem;line-height:1.5}"
         << "h1{color:#58a6ff;font-size:1.3rem}a{color:#58a6ff;word-break:break-all}"
         << "input{width:100%;padding:.55rem;background:#161b22;border:1px solid #30363d;"
         << "color:#c9d1d9;border-radius:6px;font:inherit}"
         << ".meta{color:#8b949e;font-size:.85rem}.d{color:#bc8cff}.c{color:#3fb950}.s{color:#e3b341}"
         << "li{margin:.9rem 0}.hint{color:#484f58;font-size:.8rem}"
         << "</style></head><body>\n"
         << "<h1>Shadow SE</h1>\n"
         << "<form method=\"get\" action=\"/\">\n"
         << "<input type=\"text\" name=\"q\" maxlength=\"256\" autofocus "
         << "placeholder=\"search the index&hellip;\" value=\""
         << WebServer::escapeHtml(query) << "\">\n"
         << "</form>\n";

    if (!query.empty()) {
        html << "<p class=\"meta\">" << results.size() << " result(s) for "
             << "<span class=\"s\">" << WebServer::escapeHtml(query) << "</span> &mdash; "
             << "no cookies, no logs, no trackers.</p>\n";
    }
    if (results.empty() && !query.empty()) {
        html << "<p class=\"meta\">No matches.</p>\n";
    }
    if (!results.empty()) {
        html << "<ol>\n";
        for (const auto& r : results) {
            const std::string badge =
                r.doc.isOnion() ? "<span class=\"d\">[DARKWEB]</span>"
                                : "<span class=\"c\">[CLEARWEB]</span>";
            html << "<li>" << badge << " <strong>"
                 << WebServer::escapeHtml(r.doc.title) << "</strong><br>\n"
                 << "<a href=\"" << WebServer::escapeHtml(r.doc.url) << "\">"
                 << WebServer::escapeHtml(r.doc.url) << "</a><br>\n"
                 << "<span class=\"meta\">" << WebServer::escapeHtml(r.doc.snippet)
                 << "</span> <span class=\"s\">score "
                 << std::fixed << std::setprecision(2) << r.score << "</span></li>\n";
        }
        html << "</ol>\n";
    }
    html << "<p class=\"hint\">Use <code>search</code>/<code>crawl</code> in the terminal, "
         << "or crawl then <code>save</code> an encrypted snapshot and start "
         << "<code>shadow-se-web --load &lt;snapshot&gt;</code>.</p>\n"
         << "</body></html>\n";
    return html.str();
}

void sendResponse(int fd, int status, const char* statusText, const std::string& body) {
    std::ostringstream head;
    head << "HTTP/1.1 " << status << " " << statusText << "\r\n"
         << "Content-Type: text/html; charset=utf-8\r\n"
         << kPrivacyHeaders
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n"
         << "\r\n";
    const std::string h = head.str();
    // Best-effort send of the full response.
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

bool WebServer::start(std::string* err) {
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
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only - expose via Tor only
    addr.sin_port = htons(opts_.port);
    if (::bind(listenFd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (err) *err = std::string("bind(): ") + std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    if (::listen(listenFd_, 32) != 0) {
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

void WebServer::runForever() {
    while (running_.load()) {
        const int client = ::accept(listenFd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            if (!running_.load()) break;
            continue;
        }
        if (active_.load() >= opts_.maxConns) {
            sendResponse(client, 503, "Service Unavailable",
                         "<html><body>Busy - try again in a moment.</body></html>");
            ::close(client);
            continue;
        }
        active_.fetch_add(1);
        std::thread([this, client] {
            handleClient(client);
            ::close(client);
            active_.fetch_sub(1);
        }).detach();
    }
}

void WebServer::runAsync() {
    serveThread_ = std::thread([this] { runForever(); });
}

void WebServer::stop() {
    running_.store(false);
#ifndef _WIN32
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
#endif
    if (serveThread_.joinable()) {
        serveThread_.join();
    }
}

void WebServer::handleClient(int fd) {
#ifndef _WIN32
    timeval tv{10, 0};  // 10s recv timeout
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::string buf;
    buf.reserve(1024);
    char chunk[1024];
    std::size_t headerEnd = std::string::npos;
    while (buf.size() < opts_.maxRequestBytes) {
        const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            break;
        }
        buf.append(chunk, static_cast<std::size_t>(n));
        headerEnd = buf.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            break;
        }
    }
    if (headerEnd == std::string::npos) {
        return;  // malformed or oversized request
    }
    const std::string head = buf.substr(0, headerEnd);
    const std::size_t lineEnd = head.find("\r\n");
    const std::string requestLine = head.substr(0, lineEnd);
    const std::string headers =
        lineEnd == std::string::npos ? "" : head.substr(lineEnd + 2);
    serveRequest(requestLine, headers, fd);
}

void WebServer::serveRequest(const std::string& requestLine, const std::string& headers,
                             int fd) {
    (void)headers;
    std::istringstream line(requestLine);
    std::string method, target;
    line >> method >> target;

    if (method != "GET") {
        sendResponse(fd, 405, "Method Not Allowed", "<html><body>GET only.</body></html>");
        return;
    }
    // Reject path traversal.
    if (target.find("..") != std::string::npos) {
        sendResponse(fd, 400, "Bad Request", "<html><body>Bad request.</body></html>");
        return;
    }

    std::string path = target;
    std::string queryString;
    const std::size_t qmark = target.find('?');
    if (qmark != std::string::npos) {
        path = target.substr(0, qmark);
        queryString = target.substr(qmark + 1);
    }
    if (path != "/") {
        sendResponse(fd, 404, "Not Found",
                     "<html><body><h1>404</h1><p>Not found. Try <a href=\"/\">/</a>.</p></body></html>");
        return;
    }

    // Parse the q= parameter (query length is capped server-side).
    std::string query;
    std::size_t pos = 0;
    while (pos <= queryString.size()) {
        const std::size_t amp = queryString.find('&', pos);
        const std::string pair =
            queryString.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (pair.rfind("q=", 0) == 0) {
            std::string decoded;
            if (urlDecode(pair.substr(2), &decoded)) {
                query = decoded;
            }
            break;
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    if (query.size() > opts_.maxQueryLength) {
        query.resize(opts_.maxQueryLength);
    }

    // No query -> search page; with query -> results. Nothing is logged here.
    const auto results = query.empty() ? std::vector<ScoredDocument>{}
                                       : engine_.search(query, 20);
    const std::string body = renderPage(opts_.pageTitle, query, results);
    sendResponse(fd, 200, "OK", body);
}

} // namespace shadowse
