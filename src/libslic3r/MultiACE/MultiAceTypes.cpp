#include "MultiAceTypes.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r { namespace MultiACE {

namespace {

std::string trim_lower(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    std::string out = s.substr(b, e - b);
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

std::set<std::string> check_material_availability(
    const std::map<int, std::string>& filament_types,
    const std::vector<Slot>& live_slots)
{
    std::set<std::string> loaded;
    for (const auto& s : live_slots) {
        std::string m = trim_lower(s.material);
        if (!m.empty())
            loaded.insert(m);
    }

    std::set<std::string> required;
    for (const auto& kv : filament_types) {
        std::string m = trim_lower(kv.second);
        if (!m.empty())
            required.insert(m);
    }

    std::set<std::string> missing;
    for (const auto& m : required)
        if (loaded.find(m) == loaded.end())
            missing.insert(m);
    return missing;
}

} } // namespace Slic3r::MultiACE
