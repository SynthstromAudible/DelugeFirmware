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

#include "harness/clear_song_undo_scenario.h"

#ifdef DELUGE_HOST

#include "gui/context_menu/clear_song.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "io/file.hpp"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"
#include <cstdio>
#include <string>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded
// `printf_`. That implementation emits through `putchar_`, which only the C host BSP defines --
// this harness links the Rust BSP and has no provider, so the alias turns every line this file
// writes into an undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

// The pending-command pair `View::buttonAction()` sets for UNDO and REDO: file-scope in playback_handler.cpp, reached
// by extern the same way view.cpp reaches it. Driving the gesture through them is what makes these legs the button's
// path rather than a private call into the action logger.
extern GlobalMIDICommand pendingGlobalMIDICommand;
extern int32_t pendingGlobalMIDICommandNumClustersWritten;

namespace {

/// The preset the cleared song must land on, and the sample it binds. Written through the firmware at the first
/// stage: a fixture is never hand-written XML.
constexpr const char* kSynthPreset = "SYNTHS/CSSYN.XML";
constexpr const char* kSynthSample = "SAMPLES/CS/CSSYN.WAV";
/// The built-in synth a song with no preset starts on.
constexpr const char* kDefaultSynthName = "0";
/// The name stamped on the boot song's instrument, so the clear can be seen to have replaced the whole song even
/// though the allocator hands the new Song the address the old one just freed.
constexpr const char* kOldSongMark = "CSOLD";

/// The undo legs: the samples an AudioClip's sample change moves between, and the one deleted from the card before
/// the undo asks for it back.
constexpr const char* kUndoSampleA = "SAMPLES/CS/CSUNA.WAV";
constexpr const char* kUndoSampleB = "SAMPLES/CS/CSUNB.WAV";
constexpr const char* kUndoSampleGone = "SAMPLES/CS/CSGONE.WAV";

constexpr int32_t kMaxSettleTicks = 400;
constexpr int32_t kMaxClearTicks = 2000;
/// How long a reversion is given to land before the leg reads its result. A dispatched one needs the owner's queue to
/// turn over; one that runs here needs nothing.
constexpr int32_t kUndoSettleTicks = 40;

/// Which undo leg is running. They share one set-sample fixture and differ in what they ask of the reversion.
enum class UnLeg : uint8_t { Sample, Redo, Plain, Refuse, Missing, Done };

enum class Stage : uint8_t { Fixture, Settle, UnSetup, UnAct, UnWait, UnCheck, Clear, Wait, Report, Summary, Done };
Stage g_stage = Stage::Fixture;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_ticks = 0;

bool g_fixtureWritten = false;

/// The undo legs' state: which leg, the clip it works on, and what the reversion was seen to do.
UnLeg g_unLeg = UnLeg::Sample;
AudioClip* g_unClip = nullptr;
int32_t g_unTicks = 0;
bool g_unLandedInPress = false;
int32_t g_unAcceptedCommands = 0;
int8_t g_unSwingBefore = 0;
std::string g_unBoundAfter;
bool g_unBoundToFile = false;

/// What the clear was seen to have done by the time `acceptCurrentOption()` returned.
bool g_swappedOnReturn = false;
bool g_modeReleasedOnReturn = false;
/// InstrumentLoad's counters across the clear itself: the clear must read nothing of its own.
int32_t g_readsBefore = 0;
int32_t g_ownerReadsBefore = 0;
int32_t g_loadsBefore = 0;
int32_t g_ownerLoadsBefore = 0;
int32_t g_readsOnReturn = 0;
int32_t g_loadsOnReturn = 0;
int32_t g_runsBefore = 0;

/// The clip a song starts with: its first session clip. Null while a clear is between songs.
InstrumentClip* firstClip() {
	if (currentSong == nullptr || currentSong->sessionClips.empty()) {
		return nullptr;
	}
	return static_cast<InstrumentClip*>(currentSong->sessionClips[0]);
}

/// The Instrument that clip plays.
Instrument* firstInstrument() {
	InstrumentClip* clip = firstClip();
	return (clip != nullptr && clip->output != nullptr) ? static_cast<Instrument*>(clip->output) : nullptr;
}

/// The first sample holder of a synth's source, or null.
AudioFileHolder* firstHolder(Source& source) {
	MultiRange* range = source.getOrCreateFirstRange();
	return (range != nullptr) ? range->getAudioFileHolder() : nullptr;
}

/// Whether the song's clip plays a synth bound to a file.
bool clipBound() {
	Instrument* instrument = firstInstrument();
	if (instrument == nullptr || instrument->type != OutputType::SYNTH) {
		return false;
	}
	AudioFileHolder* holder = firstHolder(static_cast<SoundInstrument*>(instrument)->sources[0]);
	return holder != nullptr && !holder->filePath.empty();
}

/// Write @p path as a synth preset whose first oscillator is @p samplePath, through the firmware's own serialiser.
/// The Instrument it is written from is leaked, as every harness fixture's is.
bool writeSampleSynthPreset(const char* path, const char* samplePath) {
	ParamManagerForTimeline paramManager;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (instrument == nullptr) {
		return false;
	}
	auto* synth = static_cast<SoundInstrument*>(instrument);
	synth->setupAsBlankSynth(&paramManager, false);
	synth->sources[0].setOscType(OscType::SAMPLE);
	AudioFileHolder* holder = firstHolder(synth->sources[0]);
	if (holder == nullptr) {
		return false;
	}
	holder->filePath = samplePath;
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(instrument->toModControllable()), nullptr,
	                                &paramManager, true);
	Error error = StorageManager::createXMLFile(path, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return false;
	}
	instrument->writeToFile(nullptr, currentSong);
	error = GetSerializer().closeFileAfterWriting(path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", "\n</sound>\n");
	return error == Error::NONE;
}

/// The path the undo legs' clip names, which the revert rewrites whether or not the file behind it loads.
std::string boundPath() {
	return (g_unClip != nullptr) ? std::string{g_unClip->sampleHolder.filePath.c_str()} : std::string{};
}

/// Whether that path actually has a file behind it. The path alone proves nothing: a revert whose file is gone still
/// writes the path and leaves the holder empty, which is what UN-MISSING is about.
bool boundToFile() {
	return g_unClip != nullptr && g_unClip->sampleHolder.audioFile != nullptr;
}

/// Build an undo leg's fixture: an AudioClip whose sample is set, then set again with the change recorded the way
/// AudioClip::clear() and the sample browser record it. @p first is dropped from memory afterwards, so undoing the
/// change has to read the card for it.
bool setupSampleChange(const char* first, const char* second, int32_t yDisplay) {
	g_unClip = static_cast<AudioClip*>(sessionView.createNewAudioClipForHarness(yDisplay));
	if (g_unClip == nullptr) {
		return false;
	}
	// What the UI does when an AUDIO clip is on screen. The root UI has to move with the clip: the session view's
	// getAffectEntire() reads the current clip generically, where InstrumentClipView's dereferences
	// getCurrentInstrumentClip() unconditionally -- null for an AudioClip, which is how a scenario that leaves the
	// clip view as root UI takes the whole process down.
	changeRootUI(&sessionView);
	currentSong->setCurrentClip(g_unClip);
	view.setActiveModControllableTimelineCounter(g_unClip);

	g_unClip->sampleHolder.filePath = first;
	(void)g_unClip->sampleHolder.loadFile(false, false, true);

	Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR);
	if (action == nullptr) {
		return false;
	}
	action->recordAudioClipSampleChange(g_unClip);

	g_unClip->sampleHolder.setAudioFile(nullptr);
	g_unClip->sampleHolder.filePath = second;
	(void)g_unClip->sampleHolder.loadFile(false, false, true);
	// Nothing holds the first sample now, so the undo cannot find it in memory.
	(void)audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(first);
	return true;
}

/// The BACK button's path and the MIDI command's path both end here: pend the command, then run the routine that
/// actions it.
void pressCommand(GlobalMIDICommand command) {
	if (!actionLogger.allowedToDoReversion()) {
		return;
	}
	g_unAcceptedCommands++;
	pendingGlobalMIDICommand = command;
	pendingGlobalMIDICommandNumClustersWritten = 0;
	playbackHandler.slowRoutine();
}

/// CS-TIER: the clear ran where the caller stands, and read no card of its own.
void checkTier() {
	const int32_t reads = g_readsOnReturn - g_readsBefore;
	const int32_t loads = g_loadsOnReturn - g_loadsBefore;
	const int32_t ownerReads = deluge::gui::instrumentLoad.documentReadsOnOwner() - g_ownerReadsBefore;
	const int32_t ownerLoads = deluge::gui::instrumentLoad.sampleLoadsOnOwner() - g_ownerLoadsBefore;
	printf("CS-TIER swappedOnReturn=%d modeReleased=%d readsInClear=%d loadsInClear=%d onOwnerReads=%d "
	       "onOwnerLoads=%d\n",
	       g_swappedOnReturn ? 1 : 0, g_modeReleasedOnReturn ? 1 : 0, reads, loads, ownerReads, ownerLoads);
	if (!g_swappedOnReturn || !g_modeReleasedOnReturn || reads != 0 || loads != 0 || ownerReads != 0
	    || ownerLoads != 0) {
		printf("CS-TIER FAIL the clear did not run to completion on the caller's tier, or it read the card doing "
		       "it\n");
		g_failures++;
	}
}

/// CS-SONG: the cleared song holds one clip, and its preset arrived through InstrumentLoad.
void checkSong() {
	Instrument* instrument = firstInstrument();
	const std::string name = (instrument != nullptr) ? instrument->name : "";
	const int32_t clips = (currentSong != nullptr) ? static_cast<int32_t>(currentSong->sessionClips.size()) : 0;
	const int32_t runs = LoadInstrumentPresetUI::presetAcquisitionsRun() - g_runsBefore;
	printf("CS-SONG clips=%d name=%s bound=%d acquisitions=%d ticks=%d\n", clips, name.c_str(), clipBound() ? 1 : 0,
	       runs, g_ticks);
	if (clips != 1 || name.empty() || name == kDefaultSynthName || name == kOldSongMark || !clipBound() || runs != 1) {
		printf("CS-SONG FAIL the cleared song did not land on a SYNTHS preset acquired by InstrumentLoad\n");
		g_failures++;
	}
}

/// CS-GATE: the clear left no gate closed behind it, and the UI takes input again.
void checkGate() {
	const bool released = currentUIMode == UI_MODE_NONE;
	// A press the UI must accept: with the mode stranded, `isUIModeWithinRange()` would refuse it.
	const bool accepted = released && getCurrentUI() != nullptr;
	printf("CS-GATE released=%d accepted=%d\n", released ? 1 : 0, accepted ? 1 : 0);
	if (!released || !accepted) {
		printf("CS-GATE FAIL the clear left the UI-mode gate closed behind it\n");
		g_failures++;
	}
}

/// One line per undo leg, named for the leg, with the failure it guards.
void checkUndoLeg() {
	const std::string& bound = g_unBoundAfter;
	switch (g_unLeg) {
	case UnLeg::Sample:
		// A pin, not a tier claim: the sample comes back either way, and no counter distinguishes WHERE it was read
		// (IoTier::SampleLoad is a tripwire for an unreachable case, not a general count). UN-PLAIN carries the tier
		// claim instead -- a reversion dispatched onto the owner cannot land inside the press, and one on the
		// caller's tier does.
		printf("UN-SAMPLE bound=%s file=%d ticks=%d\n", bound.c_str(), g_unBoundToFile ? 1 : 0, g_unTicks);
		if (bound != kUndoSampleA || !g_unBoundToFile) {
			printf("UN-SAMPLE FAIL the undo did not put the first sample back and bind it\n");
			g_failures++;
		}
		break;
	case UnLeg::Redo:
		printf("UN-REDO bound=%s file=%d ticks=%d\n", bound.c_str(), g_unBoundToFile ? 1 : 0, g_unTicks);
		if (bound != kUndoSampleB || !g_unBoundToFile) {
			printf("UN-REDO FAIL redo did not put the second sample back\n");
			g_failures++;
		}
		break;
	case UnLeg::Plain:
		printf("UN-PLAIN landedInPress=%d swing=%d wanted=%d ticks=%d\n", g_unLandedInPress ? 1 : 0,
		       static_cast<int>(currentSong->swingAmount), static_cast<int>(g_unSwingBefore), g_unTicks);
		if (!g_unLandedInPress || currentSong->swingAmount != g_unSwingBefore) {
			printf("UN-PLAIN FAIL an undo with no sample in it did not land in the tick it was asked for\n");
			g_failures++;
		}
		break;
	case UnLeg::Refuse:
		printf("UN-REFUSE bound=%s file=%d commands=%d ticks=%d\n", bound.c_str(), g_unBoundToFile ? 1 : 0,
		       g_unAcceptedCommands, g_unTicks);
		if (bound != kUndoSampleA || !g_unBoundToFile || g_unAcceptedCommands != 2) {
			printf("UN-REFUSE FAIL the redo pressed while the undo was still out was not refused\n");
			g_failures++;
		}
		break;
	case UnLeg::Missing:
		// The revert happens, the path comes back, and the holder is EMPTY because the file is gone: exactly what
		// revert() does today when its own loadFile() fails.
		printf("UN-MISSING bound=%s file=%d ticks=%d\n", bound.c_str(), g_unBoundToFile ? 1 : 0, g_unTicks);
		if (bound != kUndoSampleGone || g_unBoundToFile) {
			printf("UN-MISSING FAIL the revert did not happen, or it bound a file that is not on the card\n");
			g_failures++;
		}
		break;
	case UnLeg::Done:
		break;
	}
}

/// Build the running leg's fixture. Redo works on what the Sample leg left behind.
bool setupUndoLeg() {
	switch (g_unLeg) {
	case UnLeg::Sample:
		return setupSampleChange(kUndoSampleA, kUndoSampleB, 1);
	case UnLeg::Redo:
		return true;
	case UnLeg::Plain: {
		// An action with no sample in it at all: a swing change reads no card on either tier, and the song's swing
		// is what the undo has to put back.
		g_unSwingBefore = currentSong->swingAmount;
		const int8_t after = static_cast<int8_t>(g_unSwingBefore + 8);
		actionLogger.recordSwingChange(g_unSwingBefore, after);
		currentSong->swingAmount = after;
		return true;
	}
	case UnLeg::Refuse:
		return setupSampleChange(kUndoSampleA, kUndoSampleB, 2);
	case UnLeg::Missing: {
		if (!setupSampleChange(kUndoSampleGone, kUndoSampleB, 3)) {
			return false;
		}
		// The card loses the file the undo is about to ask for.
		deluge::storage::Owner::run_or_inline([](void*) { (void)deluge::io::unlink(kUndoSampleGone); }, nullptr);
		return true;
	}
	case UnLeg::Done:
		return true;
	}
	return true;
}

void runTick() {
	switch (g_stage) {
	case Stage::Fixture:
		deluge::storage::Owner::run_or_inline(
		    [](void*) {
			    g_fixtureWritten = writeSampleSynthPreset(kSynthPreset, kSynthSample);
			    printf("CS-PROBE fixture written=%d\n", g_fixtureWritten ? 1 : 0);
		    },
		    nullptr);
		g_ticks = 0;
		g_stage = Stage::Settle;
		break;
	case Stage::Settle: {
		// The boot song asks InstrumentLoad for its own first preset; acting before that lands would work on an
		// Instrument about to be replaced, which no user can do.
		const bool pending = currentSong != nullptr && currentSong->blankPresetPending;
		if ((pending || deluge::gui::instrumentLoad.inFlight()) && ++g_ticks < kMaxSettleTicks) {
			break;
		}
		if (Instrument* instrument = firstInstrument(); instrument != nullptr) {
			// Stamped so the clear can be seen to have replaced the whole song: the new Song lands at the address
			// the old one just freed, so a pointer comparison never fires.
			instrument->name = kOldSongMark;
		}
		printf("CS-PROBE settled ticks=%d fixture=%d\n", g_ticks, g_fixtureWritten ? 1 : 0);
		g_ticks = 0;
		g_unLeg = UnLeg::Sample;
		g_stage = Stage::UnSetup;
		break;
	}
	case Stage::Clear: {
		g_readsBefore = deluge::gui::instrumentLoad.documentReads();
		g_ownerReadsBefore = deluge::gui::instrumentLoad.documentReadsOnOwner();
		g_loadsBefore = deluge::gui::instrumentLoad.sampleLoads();
		g_ownerLoadsBefore = deluge::gui::instrumentLoad.sampleLoadsOnOwner();
		g_runsBefore = LoadInstrumentPresetUI::presetAcquisitionsRun();

		(void)deluge::gui::context_menu::clearSong.acceptCurrentOption();

		// Read the instant it returns: a dispatched body has not run yet, a synchronous one is done.
		Instrument* instrument = firstInstrument();
		g_swappedOnReturn = instrument == nullptr || instrument->name != kOldSongMark;
		g_modeReleasedOnReturn = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		g_readsOnReturn = deluge::gui::instrumentLoad.documentReads();
		g_loadsOnReturn = deluge::gui::instrumentLoad.sampleLoads();
		printf("CS-PROBE cleared swappedOnReturn=%d modeReleased=%d\n", g_swappedOnReturn ? 1 : 0,
		       g_modeReleasedOnReturn ? 1 : 0);
		g_ticks = 0;
		g_stage = Stage::Wait;
		break;
	}
	case Stage::Wait: {
		// The clear is over once the swap has happened, its gate is released, and the new song's own preset
		// request has landed.
		const bool cleared = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		const bool acquired = LoadInstrumentPresetUI::presetAcquisitionsRun() > g_runsBefore;
		const bool ended = cleared && acquired && !deluge::gui::instrumentLoad.inFlight();
		if (!ended && ++g_ticks < kMaxClearTicks) {
			break;
		}
		g_stage = Stage::Report;
		break;
	}
	case Stage::Report:
		checkTier();
		checkSong();
		checkGate();
		g_stage = Stage::Summary;
		break;
	case Stage::UnSetup:
		if (!setupUndoLeg()) {
			printf("UN-PROBE FAIL leg=%d the fixture could not be built\n", static_cast<int>(g_unLeg));
			g_failures++;
			g_stage = Stage::Summary;
			break;
		}
		g_unAcceptedCommands = 0;
		g_unLandedInPress = false;
		g_unTicks = 0;
		g_stage = Stage::UnAct;
		break;
	case Stage::UnAct: {
		const std::string before = boundPath();
		pressCommand(g_unLeg == UnLeg::Redo ? GlobalMIDICommand::REDO : GlobalMIDICommand::UNDO);
		if (g_unLeg == UnLeg::Refuse) {
			// A second command in the same tick, while the first is still out.
			pressCommand(GlobalMIDICommand::REDO);
		}
		// "Landed in the press" means the model had already moved when the press returned: the swing restored for
		// the plain leg, the clip's file changed for the others.
		g_unLandedInPress =
		    (g_unLeg == UnLeg::Plain) ? (currentSong->swingAmount == g_unSwingBefore) : (boundPath() != before);
		printf("UN-PROBE leg=%d pressed commands=%d landedInPress=%d\n", static_cast<int>(g_unLeg),
		       g_unAcceptedCommands, g_unLandedInPress ? 1 : 0);
		g_stage = Stage::UnWait;
		break;
	}
	case Stage::UnWait:
		// A dispatched reversion needs the owner's queue to turn over before its result can be read; one that runs
		// on this tier is already done. The same bounded wait covers both, so neither is read too early.
		if (++g_unTicks < kUndoSettleTicks) {
			break;
		}
		g_unBoundAfter = boundPath();
		g_unBoundToFile = boundToFile();
		g_stage = Stage::UnCheck;
		break;
	case Stage::UnCheck:
		checkUndoLeg();
		g_unLeg = static_cast<UnLeg>(static_cast<uint8_t>(g_unLeg) + 1);
		// The clear runs last: it replaces the song these legs edited and nullifies the UIs, and a reversion after
		// that reaches View::setKnobIndicatorLevels() with nothing behind it.
		g_stage = (g_unLeg == UnLeg::Done) ? Stage::Clear : Stage::UnSetup;
		break;
	case Stage::Summary:
		printf("CS-RESULT failures=%d\n", g_failures);
		// The Rust runner ends a failing run with process::exit, which does NOT flush C stdio. These lines ARE the
		// gate's evidence, so losing them exactly when the gate fails is the one case that must not happen.
		fflush(stdout);
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;
	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_clear_song_undo_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Fixture;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_ticks = 0;
	g_fixtureWritten = false;
	g_swappedOnReturn = false;
	g_modeReleasedOnReturn = false;
}

bool deluge_scenario_clear_song_undo_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_clear_song_undo_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// A marker op behind whatever the stage queued: the owner queue is FIFO, so this runs after it.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_clear_song_undo_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_clear_song_undo_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
