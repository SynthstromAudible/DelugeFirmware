#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;
class CopiedNoteRow;
class Action;

namespace deluge::edits {

/// Paste one copied note-row into one target note-row (one iteration of InstrumentClipView::pasteNotes' kit/melodic
/// loop). apply() calls NoteRow::paste, which self-records the undo into the shared Action; the view keeps the loop,
/// copy buffer, target-row resolution, Action creation, clearArea, and display. The Error return preserves the site's
/// `goto ramError` branch (NoteRow::paste currently always succeeds; the real RAM failure — getOrCreateNoteRowForYNote
/// null — stays in the view).
class PasteNoteRow final : public Edit {
public:
	PasteNoteRow(std::optional<project::NoteRowAddress> address, CopiedNoteRow* copiedRow, float scaleFactor,
	             int32_t screenEndPos, Action* action, ModelStackWithNoteRow* engineStack)
	    : address_(address), copiedRow_(copiedRow), scaleFactor_(scaleFactor), screenEndPos_(screenEndPos),
	      action_(action), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	CopiedNoteRow* copiedRow_; ///< Funnel-phase copy-buffer borrow.
	float scaleFactor_;
	int32_t screenEndPos_;
	Action* action_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
