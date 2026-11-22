#pragma once

/// @file
/// @brief A slot's record as the scenes read it, for specs that check what an Edit left there.

#include "instruments/classic/sound_state.h"
#include "instruments/classic/state_access.h"
#include "project/instrument_slot.h"
#include "scenes/menu/bind_host.h"

#include <vector>

namespace deluge::scenes::spec {

/// @brief The first oscillator's ranges the record holds at @p slot, read through the scenes' sound port, or none
/// where there is no record.
/// @param slot The slot.
/// @return The ranges.
inline std::vector<synth::SourceRange> first_ranges(project::SlotAddress slot) {
	const instruments::classic::SoundState* state = menu::host_ports().sound->sound_state(slot);
	return state != nullptr ? instruments::classic::source_ranges(*state, instruments::classic::SourceId::A)
	                        : std::vector<synth::SourceRange>{};
}

} // namespace deluge::scenes::spec
