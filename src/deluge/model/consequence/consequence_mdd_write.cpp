#include "model/consequence/consequence_mdd_write.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include <utility>

ConsequenceMddWrite::ConsequenceMddWrite(InstrumentClip* clip)
    : clip_(clip), direction_(clip->sequenceDirectionMode), reversed_(clip->currentlyPlayingReversed),
      yScroll_(clip->yScroll) {
}

void ConsequenceMddWrite::capture(NoteRowVector& oldRows) {
	rows_.swapStateWith(&oldRows);
}

bool ConsequenceMddWrite::prepareAutomation(ModelStackWithTimelineCounter*) {
	hasAutomation_ = clip_->output->type == OutputType::SYNTH;
	bakedCustom_ = hasAutomation_ ? 15 : 0;
	if (!hasAutomation_)
		return true;
	if (clip_->loopLength <= 0 || clip_->loopLength > 1048576)
		return false;
	auto core = clip_->mdd.core;
	unsigned nodes = 0;
	std::array<int32_t, 4> previous{};
	for (uint32_t tick = 0; tick < uint32_t(clip_->loopLength); ++tick) {
		if (!(tick & 31u))
			AudioEngine::routineWithClusterLoading(false);
		auto frame = core.advance(tick);
		for (int i = 0; i < 4; ++i) {
			int32_t value = snake::normalizedQ31(frame.custom[i]);
			if (tick && value == previous[i])
				continue;
			if (++nodes > 65536)
				return false;
			auto& prepared = customAutomation_[i];
			int index = prepared.nodes.insertAtKey(tick, true);
			if (index < 0)
				return false;
			auto* node = prepared.nodes.getElement(index);
			node->value = value;
			node->interpolated = false;
			if (!tick)
				prepared.value = value;
			previous[i] = value;
		}
	}
	return true;
}
void ConsequenceMddWrite::swapAutomation(ModelStackWithTimelineCounter* stack) {
	if (hasAutomation_) {
		auto* patched = clip_->paramManager.getPatchedParamSet();
		auto* model = stack->addOtherTwoThingsButNoNoteRow(clip_->output->toModControllable(), &clip_->paramManager)
		                  ->addParamCollectionSummary(clip_->paramManager.getPatchedParamSetSummary());
		for (int i = 0; i < 4; ++i) {
			auto* ap = patched->getAutoParamFromId(
			    model->addParamId(deluge::modulation::params::GLOBAL_MDD_CUSTOM_1 + i), false);
			ap->autoParam->swapState(&customAutomation_[i], ap);
		}
	}
	std::swap(clip_->mddBakedCustom, bakedCustom_);
	clip_->publishMddCustom();
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
	swapAutomation(stack);
	clip_->noteRowsNumTicksBehindClip = 0;
	clip_->ticksTilNextNoteRowEvent = 0;
	clip_->resumePlayback(stack, false);
	clip_->expectEvent();
	return Error::NONE;
}
