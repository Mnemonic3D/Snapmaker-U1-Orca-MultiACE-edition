#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Plain data types shared between the pure-logic MultiACE modules
// (libslic3r, no curl dependency) and the live-host client
// (slic3r/Utils/MultiACE, which does the actual HTTP fetch). Deliberately
// has no networking/JSON-library dependency itself so it can be used and
// unit-tested from either layer.

namespace Slic3r { namespace MultiACE {

// One occupied ACE slot, from /multiace/api/state's "aces[].slots[]".
struct Slot {
    int ace = 0;
    int slot = 0;
    std::string material;   // already lowercased/trimmed
    std::string color;      // '#rrggbb', lowercased/trimmed
};

// One loaded feeder head, from /multiace/api/state's "toolheads[]" where
// feeder=true and filament_detected=true.
struct Feeder {
    int head = 0;
    std::string material;   // trimmed, original case
    std::string color;      // trimmed, original case
};

// Head-mode context: whether the printer is in "head" mode (feeders pinned
// to fixed heads + one ACE multiplexed on the rest), the loaded feeders, and
// the ACE/head wiring. Mirrors lookup_head_mode_context()'s return dict. A
// non-"head" mode still populates this - callers must check `mode`.
struct HeadModeContext {
    std::string mode = "normal";
    std::vector<Feeder> feeders;
    std::vector<int> ace_heads;          // sorted, deduplicated
    std::map<int, int> head_ace;         // physical head -> ACE index, for heads 0-3
};

// Everything derived from ONE /multiace/api/state fetch. The Python source
// made 3 separate HTTP calls to this same endpoint (lookup_live_slots,
// lookup_head_mode_context, host_has_manual_head each fetched independently)
// - the C++ client fetches once and derives all three views from it.
struct LiveState {
    std::vector<Slot> slots;
    HeadModeContext head_mode;
    bool has_manual_head = false;
};

// Pre-check before matching: which materials (lowercased) the slicer needs
// but that aren't loaded in any slot on the printer. Empty means every
// required material has at least one slot available - matching can proceed
// even if individual colours end up falling back to a looser tier.
std::set<std::string> check_material_availability(
    const std::map<int, std::string>& filament_types,
    const std::vector<Slot>& live_slots);

} } // namespace Slic3r::MultiACE
