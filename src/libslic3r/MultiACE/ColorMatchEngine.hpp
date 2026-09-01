#pragma once

#include "MultiAceTypes.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Ported from post_process_virtual_toolheads.py's match_colors_to_slots()
// (~211 lines) and _find_color_match(). This is the single most
// order-sensitive piece of the whole tool: which physical slot wins a tie
// depends on iterating T-indices in ascending order and slots in their
// original arrival order - see ColorMatchEngine.cpp's comments before
// changing any container type here.

namespace Slic3r { namespace MultiACE {

struct MatchInfo {
    std::string tier;              // "exact_hex" | "name_exact" | "name_base" |
                                    // "name_canon" | "fuzzy" | "fallback" |
                                    // "duplicate" | "no_slot" (or a "loose_"
                                    // prefixed variant - see .cpp, currently
                                    // unreachable in practice, ported anyway
                                    // for fidelity with the Python source)
    std::optional<Slot> slot;
    bool loose_mat = false;
};

struct MatchResult {
    std::map<int, int> remap;              // original_T -> synthetic_T, omits no-ops
    std::map<int, MatchInfo> info;         // original_T -> match detail
    std::set<std::pair<int, int>> used_slots; // (ace, slot) claimed
};

// Build a remap {original_T -> synthetic_T} for the rewrite formula
// (ace = T / num_heads, slot = T % num_heads), choosing for each slicer
// T-index the physical slot whose colour best matches. Tier-major: every
// still-unmatched T is tried against a whole tier before the next tier runs,
// so an earlier/better T doesn't lose a good match to a later T's fallback.
// See the Python docstring (post_process_virtual_toolheads.py:565) for the
// full tier-order rationale; ported verbatim here, not re-derived.
MatchResult match_colors_to_slots(
    const std::map<int, std::string>& color_names,
    const std::vector<Slot>& live_slots,
    int num_heads = 4,
    const std::map<int, std::string>& filament_types = {},
    bool strict_color = false,
    std::optional<double> fuzzy_max_distance = std::nullopt);

} } // namespace Slic3r::MultiACE
