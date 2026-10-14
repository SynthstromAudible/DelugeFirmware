#pragma once

#include "model/edits/edit.h"
#include <cstdint>

class Clip;

namespace deluge::edits {

/// @brief Set a clip's section (the persistent write of session view's section-cycle gesture).
///
/// Writes via `setSection` into the clip's `DocClip.section`, which is the sole storage of the section. The view
/// owns the "find next available section" computation; this edit performs only the final write.
///
/// @note Not undoable: no Consequence is recorded (the gesture clears all undo logs).
class SetClipSection final : public Edit {
public:
	/// @param clip       The clip to move.
	/// @param newSection Section index to set.
	SetClipSection(Clip* clip, uint8_t newSection) : clip_(clip), newSection_(newSection) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	Clip* clip_;
	uint8_t newSection_;
};

} // namespace deluge::edits
