#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// Nudge one held note (across all screens) by ±1, dragging its automation + MPE with it — the reification of one
/// pad's worth of InstrumentClipView::nudgeNotes. apply() opens (or coalesces into, via getNewAction(NOTE_NUDGE,
/// ALLOWED)) the shared Action, nudges the notes, then nudges automation at BOTH the note-row and clip level under
/// that Action. The view keeps the held-pad loop, collision detection, newPos maths, note-row-editor UI, and display.
///
/// Unlike the original call site — which reused the caller's parent ModelStack for the clip-level nudge and thereby
/// invalidated its ModelStackWithNoteRow — apply() builds a fresh clip stack, so the caller's stacks stay valid
/// afterward (the note-row-editor UI update can safely run after apply returns). Behaviour-equivalent.
class NudgeNote final : public Edit {
public:
	NudgeNote(std::optional<project::NoteRowAddress> address, int32_t intendedPos, int32_t offset,
	          uint32_t wrapEditLevel, bool nudgeAutomation, bool nudgeMPE, ModelStackWithNoteRow* engineStack)
	    : address_(address), intendedPos_(intendedPos), offset_(offset), wrapEditLevel_(wrapEditLevel),
	      nudgeAutomation_(nudgeAutomation), nudgeMPE_(nudgeMPE), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	int32_t intendedPos_;
	int32_t offset_;
	uint32_t wrapEditLevel_;
	bool nudgeAutomation_;
	bool nudgeMPE_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
