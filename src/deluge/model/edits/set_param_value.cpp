#include "model/edits/set_param_value.h"

#include "deluge-project/param_automation.h" // DocAutoParam::nodes
#include "deluge-project/song.h"             // Song::resolveParam
#include "io/debug/log.h"
#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/timeline_counter.h"
#include "modulation/automation/auto_param.h"
#include "playback/playback_handler.h"

namespace deluge::edits {

Error SetParamValue::apply(Context& /*ctx*/) {
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
	if (scalarPath && newValue_ != engineStack_->autoParam->getCurrentValue()) {
		const bool isPlaying =
		    playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn
		    && (!engineStack_->timelineCounterIsSet() || engineStack_->getTimelineCounter()->isPlayingAutomationNow());
		const bool recordingArmed = playbackHandler.recording != RecordingMode::OFF
		                            && engineStack_->timelineCounterIsSet()
		                            && engineStack_->getTimelineCounter()->armedForRecording;
		if (!(isPlaying && recordingArmed) && (engineStack_->doc()->nodes.empty() || !isPlaying)) {
			actionLogger.recordUnautomatedParamChange(engineStack_);
		}
	}

	// Funnel phase: delegate to the exact engine mutation the call site used, behaviour-identical.
	engineStack_->autoParam->setValuePossiblyForRegion(newValue_, engineStack_, pos_, length_,
	                                                   mayDeleteNodesInLinearRun_);
	return Error::NONE;
}

void SetParamValue::emitEvent(Context& ctx) {
#if ALPHA_OR_BETA_VERSION
	// M4 event-feed pilot (additive/observational — the first real consumer of the emission hook). The
	// model-changed event keys on the durable ParamAddress; verify it resolves to a live DOM param, proving the
	// feed's addressing end-to-end. The engine is still poked in-line by apply(); this only observes. A nullopt
	// address is a legitimately engine-only lane (never-saved owner, engine-resolved only) and is skipped.
	if (address_.has_value() && ctx.model.resolveParam(*address_) == nullptr) {
		D_PRINTLN("M4 event feed: SetParamValue address did not resolve to a DOM param");
	}
#endif
}

} // namespace deluge::edits
