#pragma once

#include "definitions_cxx.hpp" // DrumType
#include "model/edits/edit.h"

class Kit;
class NoteRow;
class Drum;
class ModelStackWithNoteRow;

namespace deluge::edits {

/// @brief Add a new (MIDI/GATE) drum and attach it to a note-row.
///
/// Covers the synchronous branch of InstrumentClipView::createDrumForAuditionedNoteRow. apply() creates the drum, adds
/// it to the kit, and sets it on the row. drum() exposes the created drum for the view's drawDrumName.
///
/// @note Not undoable: the view keeps deleteAllLogs. The async SOUND-drum path is handled by the view, not here.
class AddKitRow final : public Edit {
public:
	AddKitRow(Kit* kit, NoteRow* noteRow, DrumType drumType, ModelStackWithNoteRow* engineStack)
	    : kit_(kit), noteRow_(noteRow), drumType_(drumType), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

	[[nodiscard]] Drum* drum() const { return drum_; }

private:
	Kit* kit_;
	NoteRow* noteRow_;
	DrumType drumType_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
	Drum* drum_ = nullptr;
};

} // namespace deluge::edits
