#pragma once

#include "MultiAceTypes.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

// Ported from post_process_virtual_toolheads.py's compute_head_mode_layout()
// and its helper _find_color_match(). Head mode: K ACE-driven heads each
// multiplex colours via slot-swaps on their own ACE, while feeder heads are
// PINNED to a single fixed colour each (no swap) - this is what a real U1
// toolchanger + ACE hybrid actually needs, as opposed to the simpler
// single-ACE match_colors_to_slots() (ColorMatchEngine.hpp).

namespace Slic3r { namespace MultiACE {

// A feeder head's loaded identity, as reported by the printer.
struct PinnedHead {
    int head = 0;
    std::string material;
    std::string color;  // with or without leading '#', any case - normalized internally
};

struct AssignmentEntry {
    std::string kind;   // "pin" | "ace" | "none"
    int head = 0;        // meaningful for "pin" and "ace"
    int ace = -1;         // meaningful for "ace" only
    int slot = -1;        // meaningful for "ace" only
    std::string tier;
};

struct HeadModeLayout {
    std::map<int, AssignmentEntry> assignment;
    bool feasible = true;
    std::vector<int> infeasible;    // sorted T-indices with kind == "none"
    std::vector<int> pinned;        // sorted feeder heads actually used
    std::vector<int> ace_heads;     // sorted ACE head indices
};

// slicer_colors: {t: 'rrggbb'} slicer tool colour per used T.
// slicer_types: {t: material} slicer material per T (material-strict).
// pinned_heads: the feeder heads' loaded identity; a slicer colour that
//   matches one (material-strict) pins to that head.
// ace_slots: all loaded ACE slots (from any ACE, wired or not).
// ace_head_of_ace: {ace_index: head} - which physical head each ACE feeds.
//   Only slots on a wired ACE are usable.
HeadModeLayout compute_head_mode_layout(
    const std::map<int, std::string>& slicer_colors,
    const std::map<int, std::string>& slicer_types,
    const std::vector<PinnedHead>& pinned_heads,
    const std::vector<Slot>& ace_slots,
    const std::map<int, int>& ace_head_of_ace,
    std::optional<double> fuzzy_max_distance = std::nullopt);

} } // namespace Slic3r::MultiACE
