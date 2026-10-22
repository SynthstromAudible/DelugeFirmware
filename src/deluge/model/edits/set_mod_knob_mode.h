#pragma once

#include "deluge-project/setting_group.h"
#include "model/edits/edit.h"

#include <cstdint>
#include <variant>

namespace deluge::edits {

/// @brief Whose gold knobs a mode belongs to: the song's own, or a track's (a kit row's knobs are its kit's).
using KnobModeOwner = std::variant<project::SongAddress, project::TrackAddress>;

/// @brief Set which mod button's pair of params the gold knobs edit, in the record and the engine.
///
/// Not an undo step: legacy's mod buttons log nothing.
class SetModKnobMode final : public Edit {
public:
	/// @param owner The song or the track.
	/// @param mode  The mod button, below kNumModButtons.
	SetModKnobMode(KnobModeOwner owner, uint8_t mode) : owner_(owner), mode_(mode) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED for a mode past the last button or a track the song does not hold.
	Error apply(Context& ctx) override;

private:
	KnobModeOwner owner_;
	uint8_t mode_;
};

/// @brief Set @p owner's knob mode to @p mode through SetModKnobMode on the current song.
/// @param owner The song or the track.
/// @param mode  The mod button.
/// @return Whether the Edit applied.
bool modKnobModeFor(KnobModeOwner owner, uint8_t mode);

} // namespace deluge::edits
