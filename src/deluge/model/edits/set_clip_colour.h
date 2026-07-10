#pragma once

#include "model/edits/edit.h"
#include <cstdint>

class Clip;

namespace deluge::edits {

/// Nudge a clip's colour offset by a delta (the reification of session view's `clip->colourOffset += offset` gesture).
/// NON-UNDOABLE by design — no Consequence, matching today's behaviour (undo deferred to the command/undo-tree
/// substrate). Writes the absolute value via `setColourOffset` into the clip's authoritative `DocClip.colour_offset`
/// (M3 owner-1 — the DocClip is the sole storage; no engine shadow).
class SetClipColour final : public Edit {
public:
	SetClipColour(Clip* clip, int32_t offset) : clip_(clip), offset_(offset) {}

	Error apply(Context& ctx) override;

private:
	Clip* clip_;
	int32_t offset_;
};

} // namespace deluge::edits
