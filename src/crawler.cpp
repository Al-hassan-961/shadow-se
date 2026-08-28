// SPDX-License-Identifier: MIT
// Shadow SE - asynchronous breadth-first crawler with a worker pool.
#include "shadowse/crawler.hpp"

#include "shadowse/tokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace shadowse {

// ---------------------------------------------------------------------------
// HTML extraction helpers.
// ---------------------------------------------------------------------------

namespace {

// Case-insensitive substring search.
std::size_t ciFind(const std::string& hay, const std::string& needle, std::size_t pos = 0) {
    if (needle.empty() || pos >= hay.size()) {
        return std::string::npos;
    }
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    for (std::size_t i = pos; i + needle.size() <= hay.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lower(static_cast<unsigned char>(hay[i + j])) != lower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return i;
        }
    }
    return std::string::npos;
}

// Reads a quoted attribute value starting at `valueStart` (after '=').
std::string readAttributeValue(const std::string& html, std::size_t valueStart) {
    if (valueStart >= html.size()) {
        return {};
    }
    const char q = html[valueStart];
    const std::size_t end = html.find(q, valueStart + 1);
    if (end == std::string::npos) {
        return {};
    }
    return html.substr(valueStart + 1, end - valueStart - 1);
}

void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(),
            s.end());
}

std::string collapseWhitespace(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool inSpace = false;
    for (unsigned char c : s) {
        if (std::isspace(c)) {
            inSpace = true;
            continue;
        }
        if (inSpace && !out.empty()) {
            out.push_back(' ');
        }
        inSpace = false;
        out.push_back(static_cast<char>(c));
    }
    return out;
}

} // namespace

std::string htmlExtractTitle(const std::string& html) {
    const std::size_t tag = ciFind(html, "<title");
    if (tag == std::string::npos) {
        return {};
    }
    const std::size_t open = html.find('>', tag);
    if (open == std::string::npos) {
        return {};
    }
    const std::size_t close = ciFind(html, "</title>", open + 1);
    if (close == std::string::npos) {
        return {};
    }
    std::string title = html.substr(open + 1, close - open - 1);
    title = collapseWhitespace(title);
    trim(title);
    return title;
}

std::string htmlExtractDescription(const std::string& html) {
    std::size_t pos = 0;
    while ((pos = ciFind(html, "<meta", pos)) != std::string::npos) {
        const std::size_t open = html.find('>', pos);
        if (open == std::string::npos) {
            break;
        }
        const std::string tag = html.substr(pos, open - pos);
        if (ciFind(tag, "name=\"description\"") != std::string::npos ||
            ciFind(tag, "name='description'") != std::string::npos ||
            ciFind(tag, "name=\"DESCRIPTION\"") != std::string::npos) {
            const std::size_t eq = ciFind(tag, "content=");
            if (eq != std::string::npos) {
                return collapseWhitespace(readAttributeValue(tag, eq + 8));
            }
        }
        pos = open + 1;
    }
    return {};
}

std::string htmlStripTags(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool inTag = false;
    for (char c : html) {
        if (c == '<') {
            inTag = true;
            // Treat a tag boundary as a word separator when text directly abuts it.
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
            continue;
        }
        if (c == '>') {
            inTag = false;
            continue;
        }
        if (inTag) {
            continue;
        }
        // Never leave a space before punctuation (e.g. "word !" -> "word!").
        if (out.size() >= 1 && out.back() == ' ' &&
            std::ispunct(static_cast<unsigned char>(c))) {
            out.pop_back();
        }
        out.push_back(c);
    }
    return collapseWhitespace(out);
}

std::vector<std::string> htmlExtractLinks(const std::string& html, const std::string& baseUrl) {
    std::vector<std::string> links;

    // Parse the base URL into scheme://host[/path].
    std::string scheme, host, path;
    const std::size_t schemeEnd = baseUrl.find("://");
    if (schemeEnd != std::string::npos) {
        scheme = baseUrl.substr(0, schemeEnd);
        const std::size_t hostStart = schemeEnd + 3;
        const std::size_t slash = baseUrl.find('/', hostStart);
        if (slash == std::string::npos) {
            host = baseUrl.substr(hostStart);
            path = "/";
        } else {
            host = baseUrl.substr(hostStart, slash - hostStart);
            path = baseUrl.substr(slash);
        }
    }
    if (scheme != "http" && scheme != "https") {
        return links;
    }
    const std::size_t lastSlash = path.find_last_of('/');
    const std::string dir = lastSlash == std::string::npos ? "/" : path.substr(0, lastSlash + 1);

    std::size_t pos = 0;
    while ((pos = ciFind(html, "<a ", pos)) != std::string::npos) {
        const std::size_t end = html.find('>', pos);
        if (end == std::string::npos) {
            break;
        }
        const std::string tag = html.substr(pos, end - pos);
        const std::size_t href = ciFind(tag, "href=");
        if (href != std::string::npos) {
            const std::string raw = readAttributeValue(tag, href + 5);
            std::string resolved;
            if (!raw.empty()) {
                if (raw.rfind("http://", 0) == 0 || raw.rfind("https://", 0) == 0) {
                    resolved = raw;
                } else if (raw.rfind("//", 0) == 0) {
                    resolved = scheme + ":" + raw;
                } else if (raw.find(':') != std::string::npos) {
                    // Other URI schemes (mailto:, tel:, javascript:, data:, ...)
                    // are not crawlable documents - left empty on purpose.
                } else if (raw[0] == '/') {
                    resolved = scheme + "://" + host + raw;
                } else {
                    resolved = scheme + "://" + host + dir + raw;
                }
            }
            if (!resolved.empty()) {
                links.push_back(std::move(resolved));
            }
        }
        pos = end + 1;
    }
    return links;
}

// ---------------------------------------------------------------------------
// Crawler.
// ---------------------------------------------------------------------------

Crawler::Crawler(InvertedIndex& index, std::shared_ptr<Fetcher> fetcher, Options opts)
    : index_(index), fetcher_(std::move(fetcher)), opts_(opts) {}

Crawler::~Crawler() {
    stop();
}

bool Crawler::enqueue(const std::string& url, std::uint32_t depth) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (stop_.load()) {
        return false;
    }
    if (!seenUrls_.insert(url).second) {
        return false;  // duplicate
    }
    if (crawled_ + pending_ >= opts_.maxPages) {
        return false;  // page cap reached
    }
    queue_.emplace(url, depth);
    ++pending_;
    if (!running_.load()) {
        running_.store(true);
        const std::size_t workers = std::max<std::size_t>(1, opts_.workerCount);
        for (std::size_t i = 0; i < workers; ++i) {
            workers_.emplace_back(&Crawler::workerLoop, this);
        }
    }
    cv_.notify_one();
    return true;
}

void Crawler::setCallbacks(OnDocument onDoc, OnError onErr) {
    std::lock_guard<std::mutex> lock(mtx_);
    onDoc_ = std::move(onDoc);
    onErr_ = std::move(onErr);
}

void Crawler::stop() {
    bool shouldJoin = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!stop_.exchange(true)) {
            shouldJoin = true;
        }
    }
    cv_.notify_all();
    if (shouldJoin) {
        for (std::thread& t : workers_) {
            if (t.joinable()) {
                t.join();
            }
        }
        workers_.clear();
        running_.store(false);
    }
}

std::size_t Crawler::pendingCount() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return pending_;
}

std::size_t Crawler::crawledCount() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return crawled_;
}

bool Crawler::running() const {
    return running_.load();
}

void Crawler::workerLoop() {
    for (;;) {
        std::pair<std::string, std::uint32_t> item;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return stop_.load() || !queue_.empty(); });
            if (queue_.empty()) {
                if (stop_.load()) {
                    return;
                }
                continue;
            }
            item = std::move(queue_.front());
            queue_.pop();
        }
        process(item.first, item.second);
        {
            std::lock_guard<std::mutex> lock(mtx_);
            --pending_;
            ++crawled_;
        }
    }
}

void Crawler::process(const std::string& url, std::uint32_t depth) {
    if (opts_.requestDelay.count() > 0) {
        std::this_thread::sleep_for(opts_.requestDelay);
    }

    FetchResult fetched = fetcher_->fetch(url);
    if (!fetched.ok) {
        OnError err;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            err = onErr_;
        }
        if (err) {
            err(url, fetched.error);
        }
        return;
    }

    Document doc;
    doc.url = url;
    doc.title = htmlExtractTitle(fetched.html);
    if (doc.title.empty()) {
        doc.title = url;
    }
    doc.snippet = htmlExtractDescription(fetched.html);
    doc.content = htmlStripTags(fetched.html);
    doc.depth = depth;
    const std::size_t hostStart = url.find("://") == std::string::npos ? 0 : url.find("://") + 3;
    doc.source = url.find(".onion", hostStart) != std::string::npos ? SourceType::Onion
                                                                    : SourceType::ClearWeb;

    const std::uint64_t id = index_.addDocument(doc);
    doc.id = id;

    OnDocument onDoc;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        onDoc = onDoc_;
    }
    if (onDoc) {
        onDoc(doc);
    }

    // Follow links when within the depth budget.
    if (depth + 1 <= opts_.maxDepth) {
        for (const std::string& link : htmlExtractLinks(fetched.html, url)) {
            if (!enqueue(link, depth + 1)) {
                break;  // cap reached or duplicate flood; stop scanning
            }
        }
    }
}

} // namespace shadowse
