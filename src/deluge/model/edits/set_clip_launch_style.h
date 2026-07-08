#pragma once

#include "definitions_cxx.hpp" // LaunchStyle
#include "model/edits/edit.h"

class Clip;

namespace deluge::edits {

/// Set a clip's launch style (the reification of LaunchStyleMenu::selectEncoderAction). NON-UNDOABLE by design — no
/// Consequence, matching today's behaviour (undo deferred to the command/undo-tree substrate, not Action/Consequence).
/// Dual-writes the DOM shadow `DocClip.launch_style` (M3 owner-1 slice 2). Funnel-phase engine borrow.
class SetClipLaunchStyle final : public Edit {
public:
	SetClipLaunchStyle(Clip* clip, LaunchStyle newStyle) : clip_(clip), newStyle_(newStyle) {}

	Error apply(Context& ctx) override;

private:
	Clip* clip_;
	LaunchStyle newStyle_;
};

} // namespace deluge::edits
