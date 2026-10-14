#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;
class CopiedNoteRow;
class Action;

namespace deluge::edits {

/// @brief Paste one copied note-row into one target note-row.
///
/// One iteration of the paste gesture's kit/melodic row loop. apply() calls NoteRow::paste, which self-records the
/// undo into the shared Action; the caller owns the loop, copy buffer, target-row resolution, Action creation,
/// clearArea, and display. apply() returns INSUFFICIENT_RAM if NoteRow::paste fails (it currently always succeeds;
/// the real RAM failure, getOrCreateNoteRowForYNote returning null, is handled by the caller).
class PasteNoteRow final : public Edit {
public:
	/// @param address     Durable note-row address of the target (nullopt if unaddressable).
	/// @param copiedRow   The copied row to paste (borrowed from the copy buffer).
	/// @param scaleFactor Horizontal scale applied to the pasted notes.
	/// @param screenEndPos End of the pasted region, in ticks.
	/// @param action      The shared Action that records the undo.
	/// @param engineStack Borrowed stack naming the target note-row; must outlive apply().
	PasteNoteRow(std::optional<project::NoteRowAddress> address, CopiedNoteRow* copiedRow, float scaleFactor,
	             int32_t screenEndPos, Action* action, ModelStackWithNoteRow* engineStack)
	    : address_(address), copiedRow_(copiedRow), scaleFactor_(scaleFactor), screenEndPos_(screenEndPos),
	      action_(action), engineStack_(engineStack) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	CopiedNoteRow* copiedRow_; ///< Copy-buffer borrow.
	float scaleFactor_;
	int32_t screenEndPos_;
	Action* action_;
	ModelStackWithNoteRow* engineStack_; ///< Engine borrow.
};

} // namespace deluge::edits
