#include "model/edits/set_param_value.h"
#include "model/edits/edit_cable.h"

#include "deluge-project/param_automation.h" // DocAutoParam::nodes
#include "deluge-project/song.h"             // Song::resolveParam
#include "gui/ui/ui.h"                       // getRootUI
#include "gui/views/automation_view.h"       // automationView interpolation flags
#include "io/debug/log.h"
#include "model/action/action.h" // recordParamChangeIfNotAlreadySnapshotted (region undo snapshot)
#include "model/action/action_logger.h"
#include "model/clip/clip.h" // Clip::getCurrentlyRecordingLinearly (region context)
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/snapshot/param_snapshot.h" // ParamSnapshot::applyPatch
#include "model/song/song.h"               // engine Song (tc != song comparison)
#include "model/timeline_counter.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"

namespace deluge::edits {

namespace {
// Gather the timeline/playback context the region patch needs, reading it from the borrowed engine stack at exactly
// the sites AutoParam::setValueForRegion / homogenizeRegion read it, so applyPatch's region path matches the engine's
// in-line setter.
// livePos/reversed are consulted by the kernel only when the clock is active (auto_param.cpp routes to
// yesChangeCurrentValue otherwise), so they are gathered lazily and stay safe on a stopped-transport stack.
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
/// Whether the reverb's sidechain copies the param @p stack names: any owner's sidechain shape, the song's or a global
/// effectable's sidechain volume, or a sound's cable from the sidechain to its reverb send.
bool copiedByReverbSidechain(const ModelStackWithAutoParam& stack) {
	namespace params = deluge::modulation::params;
	const params::Kind kind = stack.kind;
	if (kind == params::Kind::PATCH_CABLE) {
		PatchSource source{};
		deluge::synth::CableTarget destination{};
		PatchCableSet::dissectParamId(stack.paramId, &destination, &source);
		return source == PatchSource::SIDECHAIN
		       && destination == deluge::synth::CableTarget::param(params::GLOBAL_VOLUME_POST_REVERB_SEND);
	}
	const bool unpatched = kind == params::Kind::UNPATCHED_SOUND || kind == params::Kind::UNPATCHED_GLOBAL;
	return (unpatched && stack.paramId == params::UNPATCHED_SIDECHAIN_SHAPE)
	       || (kind == params::Kind::UNPATCHED_GLOBAL && stack.paramId == params::UNPATCHED_SIDECHAIN_VOLUME);
}

/// Mark the instrument that owns @p address edited: an INSTRUMENT owner, or the kit holding a DRUM owner.
void markOwnerEdited(::Song& song, const project::ParamAddress& address) {
	using Kind = project::OwnerId::Kind;
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		const bool owns = address.owner.kind == Kind::INSTRUMENT
		                      ? output->id() == address.owner.id
		                      : address.owner.kind == Kind::DRUM && output->type == OutputType::KIT
		                            && static_cast<Kit*>(output)->drumWithId(address.owner.id) != nullptr;
		if (owns) {
			if (output->type != OutputType::AUDIO) {
				static_cast<Instrument*>(output)->beenEdited();
			}
			return;
		}
	}
}
} // namespace

ModelStackWithAutoParam* paramStackFor(::Song& song, project::ParamAddress& address, SetParamValue::Resolve resolve,
                                       void* memory) {
	switch (resolve) {
	case SetParamValue::Resolve::READ:
		return bridge::paramStackAt(song, address, memory);
	case SetParamValue::Resolve::WRITE:
		return bridge::paramStackForWrite(song, address, memory);
	case SetParamValue::Resolve::EXACT:
		return bridge::paramStackExact(song, address, memory);
	}
	return nullptr;
}

Error SetParamValue::apply(Context& ctx) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = paramStackFor(ctx.song, address_, resolve_, memory);
	if (stack == nullptr) {
		return Error::UNSPECIFIED;
	}
	const Error error = write(ctx, stack);
	if (error == Error::NONE && copiedByReverbSidechain(*stack)) {
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	if (error == Error::NONE && resolve_ == Resolve::WRITE) {
		markOwnerEdited(ctx.song, address_);
	}
	return error;
}

Error SetParamValue::write(Context& ctx, ModelStackWithAutoParam* stack) {
	// The Edit owns its undo record: snapshot the before-state into the open Action up front, replicating the guard
	// at the interior log site (auto_param.cpp setCurrentValueInResponseToUserInput, "record value change")
	// evaluated pre-mutation. The interior log then dedupes via Action::containsConsequenceParamChange, so this
	// pre-log is idempotent (other callers still rely on the interior log).
	//
	// The interior log fires iff execution reaches its site AND (nodes.empty() || !isPlaying) AND
	// value != currentValue. It is reached on the SCALAR path only (a nonzero-length region edit goes to
	// setValueForRegion, which manages its own Action). The recording-armed branch never yields an
	// unautomated-value-change Action — it either early-exits (shift-delete / clip-too-short) or falls through with
	// nodes now non-empty — so excluding every (isPlaying && recordingArmed) case matches it exactly.
	// setValuePossiblyForRegion sends (length != 0 && timelineCounterIsSet) to setValueForRegion; everything else is
	// the scalar path that reaches the interior unautomated-change log.
	const bool scalarPath = length_ == 0 || !stack->timelineCounterIsSet();

	// Hoisted out of the pre-log so the model-first decision below can reuse them. recordingActive is the exact
	// condition under which the scalar poke RECORDS automation (setCurrentValueInResponseToUserInput's recording
	// branch) — that case is not model-first (it writes nodes); everything else is the plain scalar tail.
	bool isPlaying = false;
	bool recordingActive = false;
	if (scalarPath) {
		isPlaying = playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn
		            && (!stack->timelineCounterIsSet() || stack->getTimelineCounter()->isPlayingAutomationNow());
		const bool recordingArmed = playbackHandler.recording != RecordingMode::OFF && stack->timelineCounterIsSet()
		                            && stack->getTimelineCounter()->armedForRecording;
		recordingActive = isPlaying && recordingArmed;

		if (newValue_ != stack->autoParam->getCurrentValue() && !recordingActive
		    && (stack->doc()->nodes.empty() || !isPlaying)) {
			actionLogger.recordUnautomatedParamChange(stack);
		}
	}

	// The persistent scalar store write is model-first (ParamSnapshot::applyPatch is the sole writer), and the engine
	// derives its RT state (cursor/interpolation/notify/latch) from the store. Taken only for a resolvable scalar
	// lane that isn't recording automation. Region, recording-active scalar, and not-yet-relocated
	// lanes keep the in-line poke as the writer (it resolves via doc() with the relocate/engine-store fallback, so
	// no lane is dropped).
	if (scalarPath && !recordingActive && ctx.model.resolveParam(address_) != nullptr) {
		const int32_t oldValue = stack->autoParam->getCurrentValue();
		deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, address_, newValue_, 0, 0, std::nullopt);
		stack->autoParam->projectScalarFromStore(stack, oldValue, isPlaying);
		settleCableDepth(stack);
		return Error::NONE;
	}

	// The region store write is model-first for every clock state — applyPatch writes the nodes + current_value (via
	// the DOM kernel), and projectRegionFromStore derives the engine RT tail (cursor + interpolation-reset /
	// UI-notify + notify) from the branch the kernel took (RegionWriteResult). The undo snapshot is taken AHEAD of
	// the patch, with the same getNewAction + record call setValueForRegion makes, so held-pad region drags
	// coalesce identically. Only not-yet-relocated lanes keep the in-line poke below.
	if (length_ != 0 && stack->timelineCounterIsSet() && ctx.model.resolveParam(address_) != nullptr) {
		// Mirror setValueForRegion's pos>=effectiveLength early return (before any Action), so a held-pad edit beyond
		// the loop is a true no-op — no undo step.
		if (pos_ >= stack->getLoopLength()) {
			return Error::NONE;
		}
		const int32_t oldValue = stack->autoParam->getCurrentValue();
		const bool automatedBefore = stack->autoParam->isAutomated(*stack->doc());
		Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
		if (action != nullptr) {
			action->recordParamChangeIfNotAlreadySnapshotted(stack);
		}
		std::optional<deluge::project::RegionWriteResult> result = deluge::snapshot::ParamSnapshot::applyPatch(
		    ctx.model, address_, newValue_, pos_, length_, gatherRegionContext(stack));
		if (result.has_value()) {
			stack->autoParam->projectRegionFromStore(stack, oldValue, automatedBefore, *result);
		}
		settleCableDepth(stack);
		return Error::NONE;
	}

	// A scalar write while recording automation, and a lane not yet in the project tree: the engine's own setter.
	stack->autoParam->setValuePossiblyForRegion(newValue_, stack, pos_, length_, mayDeleteNodesInLinearRun_);
	settleCableDepth(stack);
	return Error::NONE;
}

void SetParamValue::emitEvent(Context& ctx) {
#if ALPHA_OR_BETA_VERSION
	// Verify the durable ParamAddress still resolves to a live DOM param, proving the feed's addressing end-to-end.
	// The store write is owned by apply() (model-first via ParamSnapshot::applyPatch where the lane resolves;
	// lanes not yet in the tree keep the in-line poke), so emitEvent does not patch.
	if (ctx.model.resolveParam(address_) == nullptr) {
		D_PRINTLN("M4 event feed: SetParamValue address did not resolve to a DOM param");
	}
#else
	(void)ctx;
#endif
}

} // namespace deluge::edits
