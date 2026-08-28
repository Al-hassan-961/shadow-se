// SPDX-License-Identifier: MIT
// Shadow SE - real SOCKS5 liveness probe for the local Tor daemon.
#include "shadowse/tor_proxy.hpp"

#ifndef _WIN32

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::uint8_t kSocksVersion = 0x05;
constexpr std::uint8_t kNoAuth = 0x00;

} // namespace

namespace shadowse {

TorProbeResult probeTor(const std::string& host, std::uint16_t port,
                        std::chrono::milliseconds timeout) {
    const auto start = std::chrono::steady_clock::now();
    TorProbeResult result;

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        result.status = TorStatus::Down;
        result.detail = "socket(): " + std::string(std::strerror(errno));
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return result;
    }

    // Non-blocking connect so the probe can respect the deadline.
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        result.status = TorStatus::Down;
        result.detail = "invalid proxy host: " + host;
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return result;
    }

    int rc = ::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS) {
        const std::string why = std::strerror(errno);
        ::close(fd);
        result.status = TorStatus::Down;
        result.detail = "connect() failed: " + why;
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return result;
    }

    // Wait for connect to complete (or time out).
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    timeval tv{};
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    rc = ::select(fd + 1, nullptr, &wfds, nullptr, &tv);
    if (rc == 0) {
        ::close(fd);
        result.status = TorStatus::Down;
        result.detail = "connection timed out (" + std::to_string(timeout.count()) + " ms)";
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return result;
    }
    if (rc < 0) {
        const std::string why = std::strerror(errno);
        ::close(fd);
        result.status = TorStatus::Down;
        result.detail = "select() failed: " + why;
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return result;
    }

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error != 0) {
        ::close(fd);
        result.status = TorStatus::Down;
        result.detail = "connect() refused: " + std::string(std::strerror(so_error));
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return result;
    }

    // Restore blocking mode with socket timeouts, then do a SOCKS5 greeting.
    ::fcntl(fd, F_SETFL, flags);
    timeval rcv{1, 0};
    timeval snd{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof(snd));

    const std::uint8_t greeting[3] = {kSocksVersion, 0x01, kNoAuth};
    const ssize_t sent = ::send(fd, greeting, sizeof(greeting), 0);
    if (sent != static_cast<ssize_t>(sizeof(greeting))) {
        ::close(fd);
        result.status = TorStatus::Down;
        result.detail = "SOCKS5 greeting send failed";
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return result;
    }

    std::uint8_t reply[2] = {0, 0};
    const ssize_t got = ::recv(fd, reply, sizeof(reply), 0);
    ::close(fd);

    const bool isSocks5 =
        (got == 2 && reply[0] == kSocksVersion && reply[1] == kNoAuth);
    result.status = isSocks5 ? TorStatus::Up : TorStatus::Down;
    result.detail = isSocks5
                        ? "SOCKS5 daemon reachable at " + host + ":" + std::to_string(port)
                        : "peer did not answer a valid SOCKS5 greeting";
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
}

} // namespace shadowse

#else // _WIN32

namespace shadowse {

TorProbeResult probeTor(const std::string& host, std::uint16_t port,
                        std::chrono::milliseconds timeout) {
    (void)host;
    (void)port;
    (void)timeout;
    TorProbeResult result;
    result.status = TorStatus::Unknown;
    result.detail = "SOCKS5 probing is not implemented on this platform";
    return result;
}

} // namespace shadowse

#endif
