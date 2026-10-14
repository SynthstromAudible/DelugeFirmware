#pragma once

#include "definitions_cxx.hpp" // LaunchStyle
#include "model/edits/edit.h"

class Clip;

namespace deluge::edits {

/// @brief Set a clip's launch style.
///
/// Writes via `setLaunchStyle` into the clip's `DocClip.launch_style`, which is the sole storage of the style.
///
/// @note Not undoable: no Consequence is recorded.
class SetClipLaunchStyle final : public Edit {
public:
	/// @param clip     The clip to change.
	/// @param newStyle The launch style to set.
	SetClipLaunchStyle(Clip* clip, LaunchStyle newStyle) : clip_(clip), newStyle_(newStyle) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	Clip* clip_;
	LaunchStyle newStyle_;
};

} // namespace deluge::edits
