#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// @brief Nudge one held note (across all screens) by +/-1, dragging its automation + MPE with it.
///
/// One pad's worth of a nudge gesture. apply() opens (or coalesces into, via getNewAction(NOTE_NUDGE, ALLOWED)) the
/// shared Action, nudges the notes, then nudges automation at BOTH the note-row and clip level under that Action.
/// The caller owns the held-pad loop, collision detection, newPos maths, note-row-editor UI, and display.
///
/// apply() builds a fresh clip-level stack, so the caller's stacks stay valid afterward (the note-row-editor UI
/// update can safely run after apply returns).
class NudgeNote final : public Edit {
public:
	/// @param address         Durable note-row address (nullopt if unaddressable).
	/// @param intendedPos     Position of the note to nudge, in ticks.
	/// @param offset          Nudge distance, in ticks (signed).
	/// @param wrapEditLevel   Screen width in ticks: the edit applies at the same offset in every screen of this width.
	/// @param nudgeAutomation Whether to drag the automation along.
	/// @param nudgeMPE        Whether to drag the MPE expression along.
	/// @param engineStack     Borrowed stack naming the note-row; must outlive apply().
	NudgeNote(std::optional<project::NoteRowAddress> address, int32_t intendedPos, int32_t offset,
	          uint32_t wrapEditLevel, bool nudgeAutomation, bool nudgeMPE, ModelStackWithNoteRow* engineStack)
	    : address_(address), intendedPos_(intendedPos), offset_(offset), wrapEditLevel_(wrapEditLevel),
	      nudgeAutomation_(nudgeAutomation), nudgeMPE_(nudgeMPE), engineStack_(engineStack) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	int32_t intendedPos_;
	int32_t offset_;
	uint32_t wrapEditLevel_;
	bool nudgeAutomation_;
	bool nudgeMPE_;
	ModelStackWithNoteRow* engineStack_; ///< Engine borrow.
};

} // namespace deluge::edits
