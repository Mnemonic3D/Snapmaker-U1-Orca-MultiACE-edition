#include "MultiAceClient.hpp"
#include "libslic3r/MultiACE/MultiAceStateParser.hpp"
#include "slic3r/Utils/Http.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r { namespace MultiACE {

std::optional<LiveState> fetch_live_state(
    const std::string& host, int port, const std::string& path,
    long timeout_connect_s, long timeout_max_s)
{
    // host may include ":port", overriding the port argument - matches the
    // Python source's `if ':' in host: host, _, port_str = host.partition(':')`.
    std::string real_host = host;
    int real_port = port;
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        real_host = host.substr(0, colon);
        try {
            real_port = std::stoi(host.substr(colon + 1));
        } catch (...) {
            // Malformed port suffix - keep the passed-in port, matching the
            // Python source's bare `except ValueError: pass`.
        }
    }

    std::string url = "http://" + real_host + ":" + std::to_string(real_port) + path;

    std::string body;
    bool ok = false;
    Http::get(url)
        .timeout_connect(timeout_connect_s)
        .timeout_max(timeout_max_s)
        .on_complete([&](std::string response, unsigned status) {
            if (status == 200) {
                body = std::move(response);
                ok = true;
            }
        })
        .on_error([&](std::string, std::string, unsigned) {})
        .perform_sync();

    if (!ok)
        return std::nullopt;

    nlohmann::json data = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (data.is_discarded())
        return std::nullopt;

    return parse_state_response(data);
}

} } // namespace Slic3r::MultiACE
