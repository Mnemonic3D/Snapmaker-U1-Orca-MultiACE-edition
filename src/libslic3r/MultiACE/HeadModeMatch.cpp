#include "HeadModeMatch.hpp"
#include "ColorMatch.hpp"
#include "ColorMatchEngine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <set>

namespace Slic3r { namespace MultiACE {

namespace {

std::string trim_lower(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    std::string t = s.substr(b, e - b);
    std::transform(t.begin(), t.end(), t.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return t;
}

std::string trim_lower_strip_hash(const std::string& s)
{
    std::string t = trim_lower(s);
    size_t p = 0;
    while (p < t.size() && t[p] == '#') ++p;
    return t.substr(p);
}

struct PinCandidate {
    int head;
    std::string color; // normalized: lowercase, no '#'
};

// Ported from _find_color_match(). candidates/slicer_color are expected
// pre-normalized (lowercase, no '#') by the caller, same contract as the
// Python source.
std::pair<std::optional<PinCandidate>, std::string> find_color_match(
    const std::vector<PinCandidate>& candidates,
    const std::string& slicer_color,
    bool strict_color,
    std::optional<double> fuzzy_max_distance)
{
    if (slicer_color.empty())
        return {std::nullopt, ""};

    for (const auto& s : candidates)
        if (s.color == slicer_color)
            return {s, "exact_hex"};
    if (strict_color)
        return {std::nullopt, ""};

    std::string slicer_name = approx_color_name("#" + slicer_color);
    if (!slicer_name.empty() && slicer_name != "?") {
        std::string slicer_base = strip_color_qualifier(slicer_name);
        std::string slicer_canon = canonicalize_color_base(slicer_base);

        static const std::array<const char*, 3> tiers = {"name_exact", "name_base", "name_canon"};
        for (int stage = 0; stage < 3; ++stage) {
            for (const auto& s : candidates) {
                std::string slot_name = approx_color_name("#" + s.color);
                if (slot_name.empty() || slot_name == "?")
                    continue;
                bool ok;
                if (stage == 0) {
                    ok = (slot_name == slicer_name);
                } else if (stage == 1) {
                    ok = (strip_color_qualifier(slot_name) == slicer_base);
                } else {
                    std::string slot_base = strip_color_qualifier(slot_name);
                    ok = (canonicalize_color_base(slot_base) == slicer_canon);
                }
                if (ok)
                    return {s, tiers[stage]};
            }
        }
    }

    if (fuzzy_max_distance) {
        auto slicer_rgb = hex_to_rgb(slicer_color);
        if (slicer_rgb) {
            std::optional<PinCandidate> best;
            std::optional<long long> best_d;
            for (const auto& s : candidates) {
                auto sr = hex_to_rgb(s.color);
                if (!sr) continue;
                long long dr = (*sr)[0] - (*slicer_rgb)[0];
                long long dg = (*sr)[1] - (*slicer_rgb)[1];
                long long db = (*sr)[2] - (*slicer_rgb)[2];
                long long d2 = dr * dr + dg * dg + db * db;
                if (!best_d || d2 < *best_d) {
                    best_d = d2;
                    best = s;
                }
            }
            if (best && std::sqrt(static_cast<double>(*best_d)) <= *fuzzy_max_distance)
                return {best, "fuzzy"};
        }
    }
    return {std::nullopt, ""};
}

} // namespace

HeadModeLayout compute_head_mode_layout(
    const std::map<int, std::string>& slicer_colors,
    const std::map<int, std::string>& slicer_types,
    const std::vector<PinnedHead>& pinned_heads,
    const std::vector<Slot>& ace_slots,
    const std::map<int, int>& ace_head_of_ace,
    std::optional<double> fuzzy_max_distance)
{
    // pins: order-preserving on first-seen head, last-value-wins on a
    // duplicate head - matches Python's `pins[int(p['head'])] = p` inside a
    // loop (dict key position is fixed at first insertion, value updates).
    std::vector<int> pin_order;
    std::map<int, PinnedHead> pins_by_head;
    for (const auto& p : pinned_heads) {
        if (pins_by_head.find(p.head) == pins_by_head.end())
            pin_order.push_back(p.head);
        pins_by_head[p.head] = p;
    }

    std::map<int, AssignmentEntry> assignment;
    std::set<int> pinned_t;

    for (const auto& kv : slicer_colors) {
        int t = kv.first;
        std::string c = trim_lower_strip_hash(kv.second);
        if (c.empty())
            continue;
        auto st_it = slicer_types.find(t);
        std::string mat = (st_it != slicer_types.end()) ? trim_lower(st_it->second) : std::string();

        std::vector<PinCandidate> cands;
        for (int head : pin_order) {
            const PinnedHead& p = pins_by_head.at(head);
            std::string pmat = trim_lower(p.material);
            if (!mat.empty() && !pmat.empty() && pmat != mat)
                continue;
            cands.push_back({head, trim_lower_strip_hash(p.color)});
        }

        auto match_result = find_color_match(cands, c, false, fuzzy_max_distance);
        if (match_result.first) {
            AssignmentEntry e;
            e.kind = "pin";
            e.head = match_result.first->head;
            e.tier = match_result.second;
            assignment[t] = e;
            pinned_t.insert(t);
        }
    }

    std::vector<Slot> usable_slots;
    for (const auto& s : ace_slots)
        if (ace_head_of_ace.find(s.ace) != ace_head_of_ace.end())
            usable_slots.push_back(s);

    std::map<int, std::string> rest_colors;
    for (const auto& kv : slicer_colors)
        if (pinned_t.find(kv.first) == pinned_t.end())
            rest_colors[kv.first] = kv.second;

    std::map<int, std::string> rest_types;
    for (const auto& kv : rest_colors) {
        auto it = slicer_types.find(kv.first);
        rest_types[kv.first] = (it != slicer_types.end()) ? it->second : std::string();
    }

    MatchResult mr = match_colors_to_slots(rest_colors, usable_slots, 4, rest_types, false, fuzzy_max_distance);
    for (const auto& kv : mr.info) {
        int t = kv.first;
        const MatchInfo& entry = kv.second;
        AssignmentEntry e;
        if (!entry.slot) {
            e.kind = "none";
            e.tier = entry.tier.empty() ? "no_slot" : entry.tier;
        } else {
            e.kind = "ace";
            e.head = ace_head_of_ace.at(entry.slot->ace);
            e.ace = entry.slot->ace;
            e.slot = entry.slot->slot;
            e.tier = entry.tier;
        }
        assignment[t] = e;
    }

    HeadModeLayout result;
    result.assignment = assignment;
    for (const auto& kv : assignment)
        if (kv.second.kind == "none")
            result.infeasible.push_back(kv.first);
    result.feasible = result.infeasible.empty();

    std::set<int> pinned_set;
    for (const auto& kv : assignment)
        if (kv.second.kind == "pin")
            pinned_set.insert(kv.second.head);
    result.pinned.assign(pinned_set.begin(), pinned_set.end());

    std::set<int> ace_heads_set;
    for (const auto& kv : ace_head_of_ace)
        ace_heads_set.insert(kv.second);
    result.ace_heads.assign(ace_heads_set.begin(), ace_heads_set.end());

    return result;
}

} } // namespace Slic3r::MultiACE
