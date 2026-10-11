#include "model/consequence/consequence_mdd_write.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include <utility>

ConsequenceMddWrite::ConsequenceMddWrite(InstrumentClip* clip)
    : clip_(clip), direction_(clip->sequenceDirectionMode), reversed_(clip->currentlyPlayingReversed),
      yScroll_(clip->yScroll) {
}

void ConsequenceMddWrite::capture(NoteRowVector& oldRows) {
	rows_.swapStateWith(&oldRows);
}

Error ConsequenceMddWrite::revert(TimeType, ModelStack* modelStack) {
	auto* stack = modelStack->addTimelineCounter(clip_);
	clip_->stopAllNotesPlaying(stack);
	clip_->noteRows.swapStateWith(&rows_);
	std::swap(clip_->sequenceDirectionMode, direction_);
	std::swap(clip_->currentlyPlayingReversed, reversed_);
	std::swap(clip_->yScroll, yScroll_);
	// Undo WRITE restores the previous Normal grid, rather than restarting MDD.
	clip_->mddEnabled = clip_->mddRequested = clip_->mddPending = false;
	clip_->mdd.back();
	clip_->noteRowsNumTicksBehindClip = 0;
	clip_->ticksTilNextNoteRowEvent = 0;
	clip_->resumePlayback(stack, false);
	clip_->expectEvent();
	return Error::NONE;
}
