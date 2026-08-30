// SPDX-License-Identifier: MIT
// Shadow SE - HTTP/JSON gateway tests.
#include "shadowse/engine.hpp"
#include "shadowse/gateway.hpp"
#include "shadowse/json.hpp"

#include "test_framework.hpp"

#include <string>

using shadowse::Engine;
using shadowse::JsonGateway;
using shadowse::json::parse;
using shadowse::json::Value;

TEST(gateway_search_get_returns_json) {
    Engine::Config cfg;
    cfg.useStubFetcher = true;
    Engine engine(cfg);
    engine.seedDemoData();

    JsonGateway::Options opts;
    opts.port = 0;
    JsonGateway gw(engine, opts);
    std::string err;
    CHECK(gw.start(&err));
    CHECK(gw.port() != 0);
    gw.runAsync();

    httplib::Client cli("127.0.0.1", gw.port());
    auto res = cli.Get("/search?q=onion");
    CHECK(res && res->status == 200);
    CHECK_EQ(res->get_header_value("Content-Type"), "application/json");

    Value v;
    CHECK(parse(res->body, &v, &err));
    CHECK_EQ(v.find("query")->asString(), "onion");
    const auto& results = v.find("results")->array();
    CHECK_EQ(results.size(), 2u);
    CHECK_EQ(results[0].find("source")->asString(), "onion");
    gw.stop();
}

TEST(gateway_search_post_returns_json) {
    Engine::Config cfg;
    cfg.useStubFetcher = true;
    Engine engine(cfg);
    engine.seedDemoData();

    JsonGateway::Options opts;
    opts.port = 0;
    JsonGateway gw(engine, opts);
    std::string err;
    CHECK(gw.start(&err));
    gw.runAsync();

    httplib::Client cli("127.0.0.1", gw.port());
    httplib::Headers h;
    auto res = cli.Post("/search", "{\"q\":\"telemetry\"}", "application/json");
    CHECK(res && res->status == 200);
    Value v;
    CHECK(parse(res->body, &v, &err));
    CHECK(!v.find("results")->array().empty());

    // Malformed JSON -> 400.
    auto bad = cli.Post("/search", "not json", "application/json");
    CHECK(bad && bad->status == 400);
    gw.stop();
}

TEST(gateway_crawl_endpoints) {
    Engine::Config cfg;
    cfg.useStubFetcher = true;
    Engine engine(cfg);
    engine.seedDemoData();

    JsonGateway::Options opts;
    opts.port = 0;
    JsonGateway gw(engine, opts);
    std::string err;
    CHECK(gw.start(&err));
    gw.runAsync();

    httplib::Client cli("127.0.0.1", gw.port());
    auto get = cli.Get("/crawl?url=example.com");
    CHECK(get && get->status == 200);
    Value v;
    CHECK(parse(get->body, &v, &err));
    CHECK(v.find("enqueued")->asBool());

    auto post = cli.Post("/crawl", "{\"url\":\"https://other.example/\"}", "application/json");
    CHECK(post && post->status == 200);

    // Missing url -> 400.
    auto missing = cli.Get("/crawl");
    CHECK(missing && missing->status == 400);
    gw.stop();
    engine.shutdown();
}

TEST(gateway_status_endpoint) {
    Engine::Config cfg;
    cfg.useStubFetcher = true;
    Engine engine(cfg);
    engine.seedDemoData();

    JsonGateway::Options opts;
    opts.port = 0;
    JsonGateway gw(engine, opts);
    std::string err;
    CHECK(gw.start(&err));
    gw.runAsync();

    httplib::Client cli("127.0.0.1", gw.port());
    auto res = cli.Get("/status");
    CHECK(res && res->status == 200);
    Value v;
    CHECK(parse(res->body, &v, &err));
    CHECK(v.find("documents")->asNumber() >= 1.0);
    CHECK(v.find("fetcher")->asString() == "stub");
    gw.stop();
}

TEST(gateway_404_returns_json) {
    Engine::Config cfg;
    cfg.useStubFetcher = true;
    Engine engine(cfg);
    engine.seedDemoData();

    JsonGateway::Options opts;
    opts.port = 0;
    JsonGateway gw(engine, opts);
    std::string err;
    CHECK(gw.start(&err));
    gw.runAsync();

    httplib::Client cli("127.0.0.1", gw.port());
    auto res = cli.Get("/nope");
    CHECK(res && res->status == 404);
    CHECK_EQ(res->get_header_value("Content-Type"), "application/json");
    gw.stop();
}
