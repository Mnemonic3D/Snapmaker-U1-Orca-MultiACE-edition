#include "ColorMatchEngine.hpp"
#include "ColorMatch.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>

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

// (s or '').strip().lower().lstrip('#')
std::string trim_lower_strip_hash(const std::string& s)
{
    std::string t = trim_lower(s);
    size_t p = 0;
    while (p < t.size() && t[p] == '#') ++p;
    return t.substr(p);
}

bool parse_hex_byte(const std::string& s, size_t offset, int& out)
{
    if (offset + 2 > s.size())
        return false;
    std::string pair = s.substr(offset, 2);
    size_t pos = 0;
    try {
        out = std::stoi(pair, &pos, 16);
    } catch (...) {
        return false;
    }
    return pos == 2;
}

// Local hex parser matching the Python closure's own _hex_to_rgb - operates
// on an already-lowercased, hash-stripped string (unlike ColorMatch::hex_to_rgb,
// which does its own stripping from raw input).
std::optional<std::array<int, 3>> hex_to_rgb_local(const std::string& lowered_no_hash)
{
    if (lowered_no_hash.size() < 6)
        return std::nullopt;
    int r, g, b;
    if (!parse_hex_byte(lowered_no_hash, 0, r) ||
        !parse_hex_byte(lowered_no_hash, 2, g) ||
        !parse_hex_byte(lowered_no_hash, 4, b))
        return std::nullopt;
    return std::array<int, 3>{r, g, b};
}

struct TMeta {
    std::string color, mat, name, base, canon;
    std::optional<std::array<int, 3>> rgb;
};

struct SlotMeta {
    Slot slot;
    std::string color, mat, name, base, canon;
    std::optional<std::array<int, 3>> rgb;
};

// (name, base, canon) for a '#rrggbb'-or-empty string, using the
// already-verified ColorMatch helpers. Empty triple if unresolvable.
void name_keys(const std::string& hex_with_hash, std::string& name, std::string& base, std::string& canon)
{
    name.clear(); base.clear(); canon.clear();
    if (hex_with_hash.empty())
        return;
    std::string n = approx_color_name(hex_with_hash);
    if (n.empty() || n == "?")
        return;
    name = n;
    base = strip_color_qualifier(n);
    canon = canonicalize_color_base(base);
}

long long sq_dist(const std::array<int, 3>& a, const std::array<int, 3>& b)
{
    long long dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
    return dr * dr + dg * dg + db * db;
}

} // namespace

MatchResult match_colors_to_slots(
    const std::map<int, std::string>& color_names,
    const std::vector<Slot>& live_slots,
    int num_heads,
    const std::map<int, std::string>& filament_types,
    bool strict_color,
    std::optional<double> fuzzy_max_distance)
{
    MatchResult result;

    // t_meta: std::map keeps ascending-T order automatically, matching
    // Python's `for t in sorted(color_names.keys())`.
    std::map<int, TMeta> t_meta;
    for (const auto& kv : color_names) {
        int t = kv.first;
        std::string c = trim_lower_strip_hash(kv.second);
        auto ft_it = filament_types.find(t);
        std::string mat = (ft_it != filament_types.end()) ? trim_lower(ft_it->second) : std::string();

        TMeta tm;
        tm.color = c;
        tm.mat = mat;
        if (!c.empty()) {
            name_keys("#" + c, tm.name, tm.base, tm.canon);
            tm.rgb = hex_to_rgb_local(c);
        }
        t_meta[t] = std::move(tm);
    }

    // slot_meta: std::vector preserves live_slots' original arrival order,
    // which matters for the fallback/duplicate passes below (they take the
    // FIRST matching slot in this order, not a "best" one).
    std::vector<SlotMeta> slot_meta;
    slot_meta.reserve(live_slots.size());
    for (const auto& s : live_slots) {
        std::string c = trim_lower_strip_hash(s.color);
        SlotMeta sm;
        sm.slot = s;
        sm.color = c;
        // NOTE: Python only .lower()s the slot's material here, no .strip() -
        // unlike t_meta's mat above, which does strip. Kept asymmetric on
        // purpose to match the source exactly.
        std::string m = s.material;
        std::transform(m.begin(), m.end(), m.begin(),
                        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        sm.mat = m;
        if (!c.empty()) {
            name_keys("#" + c, sm.name, sm.base, sm.canon);
            sm.rgb = hex_to_rgb_local(c);
        }
        slot_meta.push_back(std::move(sm));
    }

    std::set<std::pair<int, int>> used;
    std::map<int, MatchInfo> info;
    std::vector<int> pending;
    for (const auto& kv : t_meta) pending.push_back(kv.first);

    auto is_used = [&](const Slot& s) {
        return used.find({s.ace, s.slot}) != used.end();
    };

    auto candidate_slots = [&](int t, bool strict_mat) {
        std::vector<const SlotMeta*> out;
        const std::string& t_mat = t_meta.at(t).mat;
        for (const auto& sm : slot_meta) {
            if (is_used(sm.slot)) continue;
            if (strict_mat && !t_mat.empty() && sm.mat != t_mat) continue;
            out.push_back(&sm);
        }
        return out;
    };

    using Predicate = std::function<bool(const TMeta&, const SlotMeta&)>;

    auto match_pass = [&](const std::string& tier_name, bool strict_mat, const Predicate& predicate) {
        std::vector<int> snapshot = pending; // Python's `list(pending)` before mutating it
        for (int t : snapshot) {
            const TMeta& tm = t_meta.at(t);
            const SlotMeta* chosen = nullptr;
            for (const SlotMeta* sm : candidate_slots(t, strict_mat)) {
                if (predicate(tm, *sm)) { chosen = sm; break; }
            }
            if (!chosen) continue;
            const Slot& s = chosen->slot;
            used.insert({s.ace, s.slot});
            MatchInfo mi;
            mi.tier = strict_mat ? tier_name : ("loose_" + tier_name);
            mi.slot = s;
            mi.loose_mat = (!strict_mat) && !tm.mat.empty();
            info[t] = mi;
            pending.erase(std::remove(pending.begin(), pending.end(), t), pending.end());
        }
    };

    Predicate fuzzy_predicate = [&](const TMeta& tm, const SlotMeta& sm) {
        if (!fuzzy_max_distance) return false;
        if (!tm.rgb || !sm.rgb) return false;
        long long d2 = sq_dist(*tm.rgb, *sm.rgb);
        return std::sqrt(static_cast<double>(d2)) <= *fuzzy_max_distance;
    };

    struct TierSpec { std::string name; bool skip_when_strict; Predicate pred; };
    std::vector<TierSpec> color_tiers = {
        {"exact_hex",  false, [](const TMeta& tm, const SlotMeta& sm) {
            return !tm.color.empty() && tm.color == sm.color; }},
        {"name_exact", true,  [](const TMeta& tm, const SlotMeta& sm) {
            return !tm.name.empty()  && tm.name  == sm.name; }},
        {"name_base",  true,  [](const TMeta& tm, const SlotMeta& sm) {
            return !tm.base.empty()  && tm.base  == sm.base; }},
        {"name_canon", true,  [](const TMeta& tm, const SlotMeta& sm) {
            return !tm.canon.empty() && tm.canon == sm.canon; }},
        {"fuzzy",      true,  fuzzy_predicate},
    };

    // Tier-major: every tier is run to completion (against every still-pending
    // T) before the next tier starts. All passes use strict_mat=true here -
    // the "loose_" tier label in match_pass is unreachable via this call site
    // (kept for fidelity with the Python source rather than trimmed as dead code).
    for (const auto& tier : color_tiers) {
        if (tier.skip_when_strict && strict_color) continue;
        if (tier.name == "fuzzy" && !fuzzy_max_distance) continue;
        match_pass(tier.name, true, tier.pred);
    }

    // Fallback: first unclaimed slot of the same material (or any slot if
    // the T has no material), in slot_meta's original order.
    {
        std::vector<int> snapshot = pending;
        for (int t : snapshot) {
            const std::string& t_mat = t_meta.at(t).mat;
            const SlotMeta* chosen = nullptr;
            for (const auto& sm : slot_meta) {
                if (is_used(sm.slot)) continue;
                if (!t_mat.empty() && !sm.mat.empty() && sm.mat != t_mat) continue;
                chosen = &sm;
                break;
            }
            if (!chosen) continue;
            const Slot& s = chosen->slot;
            used.insert({s.ace, s.slot});
            MatchInfo mi;
            mi.tier = "fallback";
            mi.slot = s;
            mi.loose_mat = false;
            info[t] = mi;
            pending.erase(std::remove(pending.begin(), pending.end(), t), pending.end());
        }
    }

    // Duplicate: share an already-claimed same-material slot, nearest by RGB
    // distance, else the first eligible one. `already` is computed once
    // against `used` as it stands now - this pass never adds to `used`.
    if (!pending.empty()) {
        std::vector<const SlotMeta*> already;
        for (const auto& sm : slot_meta)
            if (used.find({sm.slot.ace, sm.slot.slot}) != used.end())
                already.push_back(&sm);

        std::vector<int> snapshot = pending;
        for (int t : snapshot) {
            const TMeta& tm = t_meta.at(t);
            const std::string& t_mat = tm.mat;
            std::vector<const SlotMeta*> candidates;
            for (const auto* sm : already)
                if (t_mat.empty() || sm->mat.empty() || sm->mat == t_mat)
                    candidates.push_back(sm);

            const SlotMeta* best = nullptr;
            std::optional<long long> best_d;
            if (tm.rgb) {
                for (const auto* sm : candidates) {
                    if (!sm->rgb) continue;
                    long long d2 = sq_dist(*tm.rgb, *sm->rgb);
                    if (!best_d || d2 < *best_d) {
                        best_d = d2;
                        best = sm;
                    }
                }
            }
            if (!best && !candidates.empty())
                best = candidates.front();

            MatchInfo mi;
            if (!best) {
                mi.tier = "no_slot";
                mi.slot = std::nullopt;
            } else {
                mi.tier = "duplicate";
                mi.slot = best->slot;
            }
            mi.loose_mat = false;
            info[t] = mi;
            pending.erase(std::remove(pending.begin(), pending.end(), t), pending.end());
        }
    }

    for (const auto& kv : info) {
        int t = kv.first;
        const MatchInfo& entry = kv.second;
        if (!entry.slot) continue;
        int synthetic_T = entry.slot->ace * num_heads + entry.slot->slot;
        if (synthetic_T != t)
            result.remap[t] = synthetic_T;
    }
    result.info = std::move(info);
    result.used_slots = std::move(used);
    return result;
}

} } // namespace Slic3r::MultiACE
