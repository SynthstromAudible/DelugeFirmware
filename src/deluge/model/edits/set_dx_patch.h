#pragma once

#include "deluge-project/instrument_slot.h"
#include "engines/deluge/sound_state.h"
#include "model/edits/edit.h"

#include <cstdint>

namespace deluge::edits {

/// Set the DX7 patch of one source of a sound's instrument. apply() resolves the slot @c slot names in the model and
/// the sound playing it, gives the source the patch (bridge::applyDxPatch), then makes the slot hold the sound's state
/// again. Records no undo.
class SetDxPatch final : public Edit {
public:
	SetDxPatch(deluge::project::SlotAddress slot, uint8_t source, deluge::instrument::DxPatchState patch)
	    : slot_(slot), source_(source), patch_(patch) {}

	Error apply(Context& ctx) override;

private:
	deluge::project::SlotAddress slot_;
	uint8_t source_;
	deluge::instrument::DxPatchState patch_;
};

} // namespace deluge::edits
