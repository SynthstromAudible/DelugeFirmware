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
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_holder.h"
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

constexpr int32_t kMaxSettleTicks = 400;
constexpr int32_t kMaxClearTicks = 2000;

enum class Stage : uint8_t { Fixture, Settle, Clear, Wait, Report, Done };
Stage g_stage = Stage::Fixture;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_ticks = 0;

bool g_fixtureWritten = false;
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
		g_stage = Stage::Clear;
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
