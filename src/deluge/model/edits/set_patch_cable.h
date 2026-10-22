#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"

#include <cstdint>

enum class Polarity : uint8_t;

namespace deluge::edits {

/// @brief Set a patch cable's strength by its address, making the cable first where the sound does not have it.
///
/// A new cable takes the polarity given, in the engine and the record, as legacy's strength page makes a cable with the
/// polarity it shows; an existing cable keeps its own. Making the cable is not an undo step, as in legacy. A user's
/// strength write is SetParamValue's, so it records automation and is undone as any param write; a setup write (an
/// input learn's velocity default) sets the value alone, with no undo step and no automation. The reverb recomputes
/// after a write to the cable it copies (the sidechain's to the reverb send).
class SetPatchCable final : public Edit {
public:
	/// @brief How the strength is written.
	enum class Write : uint8_t {
		USER,  ///< a knob turn: SetParamValue, undone and recorded as any param write
		SETUP, ///< a default set up for the user: the value alone, as legacy's setCurrentValueBasicForSetup
	};

	/// @param cable    The cable's depth address on a synth or a sound drum (with its clip).
	/// @param value    The new strength, as the engine stores it.
	/// @param polarity The polarity a new cable takes.
	/// @param write    How the strength is written.
	SetPatchCable(project::ParamAddress cable, int32_t value, Polarity polarity, Write write = Write::USER)
	    : cable_(cable), value_(value), polarity_(polarity), write_(write) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED where the sound does not resolve or its cable set is full.
	Error apply(Context& ctx) override;

private:
	project::ParamAddress cable_;
	int32_t value_;
	Polarity polarity_;
	Write write_;
};

} // namespace deluge::edits
