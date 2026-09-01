#include "MultiAcePostProcess.hpp"
#include "MultiAceClient.hpp"

#include "libslic3r/Exception.hpp"
#include "libslic3r/MultiACE/ColorMatchEngine.hpp"
#include "libslic3r/MultiACE/GCodeRewrite.hpp"
#include "libslic3r/MultiACE/GCodeToolchangeScan.hpp"
#include "libslic3r/MultiACE/HeadModeMatch.hpp"
#include "libslic3r/MultiACE/MultiAceTypes.hpp"

#include <boost/nowide/fstream.hpp>
#include <sstream>

namespace Slic3r { namespace MultiACE {

namespace {

std::map<int, std::string> config_strings_to_map(const DynamicPrintConfig& config, const char* key)
{
    std::map<int, std::string> out;
    auto* opt = config.option<ConfigOptionStrings>(key);
    if (!opt)
        return out;
    for (size_t i = 0; i < opt->values.size(); ++i)
        out[static_cast<int>(i)] = opt->values[i];
    return out;
}

} // namespace

void run_multiace_postprocess(const std::string& gcode_path, const DynamicPrintConfig& config)
{
    auto* enabled_opt = config.option<ConfigOptionBool>("multiace_native_postprocess");
    if (!enabled_opt || !enabled_opt->value)
        return;

    auto* host_opt = config.option<ConfigOptionString>("print_host");
    std::string host = host_opt ? host_opt->value : std::string();
    if (host.empty())
        throw Slic3r::RuntimeError("MultiACE native post-processing is enabled but no Print Host is set.");

    boost::nowide::ifstream ifs(gcode_path.c_str(), std::ios::binary);
    if (!ifs)
        throw Slic3r::RuntimeError("MultiACE post-processing could not open the gcode file: " + gcode_path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string raw = ss.str();
    ifs.close();

    // Explicitly normalize \r\n/\r -> \n regardless of what convention the
    // on-disk temp file happens to use, the same way Python's text-mode
    // open().read() would - every ported function assumes \n-only input
    // (confirmed the hard way while testing rewrite(): a raw \r\n input
    // silently corrupted offset math and line splitting several different
    // ways before this normalization was added). Reading in binary above
    // and normalizing by hand here, rather than trusting the platform's
    // text-mode translation, keeps this independent of whatever Orca's own
    // gcode writer happens to do.
    std::string gcode;
    gcode.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\r') {
            gcode += '\n';
            if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
        } else {
            gcode += raw[i];
        }
    }

    // ONE fetch replaces the Python script's 3 separate calls to the same
    // endpoint (host_has_manual_head / lookup_live_slots / lookup_head_mode_context).
    std::optional<LiveState> state_opt = fetch_live_state(host);
    if (!state_opt)
        throw Slic3r::RuntimeError("MultiACE live-lookup to " + host + " failed - printer unreachable.");
    LiveState& state = *state_opt;

    if (state.has_manual_head)
        throw Slic3r::RuntimeError(
            "MultiACE: a toolhead is set to manual - live-lookup colour matching is disabled. "
            "Switch the head back to auto and re-print.");

    bool head_mode = (state.head_mode.mode == "head");
    if (state.slots.empty() && state.head_mode.feeders.empty())
        throw Slic3r::RuntimeError(
            "MultiACE live-lookup returned 0 loaded slots/feeders - load filaments first and re-print.");

    std::map<int, std::string> slicer_colors = config_strings_to_map(config, "filament_colour");
    std::map<int, std::string> slicer_types = config_strings_to_map(config, "filament_type");

    // avail_slots = live_slots + feeders' materials, matching the Python
    // source's material-availability pre-check (feeders carry filament too -
    // a material only loaded on a feeder must not be reported as missing).
    std::vector<Slot> avail_slots = state.slots;
    for (const auto& f : state.head_mode.feeders) {
        Slot s;
        s.material = f.material;
        avail_slots.push_back(s);
    }
    std::set<std::string> missing_mats = check_material_availability(slicer_types, avail_slots);
    if (!missing_mats.empty()) {
        std::string msg = "MultiACE: the slicer needs filament(s) of material(s) not loaded anywhere on the printer:";
        for (const auto& m : missing_mats) msg += " " + m;
        throw Slic3r::RuntimeError(msg);
    }

    std::set<int> feeder_heads_used;
    std::optional<std::map<int, std::pair<int, int>>> initial_targets;

    if (head_mode) {
        std::map<int, int> ace_head_of_ace;
        for (int h : state.head_mode.ace_heads) {
            auto it = state.head_mode.head_ace.find(h);
            int ace_idx = (it != state.head_mode.head_ace.end()) ? it->second : h;
            ace_head_of_ace[ace_idx] = h;
        }

        std::vector<PinnedHead> pinned_heads;
        for (const auto& f : state.head_mode.feeders) {
            PinnedHead p;
            p.head = f.head;
            p.material = f.material;
            p.color = f.color;
            pinned_heads.push_back(p);
        }

        HeadModeLayout layout = compute_head_mode_layout(
            slicer_colors, slicer_types, pinned_heads, state.slots, ace_head_of_ace, std::nullopt);

        for (int h : layout.pinned)
            feeder_heads_used.insert(h);

        if (!layout.feasible)
            throw Slic3r::RuntimeError(
                "MultiACE: too few loaded feeders/slots for the colours this gcode needs - load more filament and re-print.");

        auto [new_gcode, head_mode_swaps] = apply_head_mode_rewrite(gcode, layout.assignment);
        (void)head_mode_swaps;
        gcode = std::move(new_gcode);

        std::map<int, std::pair<int, int>> targets;
        for (const auto& kv : layout.assignment)
            if (kv.second.kind == "ace")
                targets[kv.second.head] = {kv.second.ace, kv.second.slot};
        initial_targets = std::move(targets);
    } else {
        MatchResult mr = match_colors_to_slots(slicer_colors, state.slots, 4, slicer_types, false, std::nullopt);
        bool any_no_slot = false;
        for (const auto& kv : mr.info)
            if (kv.second.tier == "no_slot") { any_no_slot = true; break; }
        if (any_no_slot)
            throw Slic3r::RuntimeError(
                "MultiACE: too few loaded slots - load more filament and re-print.");
        if (!mr.remap.empty())
            gcode = apply_remap(gcode, mr.remap);
    }

    // Module 6 (the console loadout/layer-feasibility report) is
    // deliberately not called here - it only ever gets APPLIED to the gcode
    // under --layer/--optimize, neither of which the real invocation ever
    // passes, so it has no effect on print correctness. Diagnostic-only,
    // left for a later pass.

    if (head_mode) {
        // apply_head_mode_rewrite() above already fully rewrote the body -
        // do NOT also run rewrite() here, same reasoning as the Python source.
    } else {
        RewriteResult rr = rewrite(gcode, feeder_heads_used);
        gcode = std::move(rr.gcode);
    }

    auto [gcode_with_autoload, auto_load_count] = inject_auto_load(gcode, feeder_heads_used, initial_targets);
    (void)auto_load_count;
    gcode = std::move(gcode_with_autoload);

    boost::nowide::ofstream ofs(gcode_path.c_str(), std::ios::binary);
    if (!ofs)
        throw Slic3r::RuntimeError("MultiACE post-processing could not write the gcode file: " + gcode_path);
    ofs << gcode;
}

} } // namespace Slic3r::MultiACE
