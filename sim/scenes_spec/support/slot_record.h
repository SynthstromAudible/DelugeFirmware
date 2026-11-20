#pragma once

/// @file
/// @brief A slot's record as the scenes read it, for specs that check what an Edit left there.

#include "deluge-project/instrument_slot.h"
#include "instruments/deluge/range_state.h"
#include "instruments/deluge/sound_state.h"
#include "instruments/deluge/state_access.h"
#include "scenes/menu/bind_host.h"

#include <vector>

namespace deluge::scenes::spec {

/// @brief The first oscillator's ranges the record holds at @p slot, read through the scenes' runtime facts, or none
/// where there is no record.
/// @param slot The slot.
/// @return The ranges.
inline std::vector<instrument::RangeState> first_ranges(project::SlotAddress slot) {
	const instrument::SoundState* state = menu::host_runtime_facts().sound_state(slot);
	return state != nullptr ? instrument::source_ranges(*state, instrument::SourceId::A)
	                        : std::vector<instrument::RangeState>{};
}

} // namespace deluge::scenes::spec
