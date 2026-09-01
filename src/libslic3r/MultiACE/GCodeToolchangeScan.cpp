#include "GCodeToolchangeScan.hpp"
#include "GCodeLines.hpp"

#include <algorithm>
#include <regex>

namespace Slic3r { namespace MultiACE {

std::vector<int> parse_toolchanges(const std::string& gcode)
{
    static const std::regex change_re(R"(^;\s*Change Tool\s*\d+\s*->\s*Tool\s*(\d+))");
    static const std::regex bare_re(R"(^T(\d{1,2})\b)");

    std::vector<int> result;
    bool saw_change = false;
    for (const auto& raw_line : split_lines(gcode)) {
        std::string s(strip(raw_line));
        if (s.empty())
            continue;

        std::smatch m;
        if (std::regex_search(s, m, change_re)) {
            saw_change = true;
            result.push_back(std::stoi(m[1].str()));
            continue;
        }

        if (saw_change || s[0] == ';')
            continue;

        if (std::regex_search(s, m, bare_re))
            result.push_back(std::stoi(m[1].str()));
    }
    return result;
}

int infer_num_aces(const std::string& gcode)
{
    static const std::regex t_re(R"(^T(\d{1,2})\s*$)");
    int max_ace = 0;
    for (const auto& raw_line : split_lines(gcode)) {
        std::string s(strip(raw_line));
        std::smatch m;
        if (std::regex_search(s, m, t_re)) {
            int ace = std::stoi(m[1].str()) / 4;
            max_ace = std::max(max_ace, ace);
        }
    }
    return max_ace + 1;
}

bool is_extruding_move(const std::string& line)
{
    static const std::regex e_val_re(R"(\bE(-?\d*\.?\d+))");
    std::string s(strip(line));
    if (!(s.rfind("G1", 0) == 0 || s.rfind("G0", 0) == 0))
        return false;
    std::smatch m;
    if (!std::regex_search(s, m, e_val_re))
        return false;
    try {
        return std::stod(m[1].str()) > 0;
    } catch (...) {
        return false;
    }
}

std::optional<size_t> structural_inject_idx(const std::vector<std::string>& lines)
{
    std::optional<size_t> ext_idx;
    for (size_t idx = 0; idx < lines.size(); ++idx) {
        if (is_extruding_move(lines[idx])) {
            ext_idx = idx;
            break;
        }
    }
    if (!ext_idx)
        return std::nullopt;

    // Search back at most 200 lines for a blank line or a "===== ... ====="
    // section header, matching the Python range(ext_idx-1, max(-1, ext_idx-200), -1).
    long long ext = static_cast<long long>(*ext_idx);
    long long stop = std::max<long long>(-1, ext - 200);
    for (long long j = ext - 1; j > stop; --j) {
        std::string s(strip(lines[static_cast<size_t>(j)]));
        if (s.empty() || (s[0] == ';' && s.find("=====") != std::string::npos))
            return static_cast<size_t>(j);
    }
    return ext_idx;
}

} } // namespace Slic3r::MultiACE
