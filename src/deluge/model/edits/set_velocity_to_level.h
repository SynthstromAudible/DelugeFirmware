#pragma once

#include "model/edits/edit.h"

class MIDICable;

namespace deluge::edits {

/// @brief Give every synth and sound kit row whose input is a cable that cable's velocity-to-level default, in every
///        clip it plays in -- the device menu's "for everything".
///
/// A default set up for the user: not an undo step, as legacy's `grabVelocityToLevelFromMIDICable` is not.
class SetVelocityToLevelEverywhere final : public Edit {
public:
	/// @param cable The cable whose default the inputs on it take.
	explicit SetVelocityToLevelEverywhere(const ::MIDICable& cable) : cable_(cable) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	const ::MIDICable& cable_;
};

} // namespace deluge::edits
