#pragma once

#include "deluge-project/instrument_slot.h"
#include "instruments/deluge/sound_state.h"
#include "model/edits/edit.h"

#include <cstdint>
#include <vector>

namespace deluge::edits {

/// @brief Set the ranges of one source of a sound's instrument.
///
/// apply() resolves the slot @c slot names in the model and the sound playing it, changes the source's ranges in
/// place (bridge::applyRanges), then makes the slot hold the sound's state again. Records no undo.
class SetSourceRanges final : public Edit {
public:
	SetSourceRanges(deluge::project::SlotAddress slot, uint8_t source,
	                std::vector<deluge::instrument::RangeState> ranges)
	    : slot_(slot), source_(source), ranges_(std::move(ranges)) {}

	Error apply(Context& ctx) override;

private:
	deluge::project::SlotAddress slot_;
	uint8_t source_;
	std::vector<deluge::instrument::RangeState> ranges_;
};

} // namespace deluge::edits
