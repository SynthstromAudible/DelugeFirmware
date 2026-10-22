#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"

#include <cstdint>

enum class Polarity : uint8_t;

namespace deluge::edits {

/// @brief Set an existing patch cable's polarity, in the engine and the record.
///
/// Not an undo step: legacy's polarity switch logs nothing, so an undo after a flip leaves the cable flipped.
class SetCablePolarity final : public Edit {
public:
	/// @param cable    The cable's depth address on a synth or a sound drum (with its clip).
	/// @param polarity The polarity it takes.
	SetCablePolarity(project::ParamAddress cable, Polarity polarity) : cable_(cable), polarity_(polarity) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED where the sound does not hold the cable.
	Error apply(Context& ctx) override;

private:
	project::ParamAddress cable_;
	Polarity polarity_;
};

} // namespace deluge::edits
