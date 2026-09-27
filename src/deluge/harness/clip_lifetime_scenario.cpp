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

#include "harness/clip_lifetime_scenario.h"

#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "libdeluge/app.h"
#include "libdeluge/types.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_instrument.h"
#include "storage/preset_acquisition.h"
#include "storage/storage_manager.h"
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <unistd.h>

// The firmware headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded `printf_`. This
// harness links the Rust BSP, which has no `putchar_` provider, and every line this file writes IS the gate's output.
#ifdef printf
#undef printf
#endif

namespace {

/// The note each leg holds on its clip's synth. A blank synth sustains it until a note-off, which no leg sends.
constexpr int32_t kNote = 60;
/// The render block the scenario drives. Nothing else renders here: the golden harness skips the audio task's own
/// render once boot has finished, and the internal clock's ticks are actioned inside the render.
constexpr uint32_t kRenderBlockFrames = 128;
/// Blocks rendered with the clip playing before its deletion (at least), and after it: one per step, so the scheduler's
/// tasks (the retirement's reclaim among them) run between them.
constexpr int32_t kBlocksBefore = 8;
constexpr int32_t kBlocksAfter = 8;
/// Timer ticks the render must have actioned before the deletion, and the most blocks rendered waiting for them.
constexpr int32_t kTicksBefore = 4;
constexpr int32_t kMaxBlocksBefore = 400;
/// UNDODELETE's blocks between its undo, its redo and its log discard, rendered inside one step.
constexpr int32_t kUndoBlocks = 4;
/// A y beyond the grid: the new-clip gesture then appends the clip after every other session clip.
constexpr int32_t kAppendY = 1000;

/// Each leg is one real clip deletion path, run on a playing clip. The risky legs -- a dangling pointer the render
/// reads on every tick -- run last.
enum class Leg : uint8_t { SessionDelete, ArrangerDelete, SwapType, UndoDelete, RecordSource, SyncScale, Done };

enum class Stage : uint8_t { Setup, Before, Delete, After, Undo, Report, Summary, Done };

Stage g_stage = Stage::Setup;
Leg g_leg = Leg::SessionDelete;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_ticks = 0;

/// The clip the leg deletes. Once the deletion has run it may be freed: it is only compared, never dereferenced.
Clip* g_victim = nullptr;
/// RECORDSOURCE: the clip that records from the victim. Survives the leg.
Clip* g_recorder = nullptr;
/// The clip the scenario points the song and the view back at between legs: the boot song's first clip, never deleted.
Clip* g_home = nullptr;
/// The internal clock's timer tick when the leg's playback began, to show the render actioned ticks.
int64_t g_tickAtStart = 0;
/// SYNCSCALE: the song's tick magnitudes before the leg made its clip the sync-scaling clip, restored afterwards.
int32_t g_magnitudeBefore = 0;
int32_t g_magnitudeOffsetBefore = 0;
/// UNDODELETE: whether the undo put the clip back in the session.
bool g_restored = false;

/// The counters at the start of the deletion.
int32_t g_clipsBefore = 0;
int32_t g_songsBefore = 0;
int32_t g_soundsBefore = 0;
/// Which step of the deletion is running, for the abort report: a deletion that ends the process has no other way to
/// say where.
const char* volatile g_step = nullptr;
/// The handlers the leg replaced, restored at its report.
std::terminate_handler g_previousTerminate = nullptr;
void (*g_previousAbort)(int) = SIG_DFL;
void (*g_previousSegv)(int) = SIG_DFL;
/// Set by the terminate handler, so the abort report can tell a std::terminate from a firmware freeze.
volatile bool g_terminated = false;

const char* legName(Leg leg) {
	switch (leg) {
	case Leg::SessionDelete:
		return "SESSIONDELETE";
	case Leg::ArrangerDelete:
		return "ARRANGERDELETE";
	case Leg::SwapType:
		return "SWAPTYPE";
	case Leg::UndoDelete:
		return "UNDODELETE";
	case Leg::RecordSource:
		return "RECORDSOURCE";
	case Leg::SyncScale:
		return "SYNCSCALE";
	case Leg::Done:
		break;
	}
	return "DONE";
}

bool fail(const char* what) {
	printf("CL-SETUP FAIL leg=%s %s\n", legName(g_leg), what);
	g_failures++;
	return false;
}

/// A playing-ready session clip on a blank synth of its own, made through the new-clip gesture: the scenario hands it
/// the synth the way InstrumentLoad hands it a parsed preset, so no card is read. Appended after every other clip,
/// and marked active so playback starts it.
InstrumentClip* makeSynthClip() {
	ParamManagerForTimeline paramManager;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (instrument == nullptr) {
		fail("no synth");
		return nullptr;
	}
	auto* synth = static_cast<SoundInstrument*>(instrument);
	synth->setupAsBlankSynth(&paramManager, false);
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(synth->toModControllable()), nullptr,
	                                &paramManager, true);

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

/// Make @p clip the one the UI is on: the song's current clip, and the view's active ModControllable.
void selectClip(Clip* clip) {
	currentSong->setCurrentClip(clip);
	view.setActiveModControllableTimelineCounter(clip);
}

/// Hold a note on @p clip's synth, the audition path's shape: no note row. Voices are reported, not required.
void holdNote(InstrumentClip* clip) {
	auto* synth = static_cast<SoundInstrument*>(clip->output);
	char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* ms =
	    setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, synth, clip, &clip->paramManager);
	synth->Sound::noteOn(ms, &synth->arpeggiator, kNote, zeroMPEValues);
}

/// Build the running leg's clips, with the clock stopped, and point the song's references at the victim.
bool setupLeg() {
	g_victim = nullptr;
	g_recorder = nullptr;
	g_restored = false;
	InstrumentClip* clip = makeSynthClip();
	if (clip == nullptr) {
		return false;
	}
	g_victim = clip;
	switch (g_leg) {
	case Leg::SessionDelete:
	case Leg::SwapType:
	case Leg::UndoDelete:
		// The clip the user is on: its pad was pressed to select it before the delete or the type change.
		selectClip(clip);
		break;
	case Leg::ArrangerDelete: {
		// An arrangement-only clip, made the way the firmware makes one from a session clip: give it an instance in
		// the arranger, then remove it from the session -- removeSessionClip() then moves it to arrangementOnlyClips
		// (section 255) rather than deleting it.
		Output* output = clip->output;
		const int32_t i = output->clipInstances.insertSorted(0).value_or(-1);
		if (i < 0) {
			return fail("no room for a clip instance");
		}
		ClipInstance& instance = output->clipInstances[i];
		instance.length = clip->loopLength;
		instance.clip = clip;
		currentSong->removeSessionClip(clip, currentSong->sessionClips.getIndexForClip(clip));
		if (!clip->isArrangementOnlyClip() || currentSong->arrangementOnlyClips.getIndexForClip(clip) < 0) {
			return fail("the clip did not become arrangement-only");
		}
		// The arranger's clip view: the clip being edited is the current one.
		selectClip(clip);
		break;
	}
	case Leg::RecordSource: {
		// Linear recording's shape (Clip::possiblyCloneForArrangementRecording()): the clip being recorded into
		// carries a pointer back to the clip it records from. Only that reference points at the victim.
		InstrumentClip* recorder = makeSynthClip();
		if (recorder == nullptr) {
			return false;
		}
		recorder->beingRecordedFromClip = clip;
		g_recorder = recorder;
		break;
	}
	case Leg::SyncScale:
		// Only the sync-scaling reference points at the victim.
		g_magnitudeBefore = currentSong->insideWorldTickMagnitude;
		g_magnitudeOffsetBefore = currentSong->insideWorldTickMagnitudeOffsetFromBPM;
		currentSong->setInputTickScaleClip(clip);
		break;
	case Leg::Done:
		break;
	}
	return true;
}

[[noreturn]] void noteTerminatedDeletion() {
	g_terminated = true;
	std::abort();
}

/// Installed around a deletion and the blocks after it, for SIGABRT and SIGSEGV. Every way a deletion (or a render
/// reading what it left dangling) can end the process arrives here: a firmware freeze (FREEZE_WITH_ERROR aborts on the
/// host BSP), a std::terminate (via the handler above) and a fault. The leg still reports its counts, marked aborted=1
/// with the step it died in, and the run its result, before the process dies with the signal it was going to die with.
void reportAbortedDeletion(int signal) {
	const char* leg = legName(g_leg);
	const char* step = (g_step != nullptr) ? g_step : "?";
	const char* why = g_terminated          ? "std::terminate"
	                  : (signal == SIGSEGV) ? "SIGSEGV"
	                                        : "SIGABRT (a freeze or an abort)";
	char line[512];
	int n = snprintf(line, sizeof(line),
	                 "CL-%s clipsReferenced=%d songsReachable=%d soundsLive=%d aborted=1 step=%s\n"
	                 "CL-%s ABORTED the process ended with %s\nCL-RESULT failures=%d\n",
	                 leg, Clip::clipsDestroyedWhileReferenced() - g_clipsBefore,
	                 Song::songsDestroyedWhileReachable() - g_songsBefore,
	                 Sound::soundsDestroyedWithLiveVoices() - g_soundsBefore, step, leg, why, g_failures);
	fflush(stdout);
	if (n > 0) {
		(void)write(STDOUT_FILENO, line, static_cast<size_t>(std::min<int>(n, sizeof(line) - 1)));
	}
	std::signal(signal, SIG_DFL);
	std::raise(signal);
}

/// Render @p count blocks through the app's render entry, as the device's audio task would. The output is discarded.
void renderBlocks(int32_t count) {
	static DelugeStereoSample silence[kRenderBlockFrames]{};
	static DelugeStereoSample output[kRenderBlockFrames]{};
	for (int32_t block = 0; block < count; block++) {
		deluge_app_render(silence, output, kRenderBlockFrames);
	}
}

/// Delete the running leg's clip through its path.
void deleteLeg() {
	Clip* clip = g_victim;
	switch (g_leg) {
	case Leg::SessionDelete:
	case Leg::RecordSource:
		// The session view's delete gesture (SessionView::removeClip(), after its actionLogger.deleteAllLogs()).
		actionLogger.deleteAllLogs();
		g_step = "removeClip";
		sessionView.removeClip(clip);
		break;
	case Leg::ArrangerDelete: {
		// ArrangerView::deleteClipInstance() with no action: erase the instance, then the clip goes with it.
		Output* output = clip->output;
		g_step = "eraseInstance";
		for (int32_t i = 0; i < std::ssize(output->clipInstances); i++) {
			if (output->clipInstances[i].clip == clip) {
				output->clipInstances.erase(output->clipInstances.begin() + i);
				break;
			}
		}
		g_step = "deletingClipInstanceForClip";
		currentSong->deletingClipInstanceForClip(output, clip, nullptr, true);
		break;
	}
	case Leg::SwapType:
		// The session view's change-to-audio gesture: Song::replaceInstrumentClipWithAudioClip() -> swapClips(), then
		// the view is pointed at the new clip -- after the old one is gone.
		g_step = "replaceInstrumentClipWithAudioClip";
		sessionView.replaceInstrumentClipWithAudioClip(clip);
		break;
	case Leg::UndoDelete: {
		// The one way a session clip's deletion is logged: a clip-existence consequence, which does the deletion
		// itself and keeps the clip for the undo. SessionView::removeClip() discards the logs and records nothing.
		g_step = "recordClipExistenceChange";
		Action* action = actionLogger.getNewAction(ActionType::CLIP_DELETE);
		if (action == nullptr
		    || !action->recordClipExistenceChange(currentSong, &currentSong->sessionClips, clip,
		                                          ExistenceChangeType::DELETE)) {
			fail("the deletion could not be logged");
		}
		actionLogger.closeAction(ActionType::CLIP_DELETE);
		break;
	}
	case Leg::SyncScale:
		// SessionView::removeClip() refuses the sync-scaling clip; removeSessionClip() itself does not.
		clip->stopAllNotesPlaying(currentSong);
		g_step = "removeSessionClip";
		currentSong->removeSessionClip(clip, currentSong->sessionClips.getIndexForClip(clip));
		break;
	case Leg::Done:
		break;
	}
}

/// Which of the render's references still hold the deleted clip's address, just after its deletion. Compared by
/// value only. The activeClip of the clip's own output cannot be read here: that output may be gone.
void printDangling() {
	const void* clip = g_victim;
	bool recordSource = false;
	for (ClipArray const* array : {&currentSong->sessionClips, &currentSong->arrangementOnlyClips}) {
		for (Clip const* other : *array) {
			recordSource = recordSource || other->beingRecordedFromClip == clip;
		}
	}
	const bool inSong = currentSong->sessionClips.getIndexForClip(g_victim) >= 0
	                    || currentSong->arrangementOnlyClips.getIndexForClip(g_victim) >= 0;
	printf("CL-DANGLING leg=%s current=%d sync=%d view=%d recordSource=%d inSong=%d\n", legName(g_leg),
	       currentSong->getCurrentClip() == clip ? 1 : 0, currentSong->syncScalingClip == clip ? 1 : 0,
	       view.activeModControllableModelStack.getTimelineCounterAllowNull() == clip ? 1 : 0, recordSource ? 1 : 0,
	       inSong ? 1 : 0);
}

/// Put back what the leg left pointing at a clip that may be gone, stop the clock, and point the UI home.
void cleanUpLeg() {
	Clip* victim = g_victim;
	if (currentSong->getCurrentClip() == victim) {
		currentSong->setCurrentClip(g_home);
	}
	if (view.activeModControllableModelStack.getTimelineCounterAllowNull() == victim) {
		view.setActiveModControllableTimelineCounter(g_home);
	}
	if (currentSong->syncScalingClip == victim) {
		// Not setInputTickScaleClip(): that reads the old clip's length.
		currentSong->syncScalingClip = nullptr;
		currentSong->insideWorldTickMagnitude = g_magnitudeBefore;
		currentSong->insideWorldTickMagnitudeOffsetFromBPM = g_magnitudeOffsetBefore;
	}
	if (g_recorder != nullptr && g_recorder->beingRecordedFromClip == victim) {
		g_recorder->beingRecordedFromClip = nullptr;
	}
	if (playbackHandler.isEitherClockActive()) {
		playbackHandler.endPlayback();
	}
	selectClip(g_home);
	g_victim = nullptr;
	g_recorder = nullptr;
}

/// UNDODELETE's second half, in one step: undo the logged deletion, redo it, then discard the logs -- which destroys
/// the clip the log was keeping (ConsequenceClipExistence::prepareForDestruction()).
void runUndo() {
	g_step = "undo";
	actionLogger.undo();
	g_restored = currentSong->sessionClips.getIndexForClip(g_victim) >= 0;
	// Said now as well as in the leg's line: the redo or the log discard below may end the process.
	printf("CL-UNDO leg=%s restored=%d\n", legName(g_leg), g_restored ? 1 : 0);
	fflush(stdout);
	renderBlocks(kUndoBlocks);
	g_step = "redo";
	actionLogger.redo();
	renderBlocks(kUndoBlocks);
	g_step = "deleteAllLogs";
	actionLogger.deleteAllLogs();
	renderBlocks(kUndoBlocks);
}

void installHandlers() {
	g_terminated = false;
	g_previousTerminate = std::set_terminate(noteTerminatedDeletion);
	g_previousAbort = std::signal(SIGABRT, reportAbortedDeletion);
	g_previousSegv = std::signal(SIGSEGV, reportAbortedDeletion);
}

void restoreHandlers() {
	std::signal(SIGSEGV, g_previousSegv);
	std::signal(SIGABRT, g_previousAbort);
	std::set_terminate(g_previousTerminate);
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
		// Playback on the internal clock, with no count-in: every active clip starts from the top.
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
		g_tickAtStart = playbackHandler.lastTimerTickActioned;
		holdNote(static_cast<InstrumentClip*>(g_victim));
		g_ticks = 0;
		g_stage = Stage::Before;
		break;
	case Stage::Before: {
		renderBlocks(1);
		const auto ticks = static_cast<int32_t>(playbackHandler.lastTimerTickActioned - g_tickAtStart);
		// The internal clock's first tick lands some ten thousand samples after playback is set up, so the leg
		// renders until the render has actioned a few ticks, not just a few blocks.
		if (++g_ticks < kBlocksBefore || (ticks < kTicksBefore && g_ticks < kMaxBlocksBefore)) {
			break;
		}
		auto* synth = static_cast<SoundInstrument*>(g_victim->output);
		const bool playing = playbackHandler.isEitherClockActive() && currentSong->isClipActive(g_victim);
		printf("CL-PROBE leg=%s playing=%d ticks=%d voices=%d current=%d sync=%d view=%d recordSource=%d\n",
		       legName(g_leg), playing ? 1 : 0, ticks, static_cast<int32_t>(synth->numActiveVoices()),
		       currentSong->getCurrentClip() == g_victim ? 1 : 0, currentSong->syncScalingClip == g_victim ? 1 : 0,
		       view.activeModControllableModelStack.getTimelineCounterAllowNull() == g_victim ? 1 : 0,
		       (g_recorder != nullptr && g_recorder->beingRecordedFromClip == g_victim) ? 1 : 0);
		if (ticks <= 0) {
			fail("the render actioned no ticks: the clock never ran");
			g_stage = Stage::Summary;
			break;
		}
		g_stage = Stage::Delete;
		break;
	}
	case Stage::Delete:
		g_clipsBefore = Clip::clipsDestroyedWhileReferenced();
		g_songsBefore = Song::songsDestroyedWhileReachable();
		g_soundsBefore = Sound::soundsDestroyedWithLiveVoices();
		g_step = nullptr;
		// The handlers stay in until the report: what the deletion leaves dangling is read by the blocks after it.
		installHandlers();
		deleteLeg();
		g_step = nullptr;
		printDangling();
		g_ticks = 0;
		g_stage = Stage::After;
		break;
	case Stage::After:
		g_step = "render";
		renderBlocks(1);
		if (++g_ticks < kBlocksAfter) {
			break;
		}
		g_stage = (g_leg == Leg::UndoDelete) ? Stage::Undo : Stage::Report;
		break;
	case Stage::Undo:
		runUndo();
		g_stage = Stage::Report;
		break;
	case Stage::Report: {
		g_step = "report";
		const int32_t clips = Clip::clipsDestroyedWhileReferenced() - g_clipsBefore;
		const int32_t songs = Song::songsDestroyedWhileReachable() - g_songsBefore;
		const int32_t sounds = Sound::soundsDestroyedWithLiveVoices() - g_soundsBefore;
		if (g_leg == Leg::UndoDelete) {
			printf("CL-%s restored=%d clipsReferenced=%d songsReachable=%d soundsLive=%d\n", legName(g_leg),
			       g_restored ? 1 : 0, clips, songs, sounds);
		}
		else {
			printf("CL-%s clipsReferenced=%d songsReachable=%d soundsLive=%d\n", legName(g_leg), clips, songs, sounds);
		}
		fflush(stdout);
		g_step = "cleanUp";
		cleanUpLeg();
		restoreHandlers();
		g_step = nullptr;
		g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
		g_stage = Stage::Setup;
		break;
	}
	case Stage::Summary:
		printf("CL-RESULT failures=%d\n", g_failures);
		// The Rust runner ends a failing run with process::exit, which does NOT flush C stdio.
		fflush(stdout);
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;
	case Stage::Done:
		break;
	}
}

/// The leg `CLIP_LIFETIME_FROM` names, or the first.
Leg firstLeg() {
	const char* from = std::getenv("CLIP_LIFETIME_FROM");
	if (from == nullptr || *from == '\0') {
		return Leg::SessionDelete;
	}
	for (auto leg = static_cast<uint8_t>(Leg::SessionDelete); leg < static_cast<uint8_t>(Leg::Done); leg++) {
		if (std::strcmp(from, legName(static_cast<Leg>(leg))) == 0) {
			return static_cast<Leg>(leg);
		}
	}
	return Leg::Done;
}

} // namespace

extern "C" {

void deluge_scenario_clip_lifetime_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Setup;
	g_leg = firstLeg();
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_ticks = 0;
	// Every leg's gesture is made from the session view. The root UI has to be it: the clip view the harness boots
	// into dereferences getCurrentInstrumentClip() unconditionally, and a leg moves the current clip away from it.
	changeRootUI(&sessionView);
	g_home = currentSong->sessionClips.size() > 0 ? currentSong->sessionClips[0] : nullptr;
	if (g_home == nullptr) {
		fail("the boot song has no clip");
		g_stage = Stage::Summary;
	}
	else if (g_leg == Leg::Done) {
		fail("CLIP_LIFETIME_FROM names no leg");
		g_stage = Stage::Summary;
	}
}

bool deluge_scenario_clip_lifetime_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_clip_lifetime_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// Nothing this tick queued runs anywhere else: the step is done when the tick returns.
	g_stepDone = true;
}

bool deluge_scenario_clip_lifetime_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_clip_lifetime_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
