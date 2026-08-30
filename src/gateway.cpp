// SPDX-License-Identifier: MIT
// Shadow SE - lightweight HTTP/JSON gateway (cpp-httplib).
#include "shadowse/gateway.hpp"

#include "shadowse/json.hpp"

#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

namespace shadowse {

using json::Value;

JsonGateway::JsonGateway(Engine& engine, Options opts) : engine_(engine), opts_(std::move(opts)) {
    setupHandlers();
}

bool JsonGateway::start(std::string* err) {
    bool ok = false;
    if (opts_.port == 0) {
        const int p = srv_.bind_to_any_port(opts_.host);
        ok = p > 0;
        if (ok) port_ = static_cast<std::uint16_t>(p);
    } else {
        ok = srv_.bind_to_port(opts_.host, opts_.port);
        if (ok) port_ = opts_.port;
    }
    if (!ok) {
        if (err) *err = "could not bind to " + opts_.host + ":" + std::to_string(opts_.port);
        return false;
    }
    return true;
}

void JsonGateway::runAsync() {
    std::thread([this] { srv_.listen_after_bind(); }).detach();
}

void JsonGateway::stop() {
    srv_.stop();
}

namespace {
Value errorValue(const std::string& msg) {
    Value v;
    v["error"] = msg;
    return v;
}
} // namespace

std::string JsonGateway::searchJson(const std::string& query) const {
    const std::string q = query.substr(0, opts_.maxQueryLength);
    const auto results = engine_.search(q, 20);

    Value obj;
    obj["query"] = q;
    obj["count"] = static_cast<int>(results.size());
    Value arr;
    for (const auto& r : results) {
        Value item;
        item["title"] = r.doc.title;
        item["url"] = r.doc.url;
        item["snippet"] = r.doc.snippet;
        item["score"] = r.score;
        item["source"] = r.doc.isOnion() ? "onion" : "clearweb";
        arr.push(std::move(item));
    }
    obj["results"] = std::move(arr);
    return obj.dump();
}

std::string JsonGateway::statusJson() const {
    Value v;
    v["documents"] = static_cast<int>(engine_.index().documentCount());
    v["terms"] = static_cast<int>(engine_.index().termCount());
    v["average_doc_length"] = engine_.index().averageDocLength();
    v["crawled_pages"] = static_cast<int>(engine_.crawledPages());
    v["pending_crawls"] = static_cast<int>(engine_.pendingCrawls());
    v["crawler_running"] = engine_.crawler().running();
    v["fetcher"] = engine_.fetcher().name();
    v["tor"] = engine_.lastTor().status == TorStatus::Up
                   ? "up"
                   : (engine_.lastTor().status == TorStatus::Down ? "down" : "unknown");
    return v.dump();
}

void JsonGateway::setupHandlers() {
    auto jsonError = [](httplib::Response& res, const std::string& msg, int code) {
        res.status = code;
        res.set_content(errorValue(msg).dump(), "application/json");
    };

    srv_.Get("/search", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string q = req.has_param("q") ? req.get_param_value("q") : "";
        res.set_content(searchJson(q), "application/json");
    });

    srv_.Post("/search", [this, jsonError](const httplib::Request& req, httplib::Response& res) {
        Value body;
        std::string err;
        if (!json::parse(req.body, &body, &err) || body.type() != json::Type::Object) {
            return jsonError(res, "invalid JSON body: " + err, 400);
        }
        const Value* q = body.find("q");
        const std::string query = q != nullptr ? q->asString() : "";
        res.set_content(searchJson(query), "application/json");
    });

    auto handleCrawl = [this, jsonError](const std::string& rawUrl, httplib::Response& res) {
        const std::string url = normalizeUrl(rawUrl);
        Value v;
        if (url.empty()) {
            return jsonError(res, "a valid url is required", 400);
        }
        v["url"] = url;
        v["enqueued"] = engine_.crawl(url);
        res.set_content(v.dump(), "application/json");
    };

    srv_.Get("/crawl", [handleCrawl](const httplib::Request& req, httplib::Response& res) {
        const std::string url = req.has_param("url") ? req.get_param_value("url") : "";
        handleCrawl(url, res);
    });

    srv_.Post("/crawl", [handleCrawl, jsonError](const httplib::Request& req,
                                                 httplib::Response& res) {
        Value body;
        std::string err;
        if (!json::parse(req.body, &body, &err) || body.type() != json::Type::Object) {
            return jsonError(res, "invalid JSON body: " + err, 400);
        }
        const Value* url = body.find("url");
        handleCrawl(url != nullptr ? url->asString() : "", res);
    });

    srv_.Get("/status", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(statusJson(), "application/json");
    });

    srv_.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 404) {
            res.set_content(errorValue("not found").dump(), "application/json");
        }
    });

    srv_.set_payload_max_length(64 * 1024);
    srv_.set_keep_alive_max_count(1);
    srv_.set_read_timeout(10, 0);
    srv_.set_write_timeout(10, 0);
}

} // namespace shadowse
