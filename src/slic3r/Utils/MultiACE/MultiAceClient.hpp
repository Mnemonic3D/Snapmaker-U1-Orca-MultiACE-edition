#pragma once

#include "libslic3r/MultiACE/MultiAceTypes.hpp"

#include <optional>
#include <string>

// Thin HTTP wrapper around the pure JSON parsing in
// libslic3r/MultiACE/MultiAceStateParser.hpp - deliberately kept minimal
// (fetch the URL, hand the body to the already-verified parser) since the
// parsing logic is where the real risk lives, not this transport layer.
// Lives under src/slic3r/ (not libslic3r/) because Http.hpp/libcurl is only
// linked into libslic3r_gui, not the base libslic3r target.

namespace Slic3r { namespace MultiACE {

// Fetches /multiace/api/state and parses it into a LiveState. Consolidates
// what were 3 separate Python HTTP calls to this same endpoint
// (lookup_live_slots, lookup_head_mode_context, host_has_manual_head) into
// one round trip - they all read the same static snapshot. `host` may
// include ":port", overriding the `port` argument (matches the Python
// source's host.partition(':') handling). Returns std::nullopt on any
// connection/timeout/non-200/parse failure, matching the Python functions'
// "return None on error" contract.
std::optional<LiveState> fetch_live_state(
    const std::string& host,
    int port = 80,
    const std::string& path = "/multiace/api/state",
    long timeout_connect_s = 3,
    long timeout_max_s = 6);

} } // namespace Slic3r::MultiACE
