#include "ColorMatch.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace Slic3r { namespace MultiACE {

const std::vector<std::pair<std::string, std::array<int, 3>>>& named_colors()
{
    // Order matters: approx_color_name keeps the FIRST strictly-closer match,
    // so ties break in favor of whichever name appears earlier here - must
    // match post_process_virtual_toolheads.py's _NAMED_COLORS exactly.
    static const std::vector<std::pair<std::string, std::array<int, 3>>> table = {
        {"Black",      {0x00, 0x00, 0x00}},
        {"White",      {0xFF, 0xFF, 0xFF}},
        {"Gray",       {0x80, 0x80, 0x80}},
        {"DarkGray",   {0x40, 0x40, 0x40}},
        {"LightGray",  {0xD3, 0xD3, 0xD3}},
        {"Silver",     {0xC0, 0xC0, 0xC0}},
        {"Red",        {0xE0, 0x20, 0x20}},
        {"DarkRed",    {0x8B, 0x00, 0x00}},
        {"Pink",       {0xFF, 0xC0, 0xCB}},
        {"Orange",     {0xFF, 0x8C, 0x00}},
        {"Yellow",     {0xFF, 0xE0, 0x20}},
        {"Gold",       {0xDA, 0xA5, 0x20}},
        {"Brown",      {0x8B, 0x45, 0x13}},
        {"Beige",      {0xE6, 0xD6, 0xA5}},
        {"Green",      {0x20, 0xA0, 0x20}},
        {"DarkGreen",  {0x00, 0x64, 0x00}},
        {"LightGreen", {0x90, 0xEE, 0x90}},
        {"Cyan",       {0x20, 0xD0, 0xD0}},
        {"Blue",       {0x30, 0x50, 0xF0}},
        {"DarkBlue",   {0x00, 0x00, 0x8B}},
        {"LightBlue",  {0xAD, 0xD8, 0xE6}},
        {"Purple",     {0x80, 0x20, 0x80}},
        {"Magenta",    {0xE0, 0x20, 0xE0}},
    };
    return table;
}

namespace {

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Parses exactly 2 hex digits at s[offset:offset+2]. Unlike a bare stoi call,
// this rejects a partially-valid pair (e.g. "1g") instead of silently
// stopping at the first bad digit - matching Python's int(s[0:2], 16), which
// requires the whole 2-character slice to be valid or raises ValueError.
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

} // namespace

std::optional<std::array<int, 3>> hex_to_rgb(const std::string& hex_str)
{
    size_t b = 0, e = hex_str.size();
    while (b < e && std::isspace(static_cast<unsigned char>(hex_str[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(hex_str[e - 1]))) --e;
    while (b < e && hex_str[b] == '#') ++b;
    std::string s = hex_str.substr(b, e - b);

    if (s.size() < 6)
        return std::nullopt;

    int r, g, bch;
    if (!parse_hex_byte(s, 0, r) || !parse_hex_byte(s, 2, g) || !parse_hex_byte(s, 4, bch))
        return std::nullopt;
    return std::array<int, 3>{r, g, bch};
}

std::string strip_color_qualifier(const std::string& name)
{
    static const std::vector<std::string> qualifiers = {"Dark", "Light"};
    if (name.empty())
        return "";
    for (const auto& q : qualifiers) {
        if (name.size() > q.size() && name.compare(0, q.size(), q) == 0)
            return name.substr(q.size());
    }
    return name;
}

std::string canonicalize_color_base(const std::string& base)
{
    static const std::map<std::string, std::string> synonyms = {
        {"Silver", "Gray"},
        {"Gold",   "Yellow"},
    };
    auto it = synonyms.find(base);
    return it != synonyms.end() ? it->second : base;
}

std::string approx_color_name(const std::string& hex_str)
{
    if (hex_str.empty())
        return "?";
    auto rgb = hex_to_rgb(hex_str);
    if (!rgb)
        return hex_str;

    int r = (*rgb)[0], g = (*rgb)[1], b = (*rgb)[2];
    std::string best;
    long long best_d = 1LL << 30;
    for (const auto& entry : named_colors()) {
        const auto& n = entry.second;
        long long dr = r - n[0], dg = g - n[1], db = b - n[2];
        long long d = dr * dr + dg * dg + db * db;
        if (d < best_d) {
            best_d = d;
            best = entry.first;
        }
    }
    return best;
}

std::string format_color_hex_rgb(const std::string& hex_str)
{
    auto rgb = hex_to_rgb(hex_str);
    if (!rgb)
        return hex_str.empty() ? "?" : hex_str;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s RGB(%d,%d,%d)",
                  to_lower(hex_str).c_str(), (*rgb)[0], (*rgb)[1], (*rgb)[2]);
    return buf;
}

std::string format_color(int t_index, const std::map<int, std::string>& color_names)
{
    auto it = color_names.find(t_index);
    if (it == color_names.end() || it->second.empty())
        return "?";

    const std::string& hex_val = it->second;
    std::string name = approx_color_name(hex_val);
    std::string full = format_color_hex_rgb(hex_val);

    size_t p = hex_val.find_first_not_of('#');
    std::string stripped = (p == std::string::npos) ? std::string() : hex_val.substr(p);
    stripped = to_lower(stripped);
    std::string name_lower = to_lower(name);

    if (stripped == name_lower)
        return full;
    return name + " (" + full + ")";
}

} } // namespace Slic3r::MultiACE
