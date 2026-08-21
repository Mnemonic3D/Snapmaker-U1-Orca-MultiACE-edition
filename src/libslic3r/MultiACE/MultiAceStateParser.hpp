#pragma once

#include "MultiAceTypes.hpp"
#include <nlohmann/json.hpp>

// Pure parsing of a /multiace/api/state JSON response into a LiveState -
// deliberately separated from the actual HTTP fetch (see
// slic3r/Utils/MultiACE/MultiAceClient.hpp) so it can be unit-tested against
// canned JSON fixtures without curl or a live printer.
//
// Consolidates what were three separate Python functions each independently
// fetching and re-parsing the SAME endpoint (lookup_live_slots,
// lookup_head_mode_context, host_has_manual_head) into one parse over one
// response, since they all read from the same static snapshot.

namespace Slic3r { namespace MultiACE {

LiveState parse_state_response(const nlohmann::json& data);

} } // namespace Slic3r::MultiACE
