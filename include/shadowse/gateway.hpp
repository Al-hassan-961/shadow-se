// SPDX-License-Identifier: MIT
// Shadow SE - lightweight HTTP/JSON gateway (cpp-httplib).
//
// Exposes /search, /crawl and /status as JSON endpoints on the loopback
// interface so the engine can be driven from a browser or script alongside
// the terminal UI. Requests are size-bounded and input is validated.
#pragma once

#include "shadowse/engine.hpp"

#include "httplib.h"

#include <cstdint>
#include <string>

namespace shadowse {

class JsonGateway {
public:
    struct Options {
        std::string host = "127.0.0.1";
        std::uint16_t port = 8090;
        std::size_t maxQueryLength = 256;
    };

    explicit JsonGateway(Engine& engine, Options opts);

    bool start(std::string* err);
    void runAsync();
    void stop();

    std::uint16_t port() const { return port_; }

private:
    void setupHandlers();
    std::string searchJson(const std::string& query) const;
    std::string statusJson() const;

    Engine& engine_;
    Options opts_;
    httplib::Server srv_;
    std::uint16_t port_ = 0;
};

} // namespace shadowse
