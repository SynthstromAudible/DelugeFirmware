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
#include "deluge-project/param_automation.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "libdeluge/app.h"
#include "libdeluge/types.h"
#include "memory/heaps.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
#include "playback/move_section.h"
#include "playback/playback_handler.h"
#include "playback/playhead_checks.h"
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

/// Launch g_second late at the playing clip's position, as a pad pressed just after the launch point does.
void lateStart() {
	const uint32_t loop = static_cast<uint32_t>(g_clip->loopLength());
	session.armClipsToStartOrSoloWithQuantization(g_clip->getLivePos() % loop, loop, 0, false, g_second, true, true, 1,
	                                              ArmState::ON_NORMAL);
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
		(void)g_clip->shiftHorizontally(setupModelStackWithTimelineCounter(memory, currentSong, g_clip), 3, true, true);
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
		g_hash = 0xcbf29ce484222325ULL;
		g_injectedTicks = -1;
		move();
		for (int32_t block = 0; block < kBlocksAfter; block++) {
			renderBlock(true);
		}
		printLine();
		g_stage = Stage::After;
		break;
	case Stage::After:
		cleanUpLeg();
		g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
		g_stage = Stage::Setup;
		break;
	case Stage::Summary: {
		char line[48];
		snprintf(line, sizeof(line), "PM-RESULT failures=%d\n", static_cast<int>(g_failures));
		std::fputs(line, stdout);
	}
		fflush(stdout);
		g_summaryPrinted = true;
		break;
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
