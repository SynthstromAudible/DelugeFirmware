#pragma once

#include "model/edits/edit.h"
#include <cstdint>

class Clip;

namespace deluge::edits {

/// Set a clip's section (the reification of session view's section-cycle gesture's persistent write). NON-UNDOABLE by
/// design — no Consequence, matching today's behaviour (the gesture calls deleteAllLogs; undo deferred to the
/// command/undo-tree substrate). Writes via `setSection` into the clip's authoritative `DocClip.section` (M3 owner-1
/// — the DocClip is the sole storage; no engine shadow). The view keeps the "find next available section"
/// computation; this reifies only the final write.
class SetClipSection final : public Edit {
public:
	SetClipSection(Clip* clip, uint8_t newSection) : clip_(clip), newSection_(newSection) {}

	Error apply(Context& ctx) override;

private:
	Clip* clip_;
	uint8_t newSection_;
};

} // namespace deluge::edits
