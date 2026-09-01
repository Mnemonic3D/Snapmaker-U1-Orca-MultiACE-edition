#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Ported from post_process_virtual_toolheads.py: the named-color table and
// the hex/rgb/formatting helpers used throughout the matching engine and the
// printed loadout report. Three near-identical hex-to-rgb implementations in
// the Python source were deduplicated into the single hex_to_rgb() here.

namespace Slic3r { namespace MultiACE {

// (name, RGB) in the exact order used for nearest-color tie-breaking - do not
// reorder without checking approx_color_name's "first strictly-closer match
// wins" semantics.
extern const std::vector<std::pair<std::string, std::array<int, 3>>>& named_colors();

// ('#rrggbb' or 'rrggbb', either case) -> (r, g, b), or nullopt if not a
// parseable 6+ hex-digit string.
std::optional<std::array<int, 3>> hex_to_rgb(const std::string& hex_str);

// 'DarkRed' -> 'Red', 'LightBlue' -> 'Blue', otherwise unchanged. Empty
// input returns empty.
std::string strip_color_qualifier(const std::string& name);

// Applies the synonym table on top of an already-qualifier-stripped base
// name: 'Silver' -> 'Gray', 'Gold' -> 'Yellow', otherwise unchanged. Used by
// the matching engine's "canon" tier (base name -> canonical synonym).
std::string canonicalize_color_base(const std::string& base);

// Nearest named color from a hex string (squared-Euclidean RGB distance), or
// the original hex string unchanged if it isn't parseable as a color.
std::string approx_color_name(const std::string& hex_str);

// '#831100' -> '#831100 rgb(131,17,0)'-style string for pasting back into the
// slicer. Returns the input (or '?') unchanged if not parseable.
std::string format_color_hex_rgb(const std::string& hex_str);

// Combines approx_color_name + format_color_hex_rgb for a T-index's color,
// e.g. 'Red (#ff0000 RGB(255,0,0))', or just the hex form if the nearest
// named color equals the hex itself. '?' if the tool has no known color.
std::string format_color(int t_index, const std::map<int, std::string>& color_names);

} } // namespace Slic3r::MultiACE
