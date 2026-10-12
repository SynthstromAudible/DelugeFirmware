#pragma once
#include "model/consequence/consequence.h"
#include "model/note/note_row_vector.h"
#include "modulation/automation/auto_param.h"
#include <array>

class ModelStackWithTimelineCounter;

// Owns the other version of the complete grid, including row MPE/automation.
// Undo/redo exchange ownership without allocating or copying notes.
class ConsequenceMddWrite final : public Consequence {
public:
	explicit ConsequenceMddWrite(InstrumentClip* clip);
	Error revert(TimeType time, ModelStack* modelStack) override;
	void capture(NoteRowVector& oldRows);
	bool prepareAutomation(ModelStackWithTimelineCounter* stack);
	void swapAutomation(ModelStackWithTimelineCounter* stack);

private:
	InstrumentClip* clip_;
	std::array<AutoParamState, 4> customAutomation_{};
	uint8_t bakedCustom_ = 0;
	bool hasAutomation_ = false;
	NoteRowVector rows_;
	SequenceDirection direction_;
	bool reversed_;
	int32_t yScroll_;
};
