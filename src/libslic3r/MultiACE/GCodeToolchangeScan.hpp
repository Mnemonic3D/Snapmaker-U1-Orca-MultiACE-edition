#pragma once

#include <optional>
#include <string>
#include <vector>

// Ported from post_process_virtual_toolheads.py (parse_toolchanges,
// infer_num_aces, _is_extruding_move, _structural_inject_idx). See
// GCodeRewrite.hpp for the functions that consume these.

namespace Slic3r { namespace MultiACE {

// Yields the ORIGINAL T-index in order of appearance. Uses the
// "; Change Tool X -> Tool Y" comment as the source of truth for the target
// tool once a rewrite has run (the bare T<n> line then reads T<head>, head =
// original_T % 4). Falls back to bare T<n> lines only for gcode that hasn't
// been rewritten yet, so this works on either input.
std::vector<int> parse_toolchanges(const std::string& gcode);

// Detects how many ACEs the slicer's T-index assignment uses: canonical ACE
// for T<n> is n / 4, so inferred count = max(ACE) + 1 across all used T<n>
// commands (at least 1, so single-color prints still report ACE 0).
int infer_num_aces(const std::string& gcode);

// True for a G0/G1 move with a positive E value (i.e. it actually extrudes).
// Retracts (E<0) and non-move lines are false. This is the language/locale-
// independent boundary used to find where auto-load must be injected: before
// the first line that actually draws anything.
bool is_extruding_move(const std::string& line);

// Section boundary right before the first extruding move: the geometry-safe
// anchor for auto-load injection. Returns the index of the nearest preceding
// blank line or ";===== ... =====" section header (searching back at most
// 200 lines), or the extruding-move index itself if no such boundary is
// found nearby. std::nullopt if the gcode never extrudes at all.
std::optional<size_t> structural_inject_idx(const std::vector<std::string>& lines);

} } // namespace Slic3r::MultiACE
