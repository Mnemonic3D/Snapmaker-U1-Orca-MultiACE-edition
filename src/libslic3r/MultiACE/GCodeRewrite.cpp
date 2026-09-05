#include "GCodeRewrite.hpp"
#include "GCodeLines.hpp"
#include "GCodeToolchangeScan.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace Slic3r { namespace MultiACE {

namespace {

// Python's int(), NOT std::stoi(): the entire (already-stripped) string must
// be a valid integer literal or this returns nullopt. std::stoi() alone
// would silently accept "250x" as 250 (stops at the first non-digit),
// where Python's int("250x") raises ValueError - which the ported
// fix_toolchange_temperatures() below treats as "value unknown for this T",
// not "value is 250".
std::optional<int> parse_int_strict(const std::string& s)
{
    if (s.empty())
        return std::nullopt;
    size_t i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    if (i >= s.size())
        return std::nullopt;
    for (size_t j = i; j < s.size(); ++j)
        if (!std::isdigit(static_cast<unsigned char>(s[j])))
            return std::nullopt;
    try {
        return std::stoi(s);
    } catch (...) {
        return std::nullopt;
    }
}

// Repeatedly trims leading/trailing '"' characters - Python's str.strip('"').
std::string strip_dquotes(std::string_view s)
{
    size_t b = 0, e = s.size();
    while (b < e && s[b] == '"') ++b;
    while (e > b && s[e - 1] == '"') --e;
    return std::string(s.substr(b, e - b));
}

// Replaces every T4-T15 occurrence within a line with T(n%4) - the inner
// substitution _fix_m104 applies to matched M104/M109 lines.
std::string replace_high_t_refs_mod4(const std::string& line)
{
    static const std::regex t_high_re(R"(T([4-9]|1[0-5]))");
    std::string result;
    auto begin = std::sregex_iterator(line.begin(), line.end(), t_high_re);
    auto end = std::sregex_iterator();
    size_t last = 0;
    for (auto it = begin; it != end; ++it) {
        auto m = *it;
        result += line.substr(last, static_cast<size_t>(m.position(0)) - last);
        int n = std::stoi(m[1].str());
        result += "T" + std::to_string(n % 4);
        last = static_cast<size_t>(m.position(0)) + static_cast<size_t>(m.length(0));
    }
    result += line.substr(last);
    return result;
}

} // namespace

RewriteResult rewrite(const std::string& gcode, const std::set<int>& feeder_heads)
{
    // Pass 1: within M104/M109 lines, fold high T-refs (T4-T15) to T(n%4).
    std::vector<std::string> lines0 = split_lines(gcode);
    for (auto& line : lines0) {
        if (line.rfind("M104", 0) == 0 || line.rfind("M109", 0) == 0)
            line = replace_high_t_refs_mod4(line);
    }
    std::string gcode1 = restore_trailing_newline(gcode, join_lines(lines0));

    // Pass 2: drop SM_PRINT_PREEXTRUDE_FILAMENT INDEX=N lines for N 4-15
    // (their swap already got expanded into a real ACE_SWAP_HEAD by pass 4).
    // Not anchored in the Python source (no ^/$), so a plain whole-string
    // regex_replace is the faithful translation here - no MULTILINE-flag
    // risk since the pattern has no line anchors to begin with.
    static const std::regex preextrude_high_re(R"(SM_PRINT_PREEXTRUDE_FILAMENT INDEX=([4-9]|1[0-5])\n?)");
    std::string gcode2 = std::regex_replace(gcode1, preextrude_high_re, "");

    // Split pre/body at the first "; Change Tool" comment line.
    static const std::regex change_full_re(R"(^;\s*Change Tool\s*\d+\s*->\s*Tool\s*\d+)");
    std::vector<std::string> lines2 = split_lines(gcode2);
    std::optional<size_t> split_idx;
    for (size_t i = 0; i < lines2.size(); ++i) {
        if (std::regex_search(lines2[i], change_full_re)) { split_idx = i; break; }
    }
    std::string pre, body;
    if (!split_idx) {
        pre = gcode2;
        body.clear();
    } else {
        size_t offset = nth_line_offset(gcode2, *split_idx);
        pre = gcode2.substr(0, offset);
        body = gcode2.substr(offset);
    }

    // Pass 3 (pre only): fold high bare-T lines to T(n%4), no ACE_SWAP_HEAD -
    // this is start-gcode, before G28, where a mechanical swap is unsafe.
    //
    // NOTE on blank-line absorption (affects both pass 3 and pass 4 below):
    // the Python pattern is applied to the WHOLE multi-line string with
    // re.MULTILINE, and \s* is greedy while \s matches \n too - so a match
    // against "T4" followed by blank line(s) actually extends across them,
    // backtracking only enough to let the trailing $ succeed. Net effect:
    // any run of blank lines immediately after a matched T-line gets
    // silently swallowed (confirmed empirically, not just by reading the
    // regex - this is easy to get backwards). A per-line translation must
    // replicate that explicitly, by skipping following blank lines whenever
    // a match occurs, or gcode ends up with a spurious extra blank line the
    // Python original never had.
    static const std::regex bare_t_high_full_re(R"(^T([4-9]|1[0-5])\s*$)");
    std::vector<std::string> pre_lines = split_lines(pre);
    std::vector<std::string> pre_out;
    pre_out.reserve(pre_lines.size());
    for (size_t idx = 0; idx < pre_lines.size(); ++idx) {
        std::smatch m;
        if (std::regex_search(pre_lines[idx], m, bare_t_high_full_re)) {
            pre_out.push_back("T" + std::to_string(std::stoi(m[1].str()) % 4));
            while (idx + 1 < pre_lines.size() && strip(pre_lines[idx + 1]).empty())
                ++idx;
        } else {
            pre_out.push_back(pre_lines[idx]);
        }
    }
    // pre gets concatenated with body below - must keep its trailing
    // newline (the boundary between pre's last line and body's first) or
    // they silently merge into one line. See restore_trailing_newline().
    pre = restore_trailing_newline(pre, join_lines(pre_out));

    // Pass 4 (body only): expand high bare-T lines into T<head> + ACE_SWAP_HEAD.
    std::vector<std::string> body_lines = split_lines(body);
    std::vector<std::string> expanded;
    expanded.reserve(body_lines.size());
    for (size_t idx = 0; idx < body_lines.size(); ++idx) {
        std::smatch m;
        if (std::regex_search(body_lines[idx], m, bare_t_high_full_re)) {
            int n = std::stoi(m[1].str());
            int head = n % 4, ace = n / 4;
            expanded.push_back("T" + std::to_string(head));
            expanded.push_back("ACE_SWAP_HEAD HEAD=" + std::to_string(head) +
                                " ACE=" + std::to_string(ace) + " SLOT=" + std::to_string(head));
            while (idx + 1 < body_lines.size() && strip(body_lines[idx + 1]).empty())
                ++idx;
        } else {
            expanded.push_back(body_lines[idx]);
        }
    }

    // Pass 5: stateful walk tracking which (ace,slot) each head currently
    // holds, starting from the assumption every head begins at (ACE 0, slot=head).
    std::map<int, std::pair<int, int>> head_loaded = {{0, {0, 0}}, {1, {0, 1}}, {2, {0, 2}}, {3, {0, 3}}};
    std::vector<std::string> filtered;
    filtered.reserve(expanded.size());

    static const std::regex bare_t_low_re(R"(^T([0-3])\s*$)");
    static const std::regex ace_swap_re(R"(^ACE_SWAP_HEAD HEAD=(\d+) ACE=(\d+) SLOT=(\d+)(?:\s+\S+=\S+)*\s*$)");

    int skipped = 0, swapbacks = 0;
    size_t i = 0;
    while (i < expanded.size()) {
        const std::string& line = expanded[i];
        std::smatch m_t;
        // NOTE: matched against the raw line, not a stripped copy - mirrors
        // the Python source exactly (re.match(pattern, line), no .strip()).
        if (std::regex_search(line, m_t, bare_t_low_re)) {
            int head = std::stoi(m_t[1].str());
            if (feeder_heads.find(head) != feeder_heads.end()) {
                filtered.push_back(line);
                ++i;
                continue;
            }
            size_t j = i + 1;
            while (j < expanded.size() && strip(expanded[j]).empty()) ++j;
            bool next_is_swap = (j < expanded.size()) && (expanded[j].rfind("ACE_SWAP_HEAD", 0) == 0);
            if (next_is_swap) {
                filtered.push_back(line);
            } else {
                std::pair<int, int> initial_key = {0, head};
                auto it = head_loaded.find(head);
                if (it == head_loaded.end() || it->second != initial_key) {
                    filtered.push_back(line);
                    filtered.push_back("ACE_SWAP_HEAD HEAD=" + std::to_string(head) +
                                        " ACE=0 SLOT=" + std::to_string(head));
                    ++swapbacks;
                    head_loaded[head] = initial_key;
                } else {
                    filtered.push_back(line);
                }
            }
            ++i;
            continue;
        }

        std::smatch m_s;
        if (std::regex_search(line, m_s, ace_swap_re)) {
            int head = std::stoi(m_s[1].str());
            int ace = std::stoi(m_s[2].str());
            int slot = std::stoi(m_s[3].str());
            std::pair<int, int> key = {ace, slot};
            auto it = head_loaded.find(head);
            if (it != head_loaded.end() && it->second == key) {
                filtered.push_back("; " + line + "  ; skipped (already loaded)");
                ++skipped;
                ++i;
                continue;
            }
            head_loaded[head] = key;
        }
        filtered.push_back(line);
        ++i;
    }

    RewriteResult result;
    // NOTE: deliberately NOT restoring a trailing newline here. Python's own
    // pass 5 reconstructs via body.splitlines() + '\n'.join(filtered_lines) -
    // the same lossy-on-a-single-trailing-newline pattern split_lines()/
    // join_lines() replicate (see their doc comments) - so a bare
    // join_lines(filtered) already matches Python's actual (if slightly
    // quirky) output exactly. Restoring it here would make this MORE
    // "correct" than Python's real behavior, which is a divergence, not a fix.
    result.gcode = pre + join_lines(filtered);
    for (const auto& l : filtered)
        if (l.rfind("ACE_SWAP_HEAD", 0) == 0)
            ++result.active_swaps;
    result.skipped = skipped;
    result.swapbacks = swapbacks;
    return result;
}

std::pair<std::string, int> apply_head_mode_rewrite(
    const std::string& gcode,
    const std::map<int, AssignmentEntry>& assignment)
{
    static const std::regex change_re(R"(^;\s*Change Tool\s*\d+\s*->\s*Tool\s*(\d+))");
    static const std::regex bare_t_re(R"(^T(\d{1,2})\s*$)");
    static const std::regex preextr_re(R"(^SM_PRINT_PREEXTRUDE_FILAMENT INDEX=(\d+)\b)");
    static const std::regex m104_prefix_re(R"(^M10[49]\b)");
    static const std::regex t_inline_re(R"(T(\d{1,2}))");
    static const std::regex change_full_re(R"(^;\s*Change Tool\s*\d+\s*->\s*Tool\s*\d+)");

    std::vector<std::string> all_lines = split_lines(gcode);
    std::optional<size_t> split_idx;
    for (size_t i = 0; i < all_lines.size(); ++i) {
        if (std::regex_search(all_lines[i], change_full_re)) { split_idx = i; break; }
    }
    if (!split_idx)
        return {gcode, 0};

    size_t offset = nth_line_offset(gcode, *split_idx);
    std::string pre = gcode.substr(0, offset);
    std::string body = gcode.substr(offset);

    auto head_for = [&](int t) -> std::optional<int> {
        auto it = assignment.find(t);
        if (it != assignment.end() && (it->second.kind == "pin" || it->second.kind == "ace"))
            return it->second.head;
        return std::nullopt;
    };

    // Python's source uses body.split('\n') here, NOT .splitlines() - a real
    // difference (split('\n') always yields a trailing "" element when body
    // ends in \n, splitlines() doesn't), so split_on_newline() is required
    // for a byte-exact round trip through join_lines(), not split_lines().
    std::vector<std::string> lines = split_on_newline(body);
    std::vector<std::string> out;
    out.reserve(lines.size());
    std::map<int, std::pair<int, int>> head_loaded;
    std::optional<int> pending_t;
    int swaps = 0;

    for (const auto& line : lines) {
        std::string s(strip(line));

        std::smatch m_chg;
        if (std::regex_search(s, m_chg, change_re)) {
            pending_t = std::stoi(m_chg[1].str());
            out.push_back(line);
            continue;
        }

        std::smatch m_t;
        if (std::regex_search(s, m_t, bare_t_re)) {
            int parsed_t = std::stoi(m_t[1].str());
            int orig_t = pending_t ? *pending_t : parsed_t;
            auto it = assignment.find(orig_t);
            bool has_entry = it != assignment.end() && (it->second.kind == "pin" || it->second.kind == "ace");
            int head = has_entry ? it->second.head : parsed_t;
            out.push_back("T" + std::to_string(head));
            if (it != assignment.end() && it->second.kind == "ace") {
                std::pair<int, int> key = {it->second.ace, it->second.slot};
                auto hl_it = head_loaded.find(head);
                if (hl_it == head_loaded.end() || hl_it->second != key) {
                    out.push_back("ACE_SWAP_HEAD HEAD=" + std::to_string(head) +
                                  " ACE=" + std::to_string(it->second.ace) +
                                  " SLOT=" + std::to_string(it->second.slot));
                    head_loaded[head] = key;
                    ++swaps;
                }
            }
            pending_t = std::nullopt;
            continue;
        }

        std::smatch m_pre;
        if (std::regex_search(s, m_pre, preextr_re)) {
            int t = std::stoi(m_pre[1].str());
            auto it = assignment.find(t);
            if (it != assignment.end() && it->second.kind == "ace")
                continue; // a real ACE swap flushes/primes on its own
            if (it != assignment.end() && it->second.kind == "pin") {
                out.push_back("SM_PRINT_PREEXTRUDE_FILAMENT INDEX=" + std::to_string(it->second.head));
                continue;
            }
            out.push_back(line);
            continue;
        }

        if (std::regex_search(s, m104_prefix_re)) {
            std::string replaced;
            auto begin = std::sregex_iterator(line.begin(), line.end(), t_inline_re);
            auto end = std::sregex_iterator();
            size_t last = 0;
            for (auto it2 = begin; it2 != end; ++it2) {
                auto sm = *it2;
                replaced += line.substr(last, static_cast<size_t>(sm.position(0)) - last);
                int t = std::stoi(sm[1].str());
                auto h = head_for(t);
                replaced += h ? ("T" + std::to_string(*h)) : sm.str(0);
                last = static_cast<size_t>(sm.position(0)) + static_cast<size_t>(sm.length(0));
            }
            replaced += line.substr(last);
            out.push_back(replaced);
            continue;
        }

        out.push_back(line);
    }

    // split_on_newline()+join_lines() is a byte-exact round trip (unlike
    // split_lines(), no restore_trailing_newline() patch needed here).
    return {pre + join_lines(out), swaps};
}

std::string apply_remap(const std::string& gcode, const std::map<int, int>& remap)
{
    if (remap.empty())
        return gcode;

    auto rm = [&](int n) {
        auto it = remap.find(n);
        return it != remap.end() ? it->second : n;
    };

    static const std::regex bare_t_full_re(R"(^T(\d{1,2})\s*$)");
    static const std::regex m10x_prefix_re(R"(^M10[49]\b)");
    static const std::regex t_inline_re(R"(T(\d+))");
    static const std::regex preextrude_re(R"(SM_PRINT_PREEXTRUDE_FILAMENT INDEX=(\d+))");

    // The 3 Python passes run as independent whole-text re.sub calls, but
    // the patterns are structurally disjoint in real gcode (a line can't be
    // simultaneously "just T5", an M104/M109 line, AND contain
    // SM_PRINT_PREEXTRUDE_FILAMENT), so folding them into one mutually-
    // exclusive per-line pass changes nothing observable.
    //
    // The bare-T pattern (^T(\d{1,2})\s*$, MULTILINE) has the same greedy-\s*
    // blank-line-absorption behavior documented in rewrite() above - a
    // matched T-line silently swallows any immediately-following blank
    // line(s) in the Python source. Replicated here by skipping them too.
    std::vector<std::string> lines = split_lines(gcode);
    std::vector<std::string> out;
    out.reserve(lines.size());
    for (size_t idx = 0; idx < lines.size(); ++idx) {
        std::string line = lines[idx];
        std::smatch m;
        if (std::regex_search(line, m, bare_t_full_re)) {
            out.push_back("T" + std::to_string(rm(std::stoi(m[1].str()))));
            while (idx + 1 < lines.size() && strip(lines[idx + 1]).empty())
                ++idx;
            continue;
        }
        if (std::regex_search(line, m, m10x_prefix_re)) {
            std::string replaced;
            auto begin = std::sregex_iterator(line.begin(), line.end(), t_inline_re);
            auto end = std::sregex_iterator();
            size_t last = 0;
            for (auto it = begin; it != end; ++it) {
                auto sm = *it;
                replaced += line.substr(last, static_cast<size_t>(sm.position(0)) - last);
                replaced += "T" + std::to_string(rm(std::stoi(sm[1].str())));
                last = static_cast<size_t>(sm.position(0)) + static_cast<size_t>(sm.length(0));
            }
            replaced += line.substr(last);
            out.push_back(replaced);
            continue;
        }
        if (line.find("SM_PRINT_PREEXTRUDE_FILAMENT INDEX=") != std::string::npos) {
            std::string replaced;
            auto begin = std::sregex_iterator(line.begin(), line.end(), preextrude_re);
            auto end = std::sregex_iterator();
            size_t last = 0;
            for (auto it = begin; it != end; ++it) {
                auto sm = *it;
                replaced += line.substr(last, static_cast<size_t>(sm.position(0)) - last);
                replaced += "SM_PRINT_PREEXTRUDE_FILAMENT INDEX=" + std::to_string(rm(std::stoi(sm[1].str())));
                last = static_cast<size_t>(sm.position(0)) + static_cast<size_t>(sm.length(0));
            }
            replaced += line.substr(last);
            out.push_back(replaced);
            continue;
        }
        out.push_back(line);
    }
    return restore_trailing_newline(gcode, join_lines(out));
}

std::pair<std::string, int> inject_auto_load(
    const std::string& gcode,
    const std::set<int>& feeder_heads,
    const std::optional<std::map<int, std::pair<int, int>>>& initial_targets)
{
    // Python's source uses gcode.split('\n'), not .splitlines() - see the
    // note in apply_head_mode_rewrite() above.
    std::vector<std::string> lines = split_on_newline(gcode);

    // Strip any pre-existing auto-load block first, for idempotency across
    // repeated runs on the same file.
    std::vector<std::string> cleaned;
    bool in_block = false;
    for (const auto& ln : lines) {
        std::string ls(strip(ln));
        if (ls.rfind("; multiACE auto-load: load", 0) == 0) {
            in_block = true;
            continue;
        }
        if (in_block) {
            if (ls.rfind("; multiACE auto-load: end", 0) == 0)
                in_block = false;
            continue;
        }
        cleaned.push_back(ln);
    }
    lines = std::move(cleaned);

    std::optional<size_t> inject_idx = structural_inject_idx(lines);

    if (!inject_idx) {
        for (size_t idx = 0; idx < lines.size(); ++idx) {
            std::string lower = lines[idx];
            std::transform(lower.begin(), lower.end(), lower.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lines[idx].find(u8"画起始线") != std::string::npos // '画起始线'
                || lower.find("draw the starting line") != std::string::npos) {
                inject_idx = idx;
                break;
            }
        }
    }
    if (!inject_idx) {
        static const std::regex change_re(R"(^;\s*Change Tool\s*\d+\s*->\s*Tool\s*\d+)");
        for (size_t idx = 0; idx < lines.size(); ++idx) {
            std::string s(strip(lines[idx]));
            if (std::regex_search(s, change_re)) { inject_idx = idx; break; }
        }
    }
    if (!inject_idx) {
        for (size_t idx = 0; idx < lines.size(); ++idx) {
            if (lines[idx].find("SM_PRINT_PREEXTRUDE_FILAMENT") != std::string::npos) {
                inject_idx = idx;
                break;
            }
        }
    }
    if (!inject_idx) {
        for (size_t idx = 0; idx < lines.size(); ++idx) {
            std::string s(strip(lines[idx]));
            if (s.rfind("ACE_SWAP_HEAD HEAD=", 0) == 0) { inject_idx = idx; break; }
        }
    }

    std::map<int, std::pair<int, int>> initial; // std::map sorts by key = Python's sorted(initial)
    std::set<int> used_heads;

    static const std::regex bare_t03_re(R"(^T([0-3])\s*$)");
    static const std::regex ace_swap_full_re(R"(^ACE_SWAP_HEAD HEAD=(\d+) ACE=(\d+) SLOT=(\d+)(?:\s+\S+=\S+)*\s*$)");

    size_t body_start = inject_idx ? *inject_idx : 0;
    for (size_t i = body_start; i < lines.size(); ++i) {
        std::string ls(strip(lines[i]));

        std::smatch m_t;
        if (std::regex_search(ls, m_t, bare_t03_re)) {
            int head = std::stoi(m_t[1].str());
            used_heads.insert(head);
            if (initial.find(head) == initial.end()) {
                size_t j = i + 1;
                while (j < lines.size()) {
                    std::string sj(strip(lines[j]));
                    if (sj.empty() || sj[0] == ';' || sj.rfind("ACE_BG_", 0) == 0) {
                        ++j;
                        continue;
                    }
                    break;
                }
                std::smatch ace_m;
                bool have_ace_m = false;
                if (j < lines.size()) {
                    std::string sj(strip(lines[j]));
                    have_ace_m = std::regex_search(sj, ace_m, ace_swap_full_re);
                }
                if (have_ace_m && std::stoi(ace_m[1].str()) == head) {
                    initial[head] = {std::stoi(ace_m[2].str()), std::stoi(ace_m[3].str())};
                } else if (initial_targets && initial_targets->find(head) != initial_targets->end()) {
                    initial[head] = initial_targets->at(head);
                } else {
                    initial[head] = {0, head};
                }
            }
            continue;
        }

        std::smatch m;
        if (std::regex_search(ls, m, ace_swap_full_re)) {
            int head = std::stoi(m[1].str());
            used_heads.insert(head);
            if (initial.find(head) == initial.end())
                initial[head] = {std::stoi(m[2].str()), std::stoi(m[3].str())};
        }
    }

    for (int head : used_heads) {
        if (initial.find(head) == initial.end()) {
            if (initial_targets && initial_targets->find(head) != initial_targets->end())
                initial[head] = initial_targets->at(head);
            else
                initial[head] = {0, head};
        }
    }
    for (int head : feeder_heads)
        initial.erase(head);

    if (!inject_idx || initial.empty())
        return {gcode, 0};

    std::vector<std::string> inject_lines;
    inject_lines.push_back("");
    inject_lines.push_back("; multiACE auto-load: load initial filaments");
    for (const auto& kv : initial) {
        int head = kv.first;
        int ace = kv.second.first, slot = kv.second.second;
        inject_lines.push_back("ACE_SWAP_HEAD HEAD=" + std::to_string(head) +
                                " ACE=" + std::to_string(ace) + " SLOT=" + std::to_string(slot));
    }
    inject_lines.push_back("; multiACE auto-load: end");
    inject_lines.push_back("");

    std::vector<std::string> new_lines;
    new_lines.insert(new_lines.end(), lines.begin(), lines.begin() + static_cast<long>(*inject_idx));
    new_lines.insert(new_lines.end(), inject_lines.begin(), inject_lines.end());
    new_lines.insert(new_lines.end(), lines.begin() + static_cast<long>(*inject_idx), lines.end());

    // split_on_newline()+join_lines() round-trips exactly, no patch needed.
    return {join_lines(new_lines), static_cast<int>(initial.size())};
}

std::pair<std::string, std::vector<TemperatureFix>> fix_toolchange_temperatures(const std::string& gcode)
{
    // Whole-string search (not line-anchored, no MULTILINE-equivalent
    // needed) - matches the Python source's re.search(pattern, gcode)
    // exactly. The negative lookahead excludes
    // "nozzle_temperature_initial_layer = ..." (and any other
    // "nozzle_temperature_*" variant) from being mistaken for the plain
    // per-head sustained-temp array this function actually needs.
    static const std::regex hdr_re(R"(;\s*nozzle_temperature(?! *_)\s*=\s*(.+))");
    std::vector<std::optional<int>> nozzle_temp;
    std::smatch hm;
    if (std::regex_search(gcode, hm, hdr_re)) {
        std::string rest = hm[1].str();
        size_t start = 0;
        for (size_t i = 0; i <= rest.size(); ++i) {
            if (i == rest.size() || rest[i] == ';' || rest[i] == ',') {
                std::string piece(strip(rest.substr(start, i - start)));
                piece = strip_dquotes(piece);
                nozzle_temp.push_back(parse_int_strict(piece));
                start = i + 1;
            }
        }
    }
    if (nozzle_temp.empty())
        return {gcode, {}};

    std::vector<std::string> lines = split_lines(gcode);
    static const std::regex tc_re(R"(^T(\d+)\s*$)");
    static const std::regex temp_re(R"(^(M10[49] S)(\d+)(\s*.*)$)");
    static const std::regex ramming_re(R"(^;\s*Ramming start\s*$)");
    std::vector<TemperatureFix> changes;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::smatch m;
        if (!std::regex_search(lines[i], m, tc_re))
            continue;
        int t_num = std::stoi(m[1].str());
        if (static_cast<size_t>(t_num) >= nozzle_temp.size() || !nozzle_temp[t_num])
            continue;
        int correct_temp = *nozzle_temp[t_num];

        // Correct up to the first two bare M109/M104 temp-set lines after
        // this toolchange (the purge temp, then the settle-to-print temp),
        // stopping at the next block's own "Ramming start" boundary so the
        // third, differently-purposed occurrence is never touched.
        int fixed_here = 0;
        for (size_t j = i + 1; j < lines.size() && j < i + 60; ++j) {
            if (std::regex_search(lines[j], ramming_re))
                break;
            std::smatch mm;
            if (std::regex_search(lines[j], mm, temp_re)) {
                int old_temp = std::stoi(mm[2].str());
                if (old_temp != correct_temp) {
                    lines[j] = mm[1].str() + std::to_string(correct_temp) + mm[3].str();
                    changes.push_back({static_cast<int>(j) + 1, t_num, old_temp, correct_temp});
                }
                if (++fixed_here >= 2)
                    break;
            }
        }
    }

    return {restore_trailing_newline(gcode, join_lines(lines)), changes};
}

} } // namespace Slic3r::MultiACE
