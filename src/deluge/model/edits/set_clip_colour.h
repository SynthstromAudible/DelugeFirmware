#pragma once

#include "model/edits/edit.h"
#include <cstdint>

class Clip;

namespace deluge::edits {

/// Nudge a clip's colour offset by a delta (the reification of session view's `clip->colourOffset += offset` gesture).
/// NON-UNDOABLE by design — no Consequence, matching today's behaviour (undo deferred to the command/undo-tree
/// substrate). Dual-writes the resulting absolute value into the DOM shadow `DocClip.colour_offset` (M3 owner-1
/// slice 2). Funnel-phase engine borrow.
class SetClipColour final : public Edit {
public:
	SetClipColour(Clip* clip, int32_t offset) : clip_(clip), offset_(offset) {}

	Error apply(Context& ctx) override;

private:
	Clip* clip_;
	int32_t offset_;
};

} // namespace deluge::edits
