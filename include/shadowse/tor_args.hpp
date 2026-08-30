// SPDX-License-Identifier: MIT
// Original author: Al-hassan shehade
// Shadow SE - shared Tor CLI flag parsing for all binaries.
#pragma once

#include "shadowse/engine.hpp"

#include <string>

namespace shadowse {

// Parses one Tor CLI flag into `cfg`. Returns true if `arg` was a Tor flag
// (in which case `value` holds the consumed next token, if any).
// Flags: --tor-pool, --tor-proxy <host:port,...>, --onion-retries N,
//        --circuit-rotate SECONDS
bool parseTorFlag(Engine::Config& cfg, const std::string& arg, const std::string& value);

} // namespace shadowse
