#include "MultiAceStateParser.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace Slic3r { namespace MultiACE {

namespace {

std::string trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string trim_lower(const std::string& s)
{
    std::string t = trim(s);
    std::transform(t.begin(), t.end(), t.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return t;
}

// Never throws regardless of the field's actual JSON type - a malformed or
// unexpected API response must not be able to crash Orca's process just
// because it's running in-process now instead of as a separate script that
// could fail in isolation.
std::string str_field(const nlohmann::json& obj, const std::string& key)
{
    if (!obj.is_object())
        return {};
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string())
        return {};
    return it->get<std::string>();
}

int int_field(const nlohmann::json& obj, const std::string& key, int fallback)
{
    if (!obj.is_object())
        return fallback;
    auto it = obj.find(key);
    if (it == obj.end())
        return fallback;
    if (it->is_number())
        return it->get<int>();
    if (it->is_string()) {
        // Python's int(x) also accepts a numeric string (e.g. head_ace's
        // values) - strip and require the whole string to be the number,
        // matching int()'s all-or-nothing parse (no trailing-garbage tolerance).
        std::string s = trim(it->get<std::string>());
        if (!s.empty()) {
            try {
                size_t pos = 0;
                int v = std::stoi(s, &pos);
                if (pos == s.size())
                    return v;
            } catch (...) {
            }
        }
    }
    return fallback;
}

// Python truthiness for a JSON field accessed via .get(key): missing, null,
// false, 0, "", [], {} are all falsy; everything else is truthy.
bool truthy_field(const nlohmann::json& obj, const std::string& key)
{
    if (!obj.is_object())
        return false;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null())
        return false;
    if (it->is_boolean())
        return it->get<bool>();
    if (it->is_number())
        return it->get<double>() != 0.0;
    if (it->is_string())
        return !it->get<std::string>().empty();
    if (it->is_array() || it->is_object())
        return !it->empty();
    return true;
}

} // namespace

LiveState parse_state_response(const nlohmann::json& data)
{
    LiveState result;
    if (!data.is_object())
        return result;

    // --- occupied ACE slots (was lookup_live_slots) ---
    auto aces_it = data.find("aces");
    if (aces_it != data.end() && aces_it->is_array()) {
        for (const auto& ace : *aces_it) {
            if (!ace.is_object())
                continue;
            int ace_idx = int_field(ace, "idx", 0);

            auto slots_it = ace.find("slots");
            if (slots_it == ace.end() || !slots_it->is_array())
                continue;
            for (const auto& slot : *slots_it) {
                if (!slot.is_object())
                    continue;
                if (str_field(slot, "state") == "empty")
                    continue;
                if (slot.contains("source")) {
                    std::string src = str_field(slot, "source");
                    if (src != "rfid" && src != "override")
                        continue;
                }
                std::string color = trim_lower(str_field(slot, "color"));
                std::string material = trim(str_field(slot, "material"));
                if (!color.empty() || !material.empty()) {
                    Slot s;
                    s.ace = ace_idx;
                    s.slot = int_field(slot, "idx", 0);
                    s.material = material;
                    s.color = color;
                    result.slots.push_back(std::move(s));
                }
            }
        }
    }

    // --- mode + feeders + manual-head flag (was lookup_head_mode_context + host_has_manual_head) ---
    result.head_mode.mode = str_field(data, "mode");
    if (result.head_mode.mode.empty())
        result.head_mode.mode = "normal";

    auto toolheads_it = data.find("toolheads");
    if (toolheads_it != data.end() && toolheads_it->is_array()) {
        for (const auto& th : *toolheads_it) {
            if (!th.is_object())
                continue;
            if (truthy_field(th, "manual"))
                result.has_manual_head = true;

            if (!truthy_field(th, "feeder"))
                continue;
            if (!truthy_field(th, "filament_detected"))
                continue;
            std::string mat = trim(str_field(th, "material"));
            std::string col = trim(str_field(th, "color"));
            if (mat.empty() && col.empty())
                continue;
            // Python does th['idx'] here (KeyError -> crash if missing);
            // skip the entry instead of throwing out of the slicer process.
            if (!th.contains("idx") || !th["idx"].is_number())
                continue;
            Feeder f;
            f.head = th["idx"].get<int>();
            f.material = mat;
            f.color = col;
            result.head_mode.feeders.push_back(std::move(f));
        }
    }

    // ace_heads: use the array if present and non-empty, else fall back to
    // [ace_head or 3] - matches (data.get('ace_heads') or []) / (... or 3).
    std::vector<int> ace_heads;
    auto ace_heads_it = data.find("ace_heads");
    if (ace_heads_it != data.end() && ace_heads_it->is_array() && !ace_heads_it->empty()) {
        for (const auto& h : *ace_heads_it)
            if (h.is_number())
                ace_heads.push_back(h.get<int>());
    }
    if (ace_heads.empty()) {
        int fallback = int_field(data, "ace_head", 3);
        ace_heads.push_back(fallback != 0 ? fallback : 3);
    }
    std::set<int> unique_sorted(ace_heads.begin(), ace_heads.end());
    result.head_mode.ace_heads.assign(unique_sorted.begin(), unique_sorted.end());

    // head_ace: physical head 0-3 -> ACE index, defaulting to identity (head
    // N feeds from ACE N) for anything missing/unparseable. JSON object keys
    // are always strings, so only the "str(h)" lookup from the Python source
    // can ever match (the mixed str/int .get() fallback there is dead code
    // once loaded from real JSON).
    nlohmann::json raw_head_ace = nlohmann::json::object();
    auto head_ace_it = data.find("head_ace");
    if (head_ace_it != data.end() && head_ace_it->is_object())
        raw_head_ace = *head_ace_it;
    for (int h = 0; h < 4; ++h)
        result.head_mode.head_ace[h] = int_field(raw_head_ace, std::to_string(h), h);

    return result;
}

} } // namespace Slic3r::MultiACE
