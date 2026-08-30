// SPDX-License-Identifier: MIT
// Original author: Al-hassan shehade
// Shadow SE - internal Tor helpers shared by the manager implementations.
#pragma once

#include "shadowse/tor_proxy_manager.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace shadowse::detail {

bool isOnionUrl(const std::string& url);

// Exponential backoff with jitter: min(cap, base * 2^attempt) + uniform(0, base).
std::chrono::milliseconds backoffMs(std::size_t attempt, double baseMs, double capMs);

// Probes endpoints in order; Up if any is reachable, else Down.
TorProbeResult aggregateProbe(const std::vector<TorEndpoint>& eps,
                              std::chrono::milliseconds timeout);

// Deterministic circuit-isolation username for a URL (FNV-1a -> hex).
std::string circuitUsernameFor(const std::string& url, CircuitScope scope);

} // namespace shadowse::detail
