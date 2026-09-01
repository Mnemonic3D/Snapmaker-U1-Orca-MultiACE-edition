#pragma once

#include "libslic3r/PrintConfig.hpp"

#include <string>

// Native, in-process replacement for post_process_virtual_toolheads.py's
// `main()` live-lookup path (the one Orca's post_process field actually
// invokes: `--live-lookup`, no other flags). Ported logic lives in
// libslic3r/MultiACE/ (pure, testable) and slic3r/Utils/MultiACE/
// MultiAceClient (the HTTP fetch); this function is the orchestration layer
// that main() itself provided in the Python source - reading the config,
// calling the pieces in the right order, and translating main()'s
// print+sys.exit() error handling into exceptions Orca's own upload/export
// pipeline already knows how to show to the user.

namespace Slic3r { namespace MultiACE {

// Reads and rewrites the gcode file at `gcode_path` in place. `config` must
// be the resolved full_print_config() for this print - used for
// filament_colour/filament_type/print_host, exactly like the Python script
// read them (the Python script parsed print_host out of Orca's on-disk
// config and filament_colour/filament_type out of the gcode's own header
// comments; both are already directly available here without either step).
//
// No-op (returns immediately) unless multiace_native_postprocess is enabled
// in `config` - this is the single gate that keeps this function from
// affecting any printer/user that hasn't explicitly opted in.
//
// Throws Slic3r::RuntimeError on any condition the Python script would have
// aborted on (sys.exit(1)/sys.exit(2)): unreachable printer, a manual
// toolhead loaded, a required material not loaded anywhere, or too few
// loaded slots/feeders for the colours this gcode needs. Letting the
// exception propagate is deliberate - it reaches the same call sites that
// already wrap run_post_process_scripts() in try/catch and show the user an
// error dialog instead of silently printing with the wrong material.
void run_multiace_postprocess(const std::string& gcode_path, const DynamicPrintConfig& config);

} } // namespace Slic3r::MultiACE
