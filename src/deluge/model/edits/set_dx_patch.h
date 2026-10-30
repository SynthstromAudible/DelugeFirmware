#pragma once

#include "deluge-project/instrument_slot.h"
#include "instruments/deluge/sound_state.h"
#include "model/edits/edit.h"

#include <cstdint>

namespace deluge::edits {

/// @brief Set the DX7 patch of one source of a sound's instrument.
///
/// apply() resolves the slot @c slot names in the model and the sound playing it, gives the source the patch
/// (bridge::applyDxPatch), then makes the slot hold the sound's state again.
///
/// @note Records no undo.
class SetDxPatch final : public Edit {
public:
	/// @param slot   Address of the instrument slot.
	/// @param source Index of the source (oscillator) within the sound; must be below kNumSources and a DX7 source.
	/// @param patch  The DX7 patch state to give the source.
	SetDxPatch(deluge::project::SlotAddress slot, uint8_t source, deluge::instrument::DxPatchState patch)
	    : slot_(slot), source_(source), patch_(patch) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED if the slot, sound or a DX7 source cannot be resolved.
	Error apply(Context& ctx) override;

private:
	deluge::project::SlotAddress slot_;
	uint8_t source_;
	deluge::instrument::DxPatchState patch_;
};

} // namespace deluge::edits
