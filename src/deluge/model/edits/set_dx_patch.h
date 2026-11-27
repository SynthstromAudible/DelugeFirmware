#pragma once

#include "instruments/classic/sound_state.h"
#include "model/edits/edit.h"
#include "project/instrument_slot.h"

#include <cstdint>

namespace deluge::edits {

/// @brief Set the patch of a sound's DX7 engine.
///
/// apply() resolves the slot @c slot names in the model and the DX7-mode sound playing it, gives its DX7 the patch
/// (bridge::applyDx7Patch), then makes the slot hold the sound's state again.
///
/// @note Records no undo.
class SetDxPatch final : public Edit {
public:
	/// @param slot  Address of the instrument slot.
	/// @param patch The DX7 patch state to give the sound's DX7.
	SetDxPatch(deluge::project::SlotAddress slot, deluge::instruments::classic::DxPatchState patch)
	    : slot_(slot), patch_(patch) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED if the slot cannot be resolved or its sound is not in DX7 mode.
	Error apply(Context& ctx) override;

private:
	deluge::project::SlotAddress slot_;
	deluge::instruments::classic::DxPatchState patch_;
};

} // namespace deluge::edits
