// SPDX-License-Identifier: MIT
// Shadow SE - engine facade: index + ranking + Tor routing + crawler.
#include "shadowse/engine.hpp"

#include "shadowse/snapshot.hpp"
#include "shadowse/tokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <memory>

namespace shadowse {

Engine::Engine(Config cfg) : cfg_(cfg) {
    // Build the endpoint list: the configured default endpoint plus any
    // extra --tor-proxy endpoints.
    std::vector<TorEndpoint> endpoints;
    endpoints.push_back(TorEndpoint{cfg_.torHost, cfg_.torPort, {}, {}});
    const std::vector<TorEndpoint> extra = parseProxyList(cfg_.torProxies);
    endpoints.insert(endpoints.end(), extra.begin(), extra.end());

    TorProxyManager::Options mopts;
    mopts.proxies = std::move(endpoints);
    mopts.onionRetries = cfg_.onionRetries;
    mopts.circuitRotateInterval = cfg_.circuitRotateInterval;
    torManager_ = makeDefaultTorProxyManager(mopts);
    lastTor_ = torManager_->probe();

#ifdef HAVE_CURL
    if (!cfg_.useStubFetcher) {
        fetcher_ = std::make_shared<CurlFetcher>(torManager_);
    }
#endif
    if (!fetcher_) {
        fetcher_ = std::make_shared<StubFetcher>();
    }

    Crawler::Options copts;
    copts.workerCount = cfg_.crawlerWorkers;
    copts.maxPages = cfg_.crawlerMaxPages;
    copts.maxDepth = cfg_.crawlerMaxDepth;
    crawler_ = std::make_unique<Crawler>(index_, fetcher_, copts);
}

Engine::~Engine() {
    shutdown();
}

TorProbeResult Engine::probeTor() {
    lastTor_ = torManager_->probe();
    return lastTor_;
}

std::vector<ScoredDocument> Engine::search(const std::string& query, std::size_t limit) {
    const std::vector<std::string> tokens = tokenize(query);
    BM25Ranker ranker;
    return ranker.rank(index_, tokens, limit);
}

bool Engine::crawl(const std::string& url) {
    return crawler_->enqueue(url, 0);
}

std::size_t Engine::pendingCrawls() const {
    return crawler_->pendingCount();
}

std::size_t Engine::crawledPages() const {
    return crawler_->crawledCount();
}

bool Engine::crawlerBusy() const {
    return crawler_->running();
}

void Engine::shutdown() {
    crawler_->stop();
}

void Engine::seedDemoData() {
    const std::string intelFeed = "Real-time clearweb crawler telemetry, TLS certificate "
                                  "transparency logs, IOC correlation matrices, dark threat "
                                  "intelligence reporting and sandbox analysis pipelines.";
    const std::string docs = "Comprehensive architecture overview for the modern C++20 memory "
                             "safety model, sandboxing, inverted index compression, bm25 ranking "
                             "and tor routing used by the shadow search engine.";

    const std::string vault = "Encrypted storage node containing scraped telemetry, logs and "
                              "historical metadata mapping across darknet hidden services. Indexed "
                              "onion vault maintained over the tor anonymity network.";
    const std::string drop = "Decentralized onion drop point. Verified active via SOCKS5 proxy "
                             "routing loop against the local tor daemon on 127.0.0.1:9050. "
                             "Accepts mirror submissions and metadata exchange.";

    Document d1;
    d1.url = "https://shadow-se.internal/intel/feed-2026";
    d1.title = "Shadow SE Threat Intelligence Feed";
    d1.snippet = "Clearweb crawler telemetry, TLS transparency logs, IOC correlation";
    d1.content = intelFeed;
    d1.source = SourceType::ClearWeb;

    Document d2;
    d2.url = "https://github.com/alhassanshehade/shadow-se-docs";
    d2.title = "Open Source OSINT Framework Documentation";
    d2.snippet = "Architecture overview for C++20 memory safety, sandboxing, index compression";
    d2.content = docs;
    d2.source = SourceType::ClearWeb;

    Document d3;
    d3.url = "http://shadow77ivq2cc3x.onion/index/vault";
    d3.title = "DeepNet Secured Vault // Index Node 04";
    d3.snippet = "Encrypted storage node with scraped telemetry, logs, historical metadata";
    d3.content = vault;
    d3.source = SourceType::Onion;

    Document d4;
    d4.url = "http://anondrop55s3q5xx.onion/drop";
    d4.title = "Hidden Services Directory & Mirror";
    d4.snippet = "Decentralized onion drop point verified via SOCKS5 proxy routing loop";
    d4.content = drop;
    d4.source = SourceType::Onion;

    index_.addDocument(d1);
    index_.addDocument(d2);
    index_.addDocument(d3);
    index_.addDocument(d4);
}

bool Engine::saveEncrypted(const std::string& path, const std::string& password,
                           std::string* err) const {
    return saveEncryptedSnapshot(path, password, index_.allDocuments(), err);
}

bool Engine::loadEncrypted(const std::string& path, const std::string& password,
                           std::string* err) {
    std::vector<Document> docs;
    if (!loadEncryptedSnapshot(path, password, &docs, err)) {
        return false;
    }
    // Atomically replace the index with the loaded documents.
    index_.replaceAll(docs);
    return true;
}

std::string normalizeUrl(const std::string& input) {
    std::string url = input;
    // Trim surrounding whitespace.
    url.erase(url.begin(), std::find_if(url.begin(), url.end(),
                                        [](unsigned char c) { return !std::isspace(c); }));
    url.erase(std::find_if(url.rbegin(), url.rend(),
                           [](unsigned char c) { return !std::isspace(c); }).base(),
              url.end());
    if (url.empty()) {
        return {};
    }
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        url = "http://" + url;
    }
    return url;
}

bool Engine::saveState(const std::string& path, std::string* err) const {
    return saveSnapshot(path, index_.allDocuments(), err);
}

bool Engine::loadState(const std::string& path, std::string* err) {
    std::vector<Document> docs;
    if (!loadSnapshot(path, &docs, err)) {
        return false;
    }
    index_.replaceAll(docs);
    return true;
}

} // namespace shadowse
