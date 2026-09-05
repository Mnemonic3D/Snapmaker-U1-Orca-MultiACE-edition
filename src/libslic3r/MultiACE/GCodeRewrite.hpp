#pragma once

#include "HeadModeMatch.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

// Ported from post_process_virtual_toolheads.py's rewrite(),
// apply_head_mode_rewrite(), apply_remap(), inject_auto_load() - the actual
// gcode text surgery that turns tool changes into real ACE_SWAP_HEAD
// commands. Highest-stakes module in the port: a bug here means the wrong
// filament gets extruded on a real print, discovered only mid-print.

namespace Slic3r { namespace MultiACE {

struct RewriteResult {
    std::string gcode;
    int active_swaps = 0;
    int skipped = 0;
    int swapbacks = 0;
};

// Normal-mode rewrite: expands synthetic T-indices (T4-T15, encoding
// ace=n/4, slot=n%4) into a real T<head> + ACE_SWAP_HEAD pair, skips
// redundant swaps already reflecting the current head_loaded state, and
// comments out (rather than deletes) swaps that turn out to be no-ops once
// the head is already correctly loaded. feeder_heads never get an
// ACE_SWAP_HEAD - they're pinned, not ACE-fed.
RewriteResult rewrite(const std::string& gcode, const std::set<int>& feeder_heads = {});

// Head-mode-safe rewrite: walks the gcode using the ORIGINAL slicer T-index
// (read off "; Change Tool X -> Tool Y" comments) and looks up its real
// (kind, head, ace, slot) directly in `assignment` for every substitution -
// never round-trips through the ace*4+slot encoding rewrite() uses, so an
// ACE colour and a feeder colour sharing the same low integer can never be
// confused with each other. Only rewrites the print BODY (from the first
// "; Change Tool" marker onward); returns (gcode, active_swaps unchanged)
// if there's no such marker at all.
std::pair<std::string, int> apply_head_mode_rewrite(
    const std::string& gcode,
    const std::map<int, AssignmentEntry>& assignment);

// Rewrites every T-index reference (bare T<n> lines, M104/M109 T<n> heater
// commands, SM_PRINT_PREEXTRUDE_FILAMENT INDEX=<n>) per the permutation
// `remap` ({old_T: new_T}). "; Change Tool" comments are left untouched -
// they stay the canonical source of the original slicer indices.
std::string apply_remap(const std::string& gcode, const std::map<int, int>& remap);

// Inserts ACE_SWAP_HEAD calls for every head actually used in the gcode, at
// the safest point past G28+heating but before the first move needing the
// initial tool's filament (a 4-tier anchor fallback chain - see .cpp).
// feeder_heads are skipped (no ACE wiring, nothing to preload).
// initial_targets, if given, provides the real (ace,slot) a head needs when
// it can't be discovered by scanning the gcode itself. Returns
// (gcode_with_injection, count_of_heads_loaded).
std::pair<std::string, int> inject_auto_load(
    const std::string& gcode,
    const std::set<int>& feeder_heads = {},
    const std::optional<std::map<int, std::pair<int, int>>>& initial_targets = std::nullopt);

// One correction made by fix_toolchange_temperatures() below; line_no is
// 1-indexed, matching the Python source's own reporting convention.
struct TemperatureFix {
    int line_no = 0;
    int t_num = 0;
    int old_temp = 0;
    int new_temp = 0;
};

// Ported from post_process_virtual_toolheads.py's fix_toolchange_temperatures().
// Rewrites the first two bare M109/M104 S<temp> lines following every bare
// toolchange (T<n>) to that physical head's own configured
// `nozzle_temperature` (read from this gcode's own header comment array),
// replacing whatever flush/purge and settle temperature Orca's own
// wipe-tower logic originally computed for the PRE-remap tool assignment.
//
// Root cause this corrects (confirmed against a real print, not guessed):
// Orca computes each toolchange's temperatures from the *original* slicer
// extruder assignment, before rewrite()/apply_head_mode_rewrite()/
// inject_auto_load() above ever remap which physical head/ACE-slot actually
// executes that toolchange. Once remapped, those baked-in temperatures no
// longer relate to the material actually about to be extruded - e.g. a head
// configured for "PETG HIGH SPEED" (250C) was measured purging at 270C, a
// head configured for plain PETG (245C) at 240C, on every toolchange.
//
// Deliberately layout-agnostic: reads only the gcode's own header array,
// never ace_heads/feeder assignment - correct regardless of ACE wiring
// (verified against all 16 possible 4-head ACE-layout combinations, not
// just this project's own T0-only printer).
//
// A real toolchange block has THREE bare temperature commands, not one:
//   1. M109 S<n> right after T<n> - the purge/flush temp (waits). Fixed.
//   2. M104 S<n> ~30 lines later, inside "; CP TOOLCHANGE LOAD" - settles to
//      the SUSTAINED print temp until the next swap. Same bug, same fix.
//   3. A THIRD M104 S<n> inside the NEXT block's own "; Ramming start"
//      section (winding the outgoing material down ahead of the next swap) -
//      deliberately NOT touched: its purpose isn't fully characterized and
//      "fixing" it risks fighting Orca's own timing rather than correcting
//      a confirmed bug. The search window below stops at (never crosses)
//      the next "; Ramming start" boundary specifically to guarantee this
//      third occurrence is never reached.
//
// Must run LAST, after every other rewrite/remap/inject_auto_load step -
// mirrors main()'s own call order in the Python source exactly, since only
// the FINAL bare T<n> in the gcode reflects the actually-executing head.
std::pair<std::string, std::vector<TemperatureFix>> fix_toolchange_temperatures(const std::string& gcode);

} } // namespace Slic3r::MultiACE
