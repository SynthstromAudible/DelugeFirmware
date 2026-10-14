#pragma once

#include "model/edits/edit.h"
#include <cstdint>

class Clip;

namespace deluge::edits {

/// @brief Nudge a clip's colour offset by a delta.
///
/// Writes the resulting absolute value via `setColourOffset` into the clip's `DocClip.colour_offset`, which is the
/// sole storage of the colour.
///
/// @note Not undoable: no Consequence is recorded.
class SetClipColour final : public Edit {
public:
	/// @param clip   The clip to recolour.
	/// @param offset Delta added to the clip's current colour offset.
	SetClipColour(Clip* clip, int32_t offset) : clip_(clip), offset_(offset) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	Clip* clip_;
	int32_t offset_;
};

} // namespace deluge::edits
