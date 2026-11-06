/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "scenarios/playhead_moves_scenario.h"

#include "definitions_cxx.hpp"
#include "deluge-project/edit_guard.h"
#include "deluge-project/param_automation.h"
#include "deluge-project/param_edit.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "libdeluge/app.h"
#include "libdeluge/system.h"
#include "libdeluge/types.h"
#include "memory/heaps.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/clip_address_for.h"
#include "model/edits/shift_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/lane_publication.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
#include "playback/move_section.h"
#include "playback/playback_handler.h"
#include "playback/playhead_checks.h"
#include "playback/render_requests.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"
#include "storage/preset_acquisition.h"
#include "storage/storage_manager.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint32_t kRenderBlockFrames = 128;
/// Blocks played before a leg's move, so the clock has ticked.
constexpr int32_t kBlocksBefore = 8;
/// The most blocks waited for the first tick.
constexpr int32_t kMaxBlocksBefore = 400;
/// Blocks rendered after a leg's move; their output is the leg's hash.
constexpr int32_t kBlocksAfter = 16;
/// Blocks rendered stopped before a start: some 0.37 s, longer than a tick at any tempo the song plays.
constexpr int32_t kStoppedBlocks = 128;
/// Where the new-clip gesture puts the leg's clips.
constexpr int32_t kAppendY = 1000;
constexpr int32_t kRowNote = 60;
constexpr int32_t kVelocity = 100;
/// SPP data bytes: song position 16 sixteenths (data2 << 7 | data1), times 6 ticks.
constexpr uint8_t kSppData1 = 16;
constexpr uint8_t kSppData2 = 0;

enum class Leg : uint8_t {
	Start,
	Restart,
	Continue,
	Spp,
	ToArranger,
	ToSession,
	Undo,
	ClipLength,
	ClipShift,
	RowMute,
	RowLength,
	RowDirection,
	LateStart,
	Unsolo,
	InjectStart,
	InjectAfterMove,
	InjectLaunch,
	ArrangerRecord,
	Clone,
	Resume,
	SelfRepair,
	ClipStop,
	Done,
};

enum class Stage : uint8_t { Setup, Before, Move, After, Summary };

Stage g_stage = Stage::Setup;
Leg g_leg = Leg::Start;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_blocks = 0;
uint32_t g_timerAtStart = 0;

Clip* g_home = nullptr;
/// The leg's playing synth clip: volume automation and a row of notes.
InstrumentClip* g_clip = nullptr;
/// A second synth clip of the same length, stopped: the one a launch starts.
InstrumentClip* g_second = nullptr;
NoteRow* g_row = nullptr;
int32_t g_rowIndex = -1;

uint32_t g_strayBefore = 0;
uint32_t g_allocBefore = 0;
uint32_t g_actionsBefore = 0;
uint64_t g_hash = 0;
/// Arrangement-only clips before the move: arrangement recording makes one per armed output.
int32_t g_arrangementClipsBefore = 0;
/// Render writes to lanes outside record mode before the move.
uint32_t g_laneWritesBefore = 0;
/// Collections found publishing lanes that differ from the model, after every leg.
int32_t g_lanesStale = 0;
/// Blocks a leg plays long enough to reach a node event or a launch: some 2.3 s.
constexpr int32_t kBlocksToEvent = 800;
/// A mono-expression base set before a clip is stopped.
constexpr int32_t kExpressionBase = 1 << 24;
/// What the injected block saw: the timer and swung ticks it actioned, or -1 if it never ran.
int64_t g_injectedTicks = -1;

const char* legName(Leg leg) {
	switch (leg) {
	case Leg::Start:
		return "START";
	case Leg::Restart:
		return "RESTART";
	case Leg::Continue:
		return "CONTINUE";
	case Leg::Spp:
		return "SPP";
	case Leg::ToArranger:
		return "TOARRANGER";
	case Leg::ToSession:
		return "TOSESSION";
	case Leg::Undo:
		return "UNDO";
	case Leg::ClipLength:
		return "CLIPLENGTH";
	case Leg::ClipShift:
		return "CLIPSHIFT";
	case Leg::RowMute:
		return "ROWMUTE";
	case Leg::RowLength:
		return "ROWLENGTH";
	case Leg::RowDirection:
		return "ROWDIRECTION";
	case Leg::LateStart:
		return "LATESTART";
	case Leg::Unsolo:
		return "UNSOLO";
	case Leg::InjectStart:
		return "INJECTSTART";
	case Leg::InjectAfterMove:
		return "INJECTAFTERMOVE";
	case Leg::InjectLaunch:
		return "INJECTLAUNCH";
	case Leg::ArrangerRecord:
		return "ARRANGERRECORD";
	case Leg::Clone:
		return "CLONE";
	case Leg::Resume:
		return "RESUME";
	case Leg::SelfRepair:
		return "SELFREPAIR";
	case Leg::ClipStop:
		return "CLIPSTOP";
	case Leg::Done:
		return "DONE";
	}
	return "?";
}

bool fail(const char* what) {
	char line[160];
	snprintf(line, sizeof(line), "PM-SETUP FAIL leg=%s %s\n", legName(g_leg), what);
	std::fputs(line, stdout);
	g_failures++;
	return false;
}

/// Whether the leg's move is itself the start of playback, so nothing plays before it.
bool moveStartsPlayback(Leg leg) {
	return leg == Leg::Start || leg == Leg::Continue || leg == Leg::InjectStart || leg == Leg::InjectAfterMove;
}

/// Whether the leg plays on the external clock, so the scenario feeds it input ticks.
bool externalClock(Leg leg) {
	return leg == Leg::Spp || leg == Leg::Continue;
}

/// One render block; on the external clock, one input tick before it so the clock keeps running.
void renderBlock(bool hashing) {
	static DelugeStereoSample silence[kRenderBlockFrames]{};
	static DelugeStereoSample output[kRenderBlockFrames]{};
	if (playbackHandler.isExternalClockActive()) {
		playbackHandler.inputTick();
	}
	deluge_app_render(silence, output, kRenderBlockFrames);
	if (hashing) {
		// FNV-1a over the block's samples.
		for (const DelugeStereoSample& frame : output) {
			for (int32_t sample : {frame.l, frame.r}) {
				for (int32_t byte = 0; byte < 4; byte++) {
					g_hash ^= static_cast<uint8_t>(static_cast<uint32_t>(sample) >> (byte * 8));
					g_hash *= 0x100000001b3ULL;
				}
			}
		}
	}
}

/// The injected block: one render block, counting the swung ticks it actions.
void injectedBlock() {
	const int64_t swungBefore = playbackHandler.lastSwungTickActioned;
	const uint64_t timerBefore = playbackHandler.timeLastTimerTickBig;
	renderBlock(false);
	g_injectedTicks = (playbackHandler.lastSwungTickActioned - swungBefore)
	                  + (playbackHandler.timeLastTimerTickBig != timerBefore ? 1 : 0);
}

InstrumentClip* makeSynthClip() {
	ParamManagerForTimeline paramManager;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (instrument == nullptr) {
		fail("no synth");
		return nullptr;
	}
	auto* synth = static_cast<SoundInstrument*>(instrument);
	synth->setupAsBlankSynth(&paramManager, false);
	currentSong->live.param_backup.backUp(*synth, nullptr, &paramManager, true);
	deluge::storage::AcquireResult acquired;
	acquired.instrument = synth;
	acquired.loadedFromFile = true;
	Clip* clip = sessionView.createNewInstrumentClipAcquired(OutputType::SYNTH, kAppendY, &acquired);
	if (clip == nullptr) {
		fail("the new-clip gesture made no clip");
		return nullptr;
	}
	clip->activeIfNoSolo = true;
	return static_cast<InstrumentClip*>(clip);
}

/// Four nodes in @p clip's synth volume automation, so every move re-reads a lane.
void automateVolume(InstrumentClip* clip) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	auto* synth = static_cast<SoundInstrument*>(clip->output);
	ParamCollectionSummary* summary = clip->paramManager.getPatchedParamSetSummary();
	ModelStackWithThreeMainThings* three =
	    setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, synth, clip, &clip->paramManager);
	ModelStackWithParamId* withId =
	    three->addParamCollectionAndId(summary->paramCollection, summary, deluge::modulation::params::LOCAL_VOLUME);
	ModelStackWithAutoParam* volume = withId->paramCollection->getAutoParamFromId(withId, false);
	const int32_t loop = clip->loopLength();
	deluge::project::DocAutoParam* doc = volume->doc();
	for (int32_t eighth = 1; eighth < 8; eighth += 2) {
		(void)deluge::project::setNodeAtPos(*doc, loop * eighth / 8, (eighth - 4) << 27, eighth == 3);
	}
	volume->paramCollection->notifyParamModifiedInSomeWay(volume, volume->autoParam->getCurrentValue(), true, false,
	                                                      true);
}

/// A row of four notes in @p clip.
NoteRow* addNotes(InstrumentClip* clip, int32_t* index) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* ms = setupModelStackWithTimelineCounter(memory, currentSong, clip);
	ModelStackWithNoteRow* rowStack = clip->getOrCreateNoteRowForYNote(kRowNote, ms);
	NoteRow* row = rowStack->getNoteRowAllowNull();
	if (row == nullptr) {
		return nullptr;
	}
	*index = rowStack->noteRowId;
	for (int32_t n = 0; n < 4; n++) {
		(void)row->attemptNoteAdd(n * (clip->loopLength() / 4), clip->loopLength() / 8, kVelocity,
		                          row->getDefaultProbability(), row->getDefaultIterance(),
		                          row->getDefaultFill(rowStack), rowStack, nullptr);
	}
	return row;
}

/// What the effects self-test's runner saw.
struct RunnerLog {
	int32_t runs = 0;
	int32_t order[8]{};
	bool masked = false;
};
RunnerLog g_runnerLog;

void logRunner(const deluge::playback::MoveEffects::DelayNotify& notify) {
	if (g_runnerLog.runs < 8) {
		g_runnerLog.order[g_runnerLog.runs] = notify.oldValue;
	}
	g_runnerLog.runs++;
	g_runnerLog.masked = g_runnerLog.masked || deluge_audio_excluded_depth() > 0;
}

/// Whether the retired marker has been freed.
bool g_markerFreed = false;

void freeMarker(void* marker) {
	g_markerFreed = true;
	deluge::memory::dealloc(marker);
}

/// Memory retired while the render may read it is freed only once two render blocks have begun since.
bool selfTestRetire() {
	static DelugeStereoSample silence[kRenderBlockFrames]{};
	static DelugeStereoSample output[kRenderBlockFrames]{};
	// The render has to have run for retirement to wait on it
	deluge_app_render(silence, output, kRenderBlockFrames);
	g_markerFreed = false;
	AudioEngine::retireMemory(deluge::memory::alloc_sdram(16), &freeMarker);
	deluge_app_render(silence, output, kRenderBlockFrames);
	AudioEngine::reclaimRetired();
	const bool keptOneBlock = !g_markerFreed;
	deluge_app_render(silence, output, kRenderBlockFrames);
	deluge_app_render(silence, output, kRenderBlockFrames);
	AudioEngine::reclaimRetired();
	return keptOneBlock && g_markerFreed;
}

/// As logRunner, and the notify 10 makes a move of its own that defers 20 and 21.
void nestingRunner(const deluge::playback::MoveEffects::DelayNotify& notify) {
	logRunner(notify);
	if (notify.oldValue == 10) {
		deluge::playback::MoveSection section{"selftest nested"};
		deluge::playback::MoveEffects::DelayNotify inner{};
		for (int32_t value : {20, 21}) {
			inner.oldValue = value;
			(void)deluge::playback::MoveEffects::defer(inner);
		}
	}
}

/// MoveEffects and MoveSection on their own: order, room, nesting, the render, and the run after the mask lifts.
void selfTestEffects() {
	using deluge::playback::MoveEffects;
	using deluge::playback::MoveSection;
	MoveEffects::setRunnerForTest(&logRunner);
	MoveEffects::DelayNotify notify{};

	// Recorded in order, run when the outermost scope ends, not before.
	g_runnerLog = {};
	bool nested = true;
	{
		MoveEffects::Scope outer;
		notify.oldValue = 1;
		(void)MoveEffects::defer(notify);
		{
			MoveEffects::Scope inner;
			notify.oldValue = 2;
			(void)MoveEffects::defer(notify);
		}
		nested = g_runnerLog.runs == 0 && MoveEffects::collecting();
	}
	const bool order = g_runnerLog.runs == 2 && g_runnerLog.order[0] == 1 && g_runnerLog.order[1] == 2;

	// The room: deferred until full; past it, the caller acts at once.
	g_runnerLog = {};
	int32_t room = 0;
	{
		MoveEffects::Scope scope;
		while (room < 1000 && MoveEffects::defer(notify)) {
			room++;
		}
	}
	const bool roomRan = g_runnerLog.runs == room;

	// A section: collecting, the render held off, no allocation; its effects run after the render runs again.
	g_runnerLog = {};
	bool inside = false;
	{
		MoveSection section{"selftest"};
		inside =
		    MoveEffects::collecting() && deluge_audio_excluded_depth() > 0 && deluge::memory::inNoAllocationScope();
		(void)MoveEffects::defer(notify);
	}
	const bool unmasked = inside && g_runnerLog.runs == 1 && !g_runnerLog.masked && deluge_audio_excluded_depth() == 0
	                      && !deluge::memory::inNoAllocationScope();

	// A notify run when a section ends may itself make a move: that move runs its own effects, and the rest of the
	// first section's still run, once each, in order.
	g_runnerLog = {};
	MoveEffects::setRunnerForTest(&nestingRunner);
	{
		MoveSection section{"selftest drain"};
		for (int32_t value : {10, 11, 12}) {
			notify.oldValue = value;
			(void)MoveEffects::defer(notify);
		}
	}
	const bool drain = g_runnerLog.runs == 5 && g_runnerLog.order[0] == 10 && g_runnerLog.order[1] == 20
	                   && g_runnerLog.order[2] == 21 && g_runnerLog.order[3] == 11 && g_runnerLog.order[4] == 12;
	MoveEffects::setRunnerForTest(&logRunner);

	// On the render a scope collects nothing.
	bool render = false;
	{
		deluge::memory::RenderBlock block;
		MoveEffects::Scope scope;
		render = !MoveEffects::collecting();
	}
	MoveEffects::setRunnerForTest(nullptr);

	char line[128];
	const bool retire = selfTestRetire();
	snprintf(line, sizeof(line),
	         "PM-EFFECTS order=%d nested=%d room=%d room_ran=%d unmasked=%d drain=%d render=%d retire=%d\n",
	         order ? 1 : 0, nested ? 1 : 0, static_cast<int>(room), roomRan ? 1 : 0, unmasked ? 1 : 0, drain ? 1 : 0,
	         render ? 1 : 0, retire ? 1 : 0);
	std::fputs(line, stdout);
}

/// g_clip's synth volume lane, as automateVolume() reaches it.
ModelStackWithAutoParam* volumeLane(char* memory) {
	auto* synth = static_cast<SoundInstrument*>(g_clip->output);
	ParamCollectionSummary* summary = g_clip->paramManager.getPatchedParamSetSummary();
	ModelStackWithThreeMainThings* three =
	    setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, synth, g_clip, &g_clip->paramManager);
	ModelStackWithParamId* withId =
	    three->addParamCollectionAndId(summary->paramCollection, summary, deluge::modulation::params::LOCAL_VOLUME);
	return withId->paramCollection->getAutoParamFromId(withId, false);
}

/// g_clip's first mono-expression lane.
ModelStackWithAutoParam* expressionLane(char* memory) {
	auto* synth = static_cast<SoundInstrument*>(g_clip->output);
	(void)g_clip->paramManager.getOrCreateExpressionParamSet();
	ParamCollectionSummary* summary = g_clip->paramManager.getExpressionParamSetSummary();
	ModelStackWithThreeMainThings* three =
	    setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, synth, g_clip, &g_clip->paramManager);
	ModelStackWithParamId* withId = three->addParamCollectionAndId(summary->paramCollection, summary, 0);
	return withId->paramCollection->getAutoParamFromId(withId, true);
}

bool setupLeg() {
	g_clip = makeSynthClip();
	if (g_clip == nullptr) {
		return false;
	}
	automateVolume(g_clip);
	g_row = addNotes(g_clip, &g_rowIndex);
	if (g_row == nullptr) {
		return fail("no note row");
	}
	g_second = nullptr;
	if (g_leg == Leg::LateStart || g_leg == Leg::InjectLaunch || g_leg == Leg::Unsolo) {
		g_second = makeSynthClip();
		if (g_second == nullptr) {
			return false;
		}
		automateVolume(g_second);
		int32_t index = -1;
		if (addNotes(g_second, &index) == nullptr) {
			return fail("no note row on the second clip");
		}
		// A launch starts it; an unsolo restarts it.
		g_second->activeIfNoSolo = (g_leg == Leg::Unsolo);
	}
	return true;
}

/// Start the leg's clock, as the play button or a MIDI start does.
void startClock() {
	if (externalClock(g_leg)) {
		playbackHandler.setupPlaybackUsingExternalClock();
	}
	else {
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
	}
}

ModelStackWithNoteRow* rowStack(char* memory) {
	ModelStackWithTimelineCounter* ms = setupModelStackWithTimelineCounter(memory, currentSong, g_clip);
	return ms->addNoteRow(g_rowIndex, g_row);
}

/// What a leg does once its clock runs, before the counted move.
void prepareMove() {
	switch (g_leg) {
	case Leg::ToSession:
		playbackHandler.arrangementPosToStartAtOnSwitch = 0;
		playbackHandler.switchToArrangement();
		break;
	case Leg::Unsolo: {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		session.soloClipRightNow(setupModelStackWithTimelineCounter(memory, currentSong, g_clip));
		break;
	}
	case Leg::Continue:
		playbackHandler.endPlayback();
		playbackHandler.posToNextContinuePlaybackFrom = g_clip->loopLength() / 2;
		break;
	case Leg::Clone:
	case Leg::Resume: {
		// Recording session to arranger: the playing clip has an instance from here on, which a recorded change clones
		// it into.
		playbackHandler.recording = RecordingMode::ARRANGEMENT;
		g_clip->beginInstance(currentSong, playbackHandler.getActualArrangementRecordPos());
		if (g_leg == Leg::Resume) {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			(void)g_clip->possiblyCloneForArrangementRecording(
			    setupModelStackWithTimelineCounter(memory, currentSong, g_clip));
		}
		break;
	}
	case Leg::SelfRepair: {
		// A node at the lane's loop length, which no edit leaves: the render's self-repair is what removes it.
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* volume = volumeLane(memory);
		deluge::project::EditGuard guard{deluge::project::ModelEdit::NODE_KERNEL};
		(void)deluge::project::setNodeAtPos(*volume->doc(), g_clip->loopLength(), 1 << 27, false);
		break;
	}
	case Leg::ClipStop: {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* expression = expressionLane(memory);
		expression->autoParam->setCurrentValueWithNoReversionOrRecording(expression, kExpressionBase);
		break;
	}
	case Leg::ArrangerRecord:
		// Recording session to arranger with the clip's output armed: the switch begins arrangement recording there.
		playbackHandler.recording = RecordingMode::ARRANGEMENT;
		g_clip->output->armedForRecording = true;
		g_arrangementClipsBefore = static_cast<int32_t>(currentSong->live.clips.arrangement_only_clips.size());
		break;
	case Leg::Start:
	case Leg::InjectStart:
	case Leg::InjectAfterMove:
		playbackHandler.endPlayback();
		// Time passes between a stop and the next start, as it does on the device: the last playback's next-tick time
		// is in the past when the next one starts.
		for (int32_t block = 0; block < kStoppedBlocks; block++) {
			renderBlock(false);
		}
		break;
	default:
		break;
	}
}

/// Launch g_second late, as a pad pressed just after the launch point does: the gesture reads where the playing clip
/// has reached and starts g_second there at once.
void lateStart() {
	session.userWantsToArmClipsToStartOrSolo(0, g_second, false, true);
}

void move() {
	switch (g_leg) {
	case Leg::Start:
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
		break;
	case Leg::Restart:
		playbackHandler.forceResetPlayPos(currentSong);
		break;
	case Leg::Continue:
		playbackHandler.setupPlaybackUsingExternalClock(false, true);
		break;
	case Leg::Spp:
		playbackHandler.positionPointerReceived(kSppData1, kSppData2);
		break;
	case Leg::ToArranger:
	case Leg::ArrangerRecord:
		playbackHandler.arrangementPosToStartAtOnSwitch = 0;
		playbackHandler.switchToArrangement();
		break;
	case Leg::ToSession:
		playbackHandler.switchToSession();
		break;
	case Leg::Undo: {
		Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::NOT_ALLOWED);
		if (action == nullptr) {
			fail("no action for the undo");
			break;
		}
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		g_row->deleteNoteByPos(rowStack(memory), 0, action);
		actionLogger.closeAction(ActionType::NOTE_EDIT);
		(void)actionLogger.undo();
		break;
	}
	case Leg::ClipLength:
		currentSong->setClipLength(g_clip, g_clip->loopLength() * 2, nullptr);
		break;
	case Leg::ClipShift: {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		deluge::edits::Context context{*currentSong, currentSong->project};
		deluge::edits::ShiftClip edit{deluge::edits::clipAddressFor(*g_clip), 3, true, true,
		                              setupModelStackWithTimelineCounter(memory, currentSong, g_clip)};
		(void)deluge::edits::apply(context, edit);
		break;
	}
	case Leg::RowMute: {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		g_row->toggleMute(rowStack(memory), true);
		g_row->toggleMute(rowStack(memory), true);
		break;
	}
	case Leg::RowLength: {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithNoteRow* stack = rowStack(memory);
		g_row->setLength(stack, g_clip->loopLength() / 2, nullptr, stack->getLastProcessedPos(), false);
		break;
	}
	case Leg::RowDirection: {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		g_row->setSequenceDirectionMode(rowStack(memory), SequenceDirection::REVERSE);
		break;
	}
	case Leg::LateStart:
		lateStart();
		break;
	case Leg::Unsolo:
		session.unsoloClip(g_clip);
		break;
	case Leg::InjectStart:
		deluge::playback::setRenderInjection(deluge::playback::InjectAt::StartBeforeClock, &injectedBlock);
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
		break;
	case Leg::InjectAfterMove:
		deluge::playback::setRenderInjection(deluge::playback::InjectAt::AfterMove, &injectedBlock);
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
		break;
	case Leg::InjectLaunch:
		deluge::playback::setRenderInjection(deluge::playback::InjectAt::LaunchBeforeMove, &injectedBlock);
		lateStart();
		break;
	case Leg::Clone: {
		// As a knob turned on the clip while recording to the arranger does
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		(void)g_clip->possiblyCloneForArrangementRecording(
		    setupModelStackWithTimelineCounter(memory, currentSong, g_clip));
		break;
	}
	case Leg::Resume:
		// As stopping recording to the arranger does
		currentSong->resumeClipsClonedForArrangementRecording();
		break;
	case Leg::SelfRepair:
		// The render reaches the lane's node events
		for (int32_t block = 0; block < kBlocksToEvent; block++) {
			renderBlock(false);
		}
		// As the app loop does: the render's requests before a job, the lanes they changed published at its end
		deluge::playback::applyRenderRequests();
		deluge::modulation::publishDirtyLanes(true);
		break;
	case Leg::ClipStop: {
		// Armed to stop: the render stops it at the next launch
		int32_t index = static_cast<int32_t>(currentSong->live.clips.session_clips.getIndexForClip(g_clip));
		session.toggleClipStatus(g_clip, &index, false, 0);
		for (int32_t block = 0; block < kBlocksToEvent; block++) {
			renderBlock(false);
		}
		// As the app loop does: the render's requests before a job, the lanes they changed published at its end
		deluge::playback::applyRenderRequests();
		deluge::modulation::publishDirtyLanes(true);
		break;
	}
	case Leg::Done:
		break;
	}
	for (int32_t at = 0; at < static_cast<int32_t>(deluge::playback::InjectAt::kCount); at++) {
		deluge::playback::setRenderInjection(static_cast<deluge::playback::InjectAt>(at), nullptr);
	}
}

void printLine() {
#if ALPHA_OR_BETA_VERSION
	const uint32_t stray = deluge::playback::strayPositionWrites - g_strayBefore;
	const uint32_t actions = deluge::playback::maskedActions - g_actionsBefore;
#else
	const uint32_t stray = 0;
	const uint32_t actions = 0;
#endif
	const uint32_t allocs = deluge::memory::maskedAllocations() - g_allocBefore;
	char line[256];
	int used = snprintf(line, sizeof(line), "PM-%s stray=%u masked_alloc=%u masked_actions=%u hash=%016llx",
	                    legName(g_leg), static_cast<unsigned>(stray), static_cast<unsigned>(allocs),
	                    static_cast<unsigned>(actions), static_cast<unsigned long long>(g_hash));
	if (g_leg == Leg::InjectStart || g_leg == Leg::InjectAfterMove) {
		// The render must not tick while the clips are half positioned: none of this block's ticks.
		used += snprintf(line + used, sizeof(line) - used, " injected=%d injected_ticks=%lld",
		                 g_injectedTicks >= 0 ? 1 : 0, static_cast<long long>(g_injectedTicks));
	}
	if (g_leg == Leg::LateStart || g_leg == Leg::InjectLaunch || g_leg == Leg::Unsolo) {
		// Launched late at the playing clip's position, or restarted by its unsolo, the second clip plays in phase
		// with it.
		const int32_t phase = static_cast<int32_t>(g_second->getLivePos() % g_second->loopLength())
		                      - static_cast<int32_t>(g_clip->getLivePos() % g_clip->loopLength());
		used += snprintf(line + used, sizeof(line) - used, " active=%d phase=%d",
		                 currentSong->isClipActive(g_second) ? 1 : 0, phase);
		if (g_leg == Leg::InjectLaunch) {
			used += snprintf(line + used, sizeof(line) - used, " injected=%d", g_injectedTicks >= 0 ? 1 : 0);
		}
	}
	if (g_leg == Leg::Clone || g_leg == Leg::Resume) {
		// The clone plays in the clip's place while recording; resumed, the clip plays again.
		Clip* active = g_clip->output->getActiveClip();
		const bool cloned = active != nullptr && active != g_clip && active->beingRecordedFromClip == g_clip;
		used += snprintf(line + used, sizeof(line) - used, g_leg == Leg::Clone ? " cloned=%d" : " resumed=%d",
		                 (g_leg == Leg::Clone ? cloned : active == g_clip) ? 1 : 0);
	}
	if (g_leg == Leg::SelfRepair || g_leg == Leg::ClipStop) {
#if ALPHA_OR_BETA_VERSION
		const auto writes = static_cast<unsigned>(deluge::modulation::renderPublishedWrites - g_laneWritesBefore);
#else
		const unsigned writes = 0;
#endif
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		if (g_leg == Leg::SelfRepair) {
			const deluge::project::DocAutoParam& lane = *volumeLane(memory)->doc();
			const bool erased = lane.nodes.empty() || lane.nodes[std::ssize(lane.nodes) - 1].pos < g_clip->loopLength();
			used += snprintf(line + used, sizeof(line) - used, " render_writes=%u erased=%d", writes, erased ? 1 : 0);
		}
		else {
			ModelStackWithAutoParam* expression = expressionLane(memory);
			used += snprintf(line + used, sizeof(line) - used, " render_writes=%u stopped=%d played=%d base=%d", writes,
			                 currentSong->isClipActive(g_clip) ? 0 : 1, expression->autoParam->played() == 0 ? 0 : 1,
			                 expression->doc()->current_value == 0 ? 0 : 1);
		}
	}
	if (g_leg == Leg::ArrangerRecord) {
		const auto clips = static_cast<int32_t>(currentSong->live.clips.arrangement_only_clips.size());
		used += snprintf(line + used, sizeof(line) - used, " began=%d", clips > g_arrangementClipsBefore ? 1 : 0);
	}
	std::fputs(line, stdout);
	std::fputs("\n", stdout);
	fflush(stdout);
}

void cleanUpLeg() {
	playbackHandler.recording = RecordingMode::OFF;
	if (playbackHandler.playbackState) {
		playbackHandler.endPlayback();
	}
	currentPlaybackMode = &session;
	changeRootUI(&sessionView);
	currentSong->setCurrentClip(g_home);
	view.setActiveModControllableTimelineCounter(g_home);
	// Stopped clips leave the next leg's song as it was: only the home clip plays.
	if (g_clip != nullptr) {
		g_clip->output->armedForRecording = false;
	}
	for (Clip* clip : {static_cast<Clip*>(g_clip), static_cast<Clip*>(g_second)}) {
		if (clip != nullptr) {
			clip->activeIfNoSolo = false;
			clip->soloingInSessionMode = false;
		}
	}
	currentSong->reassessWhetherAnyClipsSoloing();
	g_clip = nullptr;
	g_second = nullptr;
	g_row = nullptr;
}

void runTick() {
	switch (g_stage) {
	case Stage::Setup:
		if (g_leg == Leg::Done) {
			g_stage = Stage::Summary;
			break;
		}
		if (!setupLeg()) {
			g_stage = Stage::Summary;
			break;
		}
		// The setup's lanes, published as the app loop does when a job ends: these steps run outside it.
		deluge::modulation::publishDirtyLanes(true);
		startClock();
		g_timerAtStart = AudioEngine::audioSampleTimer;
		g_blocks = 0;
		g_stage = Stage::Before;
		break;
	case Stage::Before: {
		renderBlock(false);
		const bool ticked = externalClock(g_leg)
		                        ? playbackHandler.lastSwungTickActioned > 0
		                        : static_cast<int32_t>(static_cast<uint32_t>(playbackHandler.timeLastTimerTickBig >> 32)
		                                               - g_timerAtStart)
		                              >= 0;
		if (++g_blocks < kBlocksBefore || (!ticked && g_blocks < kMaxBlocksBefore)) {
			break;
		}
		if (!ticked) {
			fail("the render actioned no ticks: the clock never ran");
			g_stage = Stage::Summary;
			break;
		}
		prepareMove();
		g_stage = Stage::Move;
		break;
	}
	case Stage::Move:
#if ALPHA_OR_BETA_VERSION
		g_strayBefore = deluge::playback::strayPositionWrites;
		g_actionsBefore = deluge::playback::maskedActions;
#endif
		g_allocBefore = deluge::memory::maskedAllocations();
#if ALPHA_OR_BETA_VERSION
		g_laneWritesBefore = deluge::modulation::renderPublishedWrites;
#endif
		g_hash = 0xcbf29ce484222325ULL;
		g_injectedTicks = -1;
		move();
		for (int32_t block = 0; block < kBlocksAfter; block++) {
			renderBlock(true);
		}
		printLine();
		g_lanesStale += deluge::modulation::checkPublished(*currentSong);
		g_stage = Stage::After;
		break;
	case Stage::After:
		cleanUpLeg();
		g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
		g_stage = Stage::Setup;
		break;
	case Stage::Summary: {
		// Host wall time per site: not the device's, so it is reported, not judged.
		deluge::playback::MaskedSpan::forEachSite([](const deluge::playback::MaskedSpan::Site& site) {
			char spanLine[128];
			snprintf(spanLine, sizeof(spanLine), "PM-SPAN site=%s longest_us=%u count=%u\n", site.name,
			         static_cast<unsigned>(site.longestUs), static_cast<unsigned>(site.count));
			std::fputs(spanLine, stdout);
		});
#if ALPHA_OR_BETA_VERSION
		{
			// Lanes the render resolved through the model, over every leg played outside record mode
			char snapshotLine[64];
			snprintf(snapshotLine, sizeof(snapshotLine), "PM-SNAPSHOT model_reads=%u stale=%d\n",
			         static_cast<unsigned>(deluge::modulation::renderModelReads), static_cast<int>(g_lanesStale));
			std::fputs(snapshotLine, stdout);
		}
#endif
		char line[48];
		snprintf(line, sizeof(line), "PM-RESULT failures=%d\n", static_cast<int>(g_failures));
		std::fputs(line, stdout);
		fflush(stdout);
		g_summaryPrinted = true;
		break;
	}
	}
}

} // namespace

extern "C" {

void deluge_scenario_playhead_moves_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Setup;
	g_leg = Leg::Start;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	selfTestEffects();
	changeRootUI(&sessionView);
	g_home = currentSong->live.clips.session_clips.size() > 0 ? currentSong->live.clips.session_clips[0] : nullptr;
	if (g_home == nullptr) {
		fail("the boot song has no clip");
		g_stage = Stage::Summary;
	}
}

bool deluge_scenario_playhead_moves_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_playhead_moves_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	g_stepDone = true;
}

bool deluge_scenario_playhead_moves_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_playhead_moves_failures(void) {
	return g_failures;
}

} // extern "C"
