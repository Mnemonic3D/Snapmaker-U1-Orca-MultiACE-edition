#include "SnapmakerPrinterAgent.hpp"
#include "Http.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"

#include "nlohmann/json.hpp"
#include <boost/log/trivial.hpp>

namespace Slic3r {

namespace {

constexpr const char* SNAPMAKER_AGENT_VERSION = "0.0.1";

// Safely access a parallel array by index, returning a fallback if out of bounds.
template<typename T>
T safe_at(const std::vector<T>& vec, int index, const T& fallback)
{
    return (index >= 0 && index < static_cast<int>(vec.size())) ? vec[index] : fallback;
}

std::string find_closest_color_preset_by_vendor_and_type(const PresetCollection& filaments,
                                                         const std::string&      vendor_name,
                                                         const std::string&      filament_type,
                                                         const std::string&      color_rgba)
{
    std::string best_match_id       = "";
    int         best_color_distance = 0xffffffff;

    for (const auto& p : filaments.get_presets()) {
        if (p.is_visible && p.is_compatible &&
            // Filament profile must be detached from parent to be considered for matching
            filaments.get_preset_base(p) == &p && p.config.opt_string("filament_vendor", 0u) == vendor_name &&
            p.config.opt_string("filament_type", 0u) == filament_type) {
            // The printer returns RGBA in the format RRGGBBAA, but profiles store color as #RRGGBB,
            // so we must remove # and ignore alpha channel for distance calculation
            unsigned int target_color_value = std::stoul(color_rgba.substr(0, color_rgba.length() - 2), nullptr, 16);

            std::string  p_color = p.config.opt_string("default_filament_colour", 0u);
            unsigned int p_color_value;
            if (!p_color.empty()) {
                unsigned int hash_pos = p_color.find("#");
                p_color_value         = std::stoul(p_color.substr(hash_pos != std::string::npos ? hash_pos + 1 : 0), nullptr, 16);
            } else {
                // Default to black if no color specified in profile. Assume other profiles might be a closer color match.
                // Could be a problem if the target color is also black and there exist a specific profile for that type, vendor and color
                // combination.
                p_color_value = 0;
            }

            // Calculate Euclidean color distance in RGB space
            int dr = ((target_color_value & 0xff) - (p_color_value & 0xff));
            int dg = (((target_color_value >> 8) & 0xff) - ((p_color_value >> 8) & 0xff));
            int db = (((target_color_value >> 16) & 0xff) - ((p_color_value >> 16) & 0xff));
            unsigned int distance = dr * dr + dg * dg + db * db;

            if (distance < best_color_distance) {
                best_color_distance = distance;
                best_match_id       = p.filament_id;
            }
        }
    }
    return best_match_id;
}

} // anonymous namespace

SnapmakerPrinterAgent::SnapmakerPrinterAgent(std::string log_dir) : MoonrakerPrinterAgent(std::move(log_dir)) {}

AgentInfo SnapmakerPrinterAgent::get_agent_info_static()
{
    return AgentInfo{"snapmaker", "Snapmaker", SNAPMAKER_AGENT_VERSION, "Snapmaker printer agent"};
}

std::string SnapmakerPrinterAgent::combine_filament_type(const std::string& type, const std::string& sub_type)
{
    const std::string base = trim_and_upper(type);
    const std::string sub  = trim_and_upper(sub_type);

    if (base.empty())
        return "PLA";

    if (sub.empty() || sub == "NONE")
        return base;

    if (sub == "CF")
        return base + "-CF";
    if (sub == "GF")
        return base + "-GF";
    if (sub == "SNAPSPEED" || sub == "HS")
        return base + " HIGH SPEED";
    if (sub == "SILK")
        return base + " SILK";
    if (sub == "WOOD")
        return base + " WOOD";
    if (sub == "MATTE")
        return base + " MATTE";
    if (sub == "MARBLE")
        return base + " MARBLE";

    // Unrecognized sub-type (brand names like Polylite, Basic, etc.) -- use base type only
    return base;
}

bool SnapmakerPrinterAgent::fetch_filament_info(std::string dev_id)
{
    auto* dev_manager = GUI::wxGetApp().getDeviceManager();
    if (dev_manager) {
        if (MachineObject* obj = dev_manager->get_my_machine(dev_id)) {
            const std::string dev_ip = obj->get_dev_ip();
            if (!dev_ip.empty()) {
                device_info.dev_id   = obj->get_dev_id();
                device_info.dev_ip   = dev_ip;
                device_info.api_key  = obj->get_access_code();
                device_info.base_url = "http://" + dev_ip;
            }
        }
    }

    auto fetch_multiace_json = [&](const std::string& path, nlohmann::json& out_json) -> bool {
        std::string body;
        bool        ok = false;

        std::string multiace_base_url = device_info.base_url;
        const std::string moonraker_suffix = ":7125";
        if (multiace_base_url.size() >= moonraker_suffix.size() &&
            multiace_base_url.compare(multiace_base_url.size() - moonraker_suffix.size(),
                                      moonraker_suffix.size(), moonraker_suffix) == 0) {
            multiace_base_url.erase(multiace_base_url.size() - moonraker_suffix.size());
        }

        auto request = Http::get(join_url(multiace_base_url, path));
        if (!device_info.api_key.empty())
            request.header("X-Api-Key", device_info.api_key);

        request.timeout_connect(3)
            .timeout_max(6)
            .on_complete([&](std::string response, unsigned status) {
                if (status == 200) {
                    body = response;
                    ok   = true;
                }
            })
            .on_error([&](std::string, std::string, unsigned) {})
            .perform_sync();

        if (!ok)
            return false;

        out_json = nlohmann::json::parse(body, nullptr, false, true);
        return !out_json.is_discarded();
    };

    // MultiACE Head Mode: publish only physically loaded heads.
    // /multiace/api/state is authoritative for load_finish state, head topology,
    // ACE assignments, filament identity, and colour.
    nlohmann::json direct_multiace_state;
    if (fetch_multiace_json("/multiace/api/state", direct_multiace_state) &&
        direct_multiace_state.is_object()) {
        std::string direct_mode = direct_multiace_state.value("mode", std::string{});
        if (direct_mode == "single")
            direct_mode = "multi";

        const bool loaded_heads_only =
            GUI::wxGetApp().app_config->get("multiace_filament_view_mode") == "loaded";

        if (direct_mode == "head" &&
            direct_multiace_state.contains("toolheads") &&
            direct_multiace_state["toolheads"].is_array()) {

            // Loaded Heads view: publish only the filament physically loaded
            // in each toolhead. /multiace/api/state is authoritative here,
            // including the currently active ACE slot for an ACE-backed head.
            if (loaded_heads_only) {
                std::vector<AmsTrayData> head_trays;

                for (int head = 0; head < 4; ++head) {
                    const nlohmann::json* toolhead = nullptr;

                    for (const auto& candidate : direct_multiace_state["toolheads"]) {
                        if (candidate.is_object() &&
                            candidate.value("idx", -1) == head) {
                            toolhead = &candidate;
                            break;
                        }
                    }

                    if (!toolhead ||
                        toolhead->value("channel_state", std::string{}) != "load_finish")
                        continue;

                    const std::string material =
                        toolhead->value("material", std::string{});
                    const std::string color =
                        toolhead->value("color", std::string{});

                    if (material.empty() || color.empty())
                        continue;

                    AmsTrayData tray;
                    tray.slot_index   = static_cast<int>(head_trays.size());
                    tray.has_filament = true;
                    tray.tray_type    = trim_and_upper(material);
                    tray.tray_color   = color;
                    tray.tray_info_idx =
                        map_filament_type_to_generic_id(tray.tray_type);

                    head_trays.emplace_back(std::move(tray));
                }

                if (head_trays.empty()) {
                    build_ams_payload(1, 0, head_trays);
                } else {
                    build_ams_payload(
                        1,
                        static_cast<int>(head_trays.size()) - 1,
                        head_trays);
                }

                BOOST_LOG_TRIVIAL(info)
                    << "SnapmakerPrinterAgent::fetch_filament_info: "
                    << "MultiACE Loaded Heads view, loaded="
                    << head_trays.size();

                return true;
            }

            std::vector<AmsTrayData> loaded_trays;
            std::vector<int> used_aces;

            auto set_profile = [&](AmsTrayData& tray) {
                tray.tray_info_idx = map_filament_type_to_generic_id(tray.tray_type);
            };

            auto is_ace_head = [&](int head) {
                if (!direct_multiace_state.contains("ace_heads") ||
                    !direct_multiace_state["ace_heads"].is_array())
                    return false;

                for (const auto& value : direct_multiace_state["ace_heads"]) {
                    if (value.is_number_integer() && value.get<int>() == head)
                        return true;
                }
                return false;
            };

            for (int head = 0; head < 4; ++head) {
                const nlohmann::json* toolhead = nullptr;

                for (const auto& candidate : direct_multiace_state["toolheads"]) {
                    if (candidate.is_object() && candidate.value("idx", -1) == head) {
                        toolhead = &candidate;
                        break;
                    }
                }

                if (!toolhead)
                    continue;

                const bool head_loaded =
                    toolhead->value("channel_state", std::string{}) == "load_finish";

                // Feeder heads only appear when actually loaded.
                if (toolhead->value("feeder", false) &&
                    !toolhead->value("manual", false)) {
                    if (!head_loaded)
                        continue;

                    const std::string material = toolhead->value("material", std::string{});
                    const std::string color    = toolhead->value("color", std::string{});

                    if (!material.empty() && !color.empty()) {
                        AmsTrayData tray;
                        tray.slot_index   = static_cast<int>(loaded_trays.size());
                        tray.has_filament = true;
                        tray.tray_type    = trim_and_upper(material);
                        tray.tray_color   = color;
                        set_profile(tray);
                        loaded_trays.emplace_back(std::move(tray));
                    }
                    continue;
                }

                if (!is_ace_head(head))
                    continue;

                int ace_idx = head;
                if (direct_multiace_state.contains("head_ace") &&
                    direct_multiace_state["head_ace"].is_object()) {
                    const std::string key = std::to_string(head);
                    if (direct_multiace_state["head_ace"].contains(key)) {
                        const auto& value = direct_multiace_state["head_ace"][key];
                        if (value.is_number_integer())
                            ace_idx = value.get<int>();
                    }
                }

                bool already_used = false;
                for (int used : used_aces) {
                    if (used == ace_idx) {
                        already_used = true;
                        break;
                    }
                }
                if (already_used)
                    continue;

                const nlohmann::json* ace = nullptr;
                if (direct_multiace_state.contains("aces") &&
                    direct_multiace_state["aces"].is_array()) {
                    for (const auto& candidate : direct_multiace_state["aces"]) {
                        if (candidate.is_object() &&
                            candidate.value("idx", -1) == ace_idx &&
                            candidate.value("connected", false)) {
                            ace = &candidate;
                            break;
                        }
                    }
                }

                if (!ace || !ace->contains("slots") || !(*ace)["slots"].is_array())
                    continue;

                used_aces.push_back(ace_idx);

                for (const auto& slot : (*ace)["slots"]) {
                    if (!slot.is_object())
                        continue;
                    if (slot.value("state", std::string{}) == "empty")
                        continue;

                    const std::string material = slot.value("material", std::string{});
                    const std::string color    = slot.value("color", std::string{});
                    if (color.empty())
                        continue;

                    AmsTrayData tray;
                    tray.slot_index = static_cast<int>(loaded_trays.size());
                    tray.tray_color = color;

                    if (head_loaded && !material.empty()) {
                        tray.has_filament = true;
                        tray.tray_type    = trim_and_upper(material);
                        set_profile(tray);
                    } else {
                        tray.has_filament          = false;
                        tray.keep_color_when_empty = true;
                    }

                    loaded_trays.emplace_back(std::move(tray));
                }
            }
            if (loaded_trays.empty()) {
                build_ams_payload(1, 0, loaded_trays);
                BOOST_LOG_TRIVIAL(info)
                    << "SnapmakerPrinterAgent::fetch_filament_info: MultiACE Head Mode, loaded=0";
                return true;
            }

            const int logical_count = static_cast<int>(loaded_trays.size());
            build_ams_payload((logical_count + 3) / 4, logical_count - 1, loaded_trays);
            BOOST_LOG_TRIVIAL(info)
                << "SnapmakerPrinterAgent::fetch_filament_info: MultiACE Head Mode, loaded="
                << logical_count;
            return true;
        }
    }
    nlohmann::json multiace;
    if (fetch_multiace_json("/multiace/api/preflight/livedata", multiace) &&
        multiace.contains("live_slots") && multiace["live_slots"].is_array() &&
        multiace.contains("head_ctx") && multiace["head_ctx"].is_object()) {

        const auto& live_slots = multiace["live_slots"];
        const auto& head_ctx   = multiace["head_ctx"];
        const std::string mode = head_ctx.value("mode", std::string{});

        if (mode != "normal") {
            int ace_count = 0;
            for (const auto& row : live_slots) {
                if (!row.is_object())
                    continue;
                const int ace = row.value("ace", -1);
                if (ace >= ace_count)
                    ace_count = ace + 1;
            }

            // Use /api/state for the authoritative connected ACE count.
            // This preserves completely empty ACE units that are absent from live_slots.
            nlohmann::json multiace_state;
            if (fetch_multiace_json("/multiace/api/state", multiace_state) &&
                multiace_state.contains("aces") && multiace_state["aces"].is_array()) {
                for (const auto& ace_obj : multiace_state["aces"]) {
                    if (!ace_obj.is_object() || !ace_obj.value("connected", false))
                        continue;
                    const int ace_idx = ace_obj.value("idx", -1);
                    if (ace_idx >= ace_count)
                        ace_count = ace_idx + 1;
                }
            }

            if (mode == "head" && ace_count == 0)
                ace_count = 1;

            const nlohmann::json feeders =
                (head_ctx.contains("feeders") && head_ctx["feeders"].is_array())
                    ? head_ctx["feeders"]
                    : nlohmann::json::array();

            std::vector<AmsTrayData> multiace_trays;
            multiace_trays.reserve(static_cast<size_t>(ace_count * 4 + feeders.size()));

            auto set_profile = [&](AmsTrayData& tray) {
                tray.tray_info_idx = map_filament_type_to_generic_id(tray.tray_type);
            };

            bool added_state_slots = false;
            if (multiace_state.contains("aces") && multiace_state["aces"].is_array()) {
                for (const auto& ace_obj : multiace_state["aces"]) {
                    if (!ace_obj.is_object() || !ace_obj.value("connected", false))
                        continue;

                    const int ace = ace_obj.value("idx", -1);
                    if (ace < 0 || !ace_obj.contains("slots") || !ace_obj["slots"].is_array())
                        continue;

                    for (const auto& slot_obj : ace_obj["slots"]) {
                        if (!slot_obj.is_object())
                            continue;

                        const int slot = slot_obj.value("idx", -1);
                        const std::string material = slot_obj.value("material", std::string{});
                        const std::string color    = slot_obj.value("color", std::string{});

                        if (slot < 0 || slot > 3 || material.empty() || color.empty())
                            continue;

                        AmsTrayData tray;
                        tray.slot_index   = ace * 4 + slot;
                        tray.has_filament = true;
                        tray.tray_type    = trim_and_upper(material);
                        tray.tray_color   = color;
                        set_profile(tray);
                        multiace_trays.emplace_back(std::move(tray));
                        added_state_slots = true;
                    }
                }
            }

            // Fallback if /api/state did not provide usable ACE slot data.
            if (!added_state_slots) {
                for (const auto& row : live_slots) {
                    if (!row.is_object())
                        continue;

                    const int ace  = row.value("ace", -1);
                    const int slot = row.value("slot", -1);
                    const std::string material = row.value("material", std::string{});
                    const std::string color    = row.value("color", std::string{});

                    if (ace < 0 || slot < 0 || slot > 3 || material.empty() || color.empty())
                        continue;

                    AmsTrayData tray;
                    tray.slot_index   = ace * 4 + slot;
                    tray.has_filament = true;
                    tray.tray_type    = trim_and_upper(material);
                    tray.tray_color   = color;
                    set_profile(tray);
                    multiace_trays.emplace_back(std::move(tray));
                }
            }

            int feeder_lane = ace_count * 4;
            for (const auto& row : feeders) {
                if (!row.is_object())
                    continue;

                const std::string material = row.value("material", std::string{});
                const std::string color    = row.value("color", std::string{});

                if (material.empty() || color.empty())
                    continue;

                AmsTrayData tray;
                tray.slot_index   = feeder_lane++;
                tray.has_filament = true;
                tray.tray_type    = trim_and_upper(material);
                tray.tray_color   = color;
                set_profile(tray);
                multiace_trays.emplace_back(std::move(tray));
            }

            const int total_lanes = feeder_lane;
            if (total_lanes > 0 && !multiace_trays.empty()) {
                const int ams_count = (total_lanes + 3) / 4;
                build_ams_payload(ams_count, total_lanes - 1, multiace_trays);
                BOOST_LOG_TRIVIAL(info)
                    << "SnapmakerPrinterAgent::fetch_filament_info: MultiACE sync, mode="
                    << mode << ", lanes=" << total_lanes << ", loaded=" << multiace_trays.size();
                return true;
            }
        }
    }

    std::string url = join_url(device_info.base_url, "/printer/objects/query?print_task_config&filament_detect");

    std::string response_body;
    bool        success = false;
    std::string http_error;

    auto http = Http::get(url);
    if (!device_info.api_key.empty()) {
        http.header("X-Api-Key", device_info.api_key);
    }
    http.timeout_connect(5)
        .timeout_max(10)
        .on_complete([&](std::string body, unsigned status) {
            if (status == 200) {
                response_body = body;
                success       = true;
            } else {
                http_error = "HTTP error: " + std::to_string(status);
            }
        })
        .on_error([&](std::string body, std::string err, unsigned status) {
            http_error = err;
            if (status > 0) {
                http_error += " (HTTP " + std::to_string(status) + ")";
            }
        })
        .perform_sync();

    if (!success) {
        BOOST_LOG_TRIVIAL(warning) << "SnapmakerPrinterAgent::fetch_filament_info: HTTP request failed: " << http_error;
        return false;
    }

    auto json = nlohmann::json::parse(response_body, nullptr, false, true);
    if (json.is_discarded()) {
        BOOST_LOG_TRIVIAL(warning) << "SnapmakerPrinterAgent::fetch_filament_info: Invalid JSON response";
        return false;
    }

    // Navigate to result.status.print_task_config
    if (!json.contains("result") || !json["result"].contains("status") ||
        !json["result"]["status"].contains("print_task_config")) {
        BOOST_LOG_TRIVIAL(warning) << "SnapmakerPrinterAgent::fetch_filament_info: Missing print_task_config in response";
        return false;
    }

    auto& ptc = json["result"]["status"]["print_task_config"];

    // Read parallel arrays from print_task_config
    auto filament_exist    = ptc.value("filament_exist", std::vector<bool>{});
    auto filament_type     = ptc.value("filament_type", std::vector<std::string>{});
    auto filament_sub_type = ptc.value("filament_sub_type", std::vector<std::string>{});
    auto filament_color    = ptc.value("filament_color_rgba", std::vector<std::string>{});
    auto filament_vendor   = ptc.value("filament_vendor", std::vector<std::string>{});

    const int slot_count = static_cast<int>(filament_exist.size());
    if (slot_count == 0) {
        BOOST_LOG_TRIVIAL(info) << "SnapmakerPrinterAgent::fetch_filament_info: No filament slots reported";
        return false;
    }

    // Read NFC filament_detect data for temperature info (optional)
    nlohmann::json nfc_info;
    if (json["result"]["status"].contains("filament_detect") &&
        json["result"]["status"]["filament_detect"].contains("info")) {
        nfc_info = json["result"]["status"]["filament_detect"]["info"];
    }

    static const std::string empty_str;
    static const std::string default_color = "FFFFFFFF";

    std::vector<AmsTrayData> trays;
    trays.reserve(slot_count);

    for (int i = 0; i < slot_count; ++i) {
        AmsTrayData tray;
        tray.slot_index   = i;
        tray.has_filament = filament_exist[i];

        if (tray.has_filament) {
            tray.tray_type     = combine_filament_type(safe_at(filament_type, i, empty_str),
                                                       safe_at(filament_sub_type, i, empty_str));
            tray.tray_color    = safe_at(filament_color, i, default_color);

            auto* bundle = GUI::wxGetApp().preset_bundle;
            // Try to find a matching preset for this filament based on vendor, type and color.
            // If not found, default to traditional search by type only or generic type mapping.
            if (bundle) {
                std::string vendor      = safe_at(filament_vendor, i, empty_str);
                std::string filament_id = find_closest_color_preset_by_vendor_and_type(bundle->filaments, vendor, tray.tray_type,
                                                                                       tray.tray_color);

                if (!filament_id.empty()) {
                    tray.tray_info_idx = filament_id;
                    BOOST_LOG_TRIVIAL(warning) << "Filament sync: Found manufacturer-specific profile for slot " << i << ": "
                                               << filament_id;
                } else {
                    tray.tray_info_idx = bundle->filaments.filament_id_by_type(tray.tray_type);
                }
            } else {
                tray.tray_info_idx = map_filament_type_to_generic_id(tray.tray_type);
            }

            // Extract NFC temperature data if available
            if (nfc_info.is_array() && i < static_cast<int>(nfc_info.size()) && nfc_info[i].is_object()) {
                auto& nfc_slot = nfc_info[i];
                std::string vendor = nfc_slot.value("VENDOR", "NONE");
                if (vendor != "NONE" && !vendor.empty()) {
                    tray.bed_temp    = nfc_slot.value("BED_TEMP", 0);
                    tray.nozzle_temp = nfc_slot.value("FIRST_LAYER_TEMP", 0);
                }
            }
        }

        trays.emplace_back(std::move(tray));
    }

    build_ams_payload(1, slot_count - 1, trays);
    return true;
}

} // namespace Slic3r
