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

#include "scenarios/card_reinsert_scenario.h"

#include "definitions_cxx.hpp"
#include "gui/ui/load/instrument_load.h"
#include "gui/views/session_view.h"
#include "io/file.hpp"
#include "libdeluge/app.h"
#include "model/clip/audio_clip.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/multi_range/multi_range.h"
#include "storage/storage_manager.h"

#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases printf to an implementation this harness has no
// `putchar_` for -- every line below would become an undefined symbol at link time.
#ifdef printf
#undef printf
#endif

namespace {

// ── The fixture ─────────────────────────────────────────────────────────────
//
// Three residents, in the three states `cardReinserted()`'s walk distinguishes. All three must be
// RESIDENT at the moment of the swap, which is the whole thing `audio-load-ab` lacks.

/// Referenced by the song, and its file is still there after the swap. Must stay loadable.
constexpr const char* kSurvivePath = "SAMPLES/CRSURV.WAV";
/// Referenced by the song, and its file is REMOVED before the re-insert. Must be marked unloadable.
constexpr const char* kLostPath = "SAMPLES/CRLOST.WAV";
/// Loaded and then released, so it is resident but referenced by nothing. Must be evicted.
constexpr const char* kEvictPath = "SAMPLES/CREVICT.WAV";

/// Ticks any one wait may take before the stage is called wedged.
constexpr int32_t kMaxWaitTicks = 4000;

enum class Stage : uint8_t {
	Build,
	Settle,
	Snapshot,
	Eject,
	Mutate,
	Reinsert,
	Report,
	Done,
};

Stage g_stage = Stage::Build;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_waitTicks = 0;

/// Residents counted at Snapshot, before anything is ejected.
int32_t g_samplesBefore = 0;
int32_t g_referencedBefore = 0;

bool fail(const char* what) {
	printf("CR-FAIL %s\n", what);
	g_failures++;
	return false;
}

/// The resident `AudioFile` at @p path, or nullptr when it is no longer in memory.
AudioFile* residentAt(const char* path) {
	for (int32_t i = 0; i < static_cast<int32_t>(audioFileManager.audioFiles.size()); i++) {
		AudioFile* file = audioFileManager.audioFiles[i];
		if (file != nullptr && file->filePath == path) {
			return file;
		}
	}
	return nullptr;
}

/// Count the resident samples, and how many of them a project still holds.
void countResidents(int32_t& samples, int32_t& referenced) {
	samples = 0;
	referenced = 0;
	for (int32_t i = 0; i < static_cast<int32_t>(audioFileManager.audioFiles.size()); i++) {
		AudioFile* file = audioFileManager.audioFiles[i];
		if (file == nullptr || file->type != AudioFileType::SAMPLE) {
			continue;
		}
		samples++;
		if (file->isProjectReferenced()) {
			referenced++;
		}
	}
}

AudioFileHolder* firstHolder(Source& source) {
	MultiRange* range = source.getOrCreateFirstRange();
	return (range != nullptr) ? range->getAudioFileHolder() : nullptr;
}

/// Fixture builds a tick asked for, run when the runner next asks whether the step is done: one virtual
/// millisecond after the tick, matching the pacing the gates are already tuned to. Builds are queued here
/// rather than run inside the tick, to keep that order.
std::vector<void (*)()> g_pendingFixtures{};

/// Queue a fixture build for the end of this step. See g_pendingFixtures.
void buildFixture(void (*fn)()) {
	g_pendingFixtures.push_back(fn);
}

/// Run the next build this step queued, if any: one per call, so each build gets a poll of its own.
/// @return Whether a build ran -- the step is not done on a call that ran one.
bool runPendingFixture() {
	if (g_pendingFixtures.empty()) {
		return false;
	}
	void (*fn)() = g_pendingFixtures.front();
	g_pendingFixtures.erase(g_pendingFixtures.begin());
	fn();
	return true;
}

/// Put the two referenced samples in the song, and leave the third resident but unheld.
bool buildFixture() {
	SoundInstrument* synth = nullptr;
	for (Output* output = currentSong->firstOutput; output != nullptr; output = output->next) {
		if (output->type == OutputType::SYNTH) {
			synth = static_cast<SoundInstrument*>(output);
			break;
		}
	}
	if (synth == nullptr) {
		return fail("the boot song has no synth");
	}
	synth->sources[0].setOscType(OscType::SAMPLE);
	AudioFileHolder* surviveHolder = firstHolder(synth->sources[0]);
	if (surviveHolder == nullptr) {
		return fail("the synth's source has no range");
	}
	surviveHolder->filePath = kSurvivePath;
	(void)synth->loadAllAudioFiles(true);

	Clip* lostClipBase = sessionView.createNewClip(OutputType::AUDIO, 1);
	if (lostClipBase == nullptr || lostClipBase->type != ClipType::AUDIO) {
		return fail("no audio clip could be created for the lost sample");
	}
	auto* lostClip = static_cast<AudioClip*>(lostClipBase);
	lostClip->sampleHolder.filePath = kLostPath;
	lostClip->loadSample(true);

	// The evicted one: loaded through a clip, then released, so it stays in `audioFiles` with nothing
	// holding it. That is precisely the state the walk's `!isProjectReferenced()` branch prunes.
	Clip* evictClipBase = sessionView.createNewClip(OutputType::AUDIO, 2);
	if (evictClipBase == nullptr || evictClipBase->type != ClipType::AUDIO) {
		return fail("no audio clip could be created for the evicted sample");
	}
	auto* evictClip = static_cast<AudioClip*>(evictClipBase);
	evictClip->sampleHolder.filePath = kEvictPath;
	evictClip->loadSample(true);
	evictClip->sampleHolder.setAudioFile(nullptr);

	printf("CR-SETUP survive=%d lost=%d evict=%d\n", residentAt(kSurvivePath) != nullptr ? 1 : 0,
	       residentAt(kLostPath) != nullptr ? 1 : 0, residentAt(kEvictPath) != nullptr ? 1 : 0);
	return true;
}

/// Whether the blank song's own preset request has landed. The boot song starts on the built-in default
/// synth and asks InstrumentLoad for the first SYNTHS preset on the interaction tier; a scenario that
/// acted on the boot clip before that landed would be working on an Instrument about to be replaced --
/// and would find its sources still range-less.
bool bootSettled() {
	static int32_t waited = 0;
	if (currentSong != nullptr && (currentSong->blankPresetPending || deluge::gui::instrumentLoad.inFlight())
	    && ++waited < 2000) {
		return false;
	}
	return true;
}

void runTick() {
	if (!bootSettled()) {
		return;
	}
	switch (g_stage) {

	case Stage::Build:
		buildFixture([] {
			if (!buildFixture()) {
				g_stage = Stage::Report;
			}
		});
		if (g_stage == Stage::Report) {
			return;
		}
		g_waitTicks = 0;
		g_stage = Stage::Settle;
		return;

	case Stage::Settle:
		// Loads span ticks; let them land before anything is counted.
		AudioEngine::routine();
		if (++g_waitTicks < 64) {
			return;
		}
		g_stage = Stage::Snapshot;
		return;

	case Stage::Snapshot: {
		countResidents(g_samplesBefore, g_referencedBefore);
		// CR-RESIDENTS is not ceremony. Three gates in this branch have been green because the thing
		// under test never ran; this line is what makes that visible here.
		AudioFile* survivorBefore = residentAt(kSurvivePath);
		const int32_t survivorUnloadableBefore =
		    (survivorBefore != nullptr && survivorBefore->type == AudioFileType::SAMPLE)
		        ? (static_cast<Sample*>(survivorBefore)->unloadable ? 1 : 0)
		        : -1;
		printf("CR-RESIDENTS samples=%d referenced=%d survivorUnloadableBefore=%d\n", (int)g_samplesBefore,
		       (int)g_referencedBefore, (int)survivorUnloadableBefore);
		g_stage = Stage::Eject;
		return;
	}

	case Stage::Eject:
		audioFileManager.setCardEjected();
		g_stage = Stage::Mutate;
		return;

	case Stage::Mutate: {
		// The swap is a file change at a path, not a block-device exchange: the host BSP opens ONE
		// backing file for the process lifetime behind a OnceLock with is_inserted() hard-coded true,
		// so there is no image to exchange. What the code under test sees is identical either way --
		// the file at a path it recorded is now gone. See audio_load_ab_scenario.cpp for the same
		// limitation stated at length.
		const bool removed = deluge::io::unlink(kLostPath).has_value();
		if (!removed) {
			(void)fail("the lost sample's file could not be removed");
		}
		g_waitTicks = 0;
		g_stage = Stage::Reinsert;
		return;
	}

	case Stage::Reinsert:
		// Through slowRoutine(), NOT reinitEjectedCard() directly: slowRoutine is the path the device takes.
		audioFileManager.slowRoutine();
		AudioEngine::routine();
		// Bounded rather than conditional: `cardEjected` is private, and the fates below are themselves
		// the evidence that the walk ran. 256 ticks is far more than the queue-then-run round trip needs.
		if (++g_waitTicks < 256) {
			return;
		}
		g_stage = Stage::Report;
		return;

	case Stage::Report: {
		AudioFile* survivor = residentAt(kSurvivePath);
		AudioFile* lost = residentAt(kLostPath);
		AudioFile* evicted = residentAt(kEvictPath);

		const int32_t survivorUnloadable = (survivor != nullptr && survivor->type == AudioFileType::SAMPLE)
		                                       ? (static_cast<Sample*>(survivor)->unloadable ? 1 : 0)
		                                       : -1;
		const int32_t lostUnloadable = (lost != nullptr && lost->type == AudioFileType::SAMPLE)
		                                   ? (static_cast<Sample*>(lost)->unloadable ? 1 : 0)
		                                   : -1;

		printf("CR-SURVIVE ok=%d unloadable=%d resident=%d\n", (int)(survivorUnloadable == 0), (int)survivorUnloadable,
		       (int)(survivor != nullptr));
		printf("CR-LOST ok=%d unloadable=%d resident=%d\n", (int)(lostUnloadable == 1), (int)lostUnloadable,
		       (int)(lost != nullptr));
		printf("CR-EVICT ok=%d stillResident=%d\n", (int)(evicted == nullptr), (int)(evicted != nullptr));

		if (g_samplesBefore <= 0) {
			(void)fail("the walk had no resident samples to visit");
		}
		if (survivorUnloadable != 0) {
			(void)fail("the intact sample did not survive the swap");
		}
		if (lostUnloadable != 1) {
			(void)fail("the removed sample was not marked unloadable");
		}
		if (evicted != nullptr) {
			(void)fail("the unreferenced resident was not evicted");
		}

		printf("CR-RESULT failures=%d\n", (int)g_failures);
		fflush(nullptr);
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		return;
	}

	case Stage::Done:
		return;
	}
}

} // namespace

extern "C" {

void deluge_scenario_card_reinsert_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Build;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_waitTicks = 0;
	g_samplesBefore = 0;
	g_referencedBefore = 0;
}

bool deluge_scenario_card_reinsert_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_card_reinsert_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// Nothing this tick queued runs anywhere else: the step is done when the tick returns.
	g_stepDone = true;
}

bool deluge_scenario_card_reinsert_step_done(void) {
	if (runPendingFixture()) {
		return false;
	}
	return g_stepDone;
}

int32_t deluge_scenario_card_reinsert_failures(void) {
	return g_failures;
}

} // extern "C"
