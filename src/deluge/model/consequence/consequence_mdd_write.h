#pragma once
#include "model/consequence/consequence.h"
#include "model/note/note_row_vector.h"

// Owns the other version of the complete grid, including row MPE/automation.
// Undo/redo exchange ownership without allocating or copying notes.
class ConsequenceMddWrite final : public Consequence {
public:
	explicit ConsequenceMddWrite(InstrumentClip* clip);
	Error revert(TimeType time, ModelStack* modelStack) override;
	void capture(NoteRowVector& oldRows);

private:
	InstrumentClip* clip_;
	NoteRowVector rows_;
	SequenceDirection direction_;
	bool reversed_;
	int32_t yScroll_;
};
