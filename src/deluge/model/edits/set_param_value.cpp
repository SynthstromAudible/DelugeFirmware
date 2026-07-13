#include "model/edits/set_param_value.h"

#include "deluge-project/param_automation.h" // DocAutoParam::nodes
#include "deluge-project/song.h"             // Song::resolveParam
#include "gui/ui/ui.h"                       // getRootUI
#include "gui/views/automation_view.h"       // automationView interpolation flags
#include "io/debug/log.h"
#include "model/action/action.h" // recordParamChangeIfNotAlreadySnapshotted (region undo snapshot)
#include "model/action/action_logger.h"
#include "model/clip/clip.h" // Clip::getCurrentlyRecordingLinearly (region context)
#include "model/model_stack.h"
#include "model/snapshot/param_snapshot.h" // ParamSnapshot::applyPatch (M4a event-patch)
#include "model/song/song.h"               // engine Song (tc != song comparison)
#include "model/timeline_counter.h"
#include "modulation/automation/auto_param.h"
#include "playback/playback_handler.h"

namespace deluge::edits {

namespace {
// Gather the timeline/playback context the region patch needs, reading it from the (still-borrowed, M4a) engine
// stack at EXACTLY the sites AutoParam::setValueForRegion / homogenizeRegion read it — so applyPatch's region path
// reproduces the in-line poke. The north-star read-side flip will source these from the model/snapshot instead.
// livePos/reversed are consulted by the kernel only when the clock is active (auto_param.cpp routes to
// yesChangeCurrentValue otherwise), so they are gathered lazily — matching the engine and staying safe on a
// stopped-transport stack.
deluge::snapshot::RegionContext gatherRegionContext(ModelStackWithAutoParam* engineStack) {
	deluge::snapshot::RegionContext r{};
	r.effectiveLength = engineStack->getLoopLength();
	// Interpolation flags mirror AutoParam::setValueForRegion: the automation view supplies them, else both false.
	if (getRootUI() == &automationView) {
		r.interpolateLeftNode = automationView.interpolationBefore;
		r.interpolateRightNode = automationView.interpolationAfter;
	}
	// recordingLinearly mirrors AutoParam::homogenizeRegion's guarded Clip cast (the Song-as-timeline case is not a
	// Clip, so it stays false there).
	TimelineCounter* tc = engineStack->getTimelineCounterAllowNull();
	if (tc != nullptr && tc != static_cast<TimelineCounter*>(engineStack->song)) {
		r.recordingLinearly = static_cast<Clip*>(tc)->getCurrentlyRecordingLinearly();
	}
	r.clockActive = playbackHandler.isEitherClockActive();
	if (r.clockActive) {
		r.livePos = engineStack->getLivePos();
		r.currentlyPlayingReversed = engineStack->isCurrentlyPlayingReversed();
	}
	return r;
}
} // namespace

Error SetParamValue::apply(Context& ctx) {
	// The Edit owns its undo record (migration plan M1: do-site == undo-record-site): snapshot the before-state into
	// the open Action up front, replicating the guard at the interior log site (auto_param.cpp
	// setCurrentValueInResponseToUserInput, "record value change") evaluated pre-mutation. The interior log then
	// dedupes via Action::containsConsequenceParamChange — this pre-log is idempotent, and stays so until the M1c
	// sweep retires the interior log (other callers still rely on it).
	//
	// Faithfulness: the interior log fires iff execution reaches its site AND (nodes.empty() || !isPlaying) AND
	// value != currentValue. It is reached on the SCALAR path only (a nonzero-length region edit goes to
	// setValueForRegion, which manages its own Action). The recording-armed branch never yields an
	// unautomated-value-change Action — it either early-exits (shift-delete / clip-too-short) or falls through with
	// nodes now non-empty — so excluding every (isPlaying && recordingArmed) case reproduces it exactly.
	// setValuePossiblyForRegion sends (length != 0 && timelineCounterIsSet) to setValueForRegion; everything else is
	// the scalar path that reaches the interior unautomated-change log.
	const bool scalarPath = length_ == 0 || !engineStack_->timelineCounterIsSet();

	// Hoisted out of the pre-log so the commit-flip decision below can reuse them. recordingActive is the exact
	// condition under which the scalar poke RECORDS automation (setCurrentValueInResponseToUserInput's recording
	// branch) — that case is NOT flipped (it writes nodes); everything else is the plain scalar tail.
	bool isPlaying = false;
	bool recordingActive = false;
	if (scalarPath) {
		isPlaying =
		    playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn
		    && (!engineStack_->timelineCounterIsSet() || engineStack_->getTimelineCounter()->isPlayingAutomationNow());
		const bool recordingArmed = playbackHandler.recording != RecordingMode::OFF
		                            && engineStack_->timelineCounterIsSet()
		                            && engineStack_->getTimelineCounter()->armedForRecording;
		recordingActive = isPlaying && recordingArmed;

		if (newValue_ != engineStack_->autoParam->getCurrentValue() && !recordingActive
		    && (engineStack_->doc()->nodes.empty() || !isPlaying)) {
			actionLogger.recordUnautomatedParamChange(engineStack_);
		}
	}

	// M4a commit-flip: the persistent scalar store write is now model-first (ParamSnapshot::applyPatch is the sole
	// writer), and the engine derives its RT state (cursor/interpolation/notify/latch) from the store. Taken only
	// for a resolvable scalar lane that isn't recording automation. Region, recording-active scalar, and
	// unaddressable/not-yet-relocated lanes keep the in-line poke as the writer (it resolves via doc() with the
	// relocate/engine-store fallback, so no lane is dropped) — behaviour-identical to before for those.
	if (scalarPath && !recordingActive && address_.has_value() && ctx.model.resolveParam(*address_) != nullptr) {
		const int32_t oldValue = engineStack_->autoParam->getCurrentValue();
		deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, *address_, newValue_, 0, 0, std::nullopt);
		engineStack_->autoParam->projectScalarFromStore(engineStack_, oldValue, isPlaying);
		return Error::NONE;
	}

	// M4a region commit-flip: the STOPPED-TRANSPORT region store write is also model-first — applyPatch writes the
	// nodes + current_value (via the proven DOM kernel), and the engine derives its RT tail (cursor + notify) from
	// the store. The undo snapshot is reordered AHEAD of the patch (the exact getNewAction + record call
	// setValueForRegion makes at auto_param.cpp:1109-1112, so held-pad region drags coalesce identically). Clock-
	// active region (interpolation-reset / UI-notify sub-branch not surfaced by the kernel) and unresolvable lanes
	// keep the in-line poke below.
	if (length_ != 0 && engineStack_->timelineCounterIsSet() && !playbackHandler.isEitherClockActive()
	    && address_.has_value() && ctx.model.resolveParam(*address_) != nullptr) {
		const int32_t oldValue = engineStack_->autoParam->getCurrentValue();
		const bool automatedBefore = engineStack_->autoParam->isAutomated(*engineStack_->doc());
		Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
		if (action != nullptr) {
			action->recordParamChangeIfNotAlreadySnapshotted(engineStack_);
		}
		std::optional<bool> changed = deluge::snapshot::ParamSnapshot::applyPatch(
		    ctx.model, *address_, newValue_, pos_, length_, gatherRegionContext(engineStack_));
		engineStack_->autoParam->projectRegionFromStore(engineStack_, oldValue, automatedBefore,
		                                                changed.value_or(false));
		return Error::NONE;
	}

	// Funnel phase: delegate to the exact engine mutation the call site used, behaviour-identical.
	engineStack_->autoParam->setValuePossiblyForRegion(newValue_, engineStack_, pos_, length_,
	                                                   mayDeleteNodesInLinearRun_);
	return Error::NONE;
}

void SetParamValue::emitEvent(Context& ctx) {
	if (!address_.has_value()) {
		// Engine-only lane (never-saved owner, engine-resolved only) — nothing to patch in the model.
		return;
	}
#if ALPHA_OR_BETA_VERSION
	// M4 event-feed pilot observation: verify the durable ParamAddress still resolves to a live DOM param, proving
	// the feed's addressing end-to-end. The store WRITE is now owned by apply() (scalar + stopped-transport region
	// are model-first via ParamSnapshot::applyPatch; clock-active region + unresolvable lanes keep the in-line poke),
	// so emitEvent no longer patches — the M4a A/B toggle has been retired.
	if (ctx.model.resolveParam(*address_) == nullptr) {
		D_PRINTLN("M4 event feed: SetParamValue address did not resolve to a DOM param");
	}
#else
	(void)ctx;
#endif
}

} // namespace deluge::edits
