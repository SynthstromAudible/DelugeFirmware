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

#include "harness/sound_lifetime_scenario.h"

#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "extern.h"
#include "libdeluge/app.h"
#include "libdeluge/types.h"
#include "memory/fast_allocator.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>

// The firmware headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded `printf_`. This
// harness links the Rust BSP, which has no `putchar_` provider, and every line this file writes IS the gate's output.
#ifdef printf
#undef printf
#endif

namespace {

/// The note every leg holds. A blank synth sustains it until a note-off, which no leg sends.
constexpr int32_t kNote = 60;
/// The render block the scenario drives, and how many it renders over each held note before deleting it. Nothing
/// else renders here: the golden harness skips the audio task's own render once boot has finished.
constexpr uint32_t kRenderBlockFrames = 128;
constexpr int32_t kRenderBlocksPerNote = 4;
/// How many steps a leg waits after its deletion before it reads the count, so any deferred destruction has run.
constexpr int32_t kSettleTicks = 10;

/// Each leg is one real deletion path, run on a Sound that is sounding when it is deleted.
enum class Leg : uint8_t { Synth, Unedited, Kit, DrumTail, Done };

enum class Stage : uint8_t { Setup, Sounding, Delete, Settle, Report, Summary, Done };

Stage g_stage = Stage::Setup;
Leg g_leg = Leg::Synth;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_ticks = 0;

/// The running leg's objects: the synth is null on a kit leg, and the kit and drum are null on a synth leg.
SoundInstrument* g_synth = nullptr;
Kit* g_kit = nullptr;
SoundDrum* g_drum = nullptr;
/// The Sound the leg holds a note on, for the checks: the synth, or the drum.
Sound* g_sound = nullptr;
/// The render's sample clock when the note went on: the leg deletes only once it has moved.
uint32_t g_timerAtNoteOn = 0;
/// The counter before the deletion.
int32_t g_before = 0;

const char* legName(Leg leg) {
	switch (leg) {
	case Leg::Synth:
		return "SYNTH";
	case Leg::Unedited:
		return "UNEDITED";
	case Leg::Kit:
		return "KIT";
	case Leg::DrumTail:
		return "DRUMTAIL";
	case Leg::Done:
		break;
	}
	return "DONE";
}

bool fail(const char* what) {
	printf("SL-SETUP FAIL leg=%s %s\n", legName(g_leg), what);
	g_failures++;
	return false;
}

/// A blank synth, built the way the new-synth path builds one. Not added to any list.
SoundInstrument* makeSynth() {
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
	return synth;
}

/// A kit holding one blank-synth drum. Not added to any list.
Kit* makeKitWithSynthDrum(SoundDrum** drumOut) {
	ParamManagerForTimeline kitParams;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	if (instrument == nullptr) {
		fail("no kit");
		return nullptr;
	}
	auto* kit = static_cast<Kit*>(instrument);
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(kit->toModControllable()), nullptr, &kitParams,
	                                true);

	ParamManagerForTimeline paramManager;
	if (paramManager.setupWithPatching() != Error::NONE) {
		fail("a drum's ParamManager could not be set up");
		return nullptr;
	}
	void* memory = deluge::memory::alloc_fast(sizeof(SoundDrum));
	if (memory == nullptr) {
		fail("no memory for a drum");
		return nullptr;
	}
	auto* drum = new (memory) SoundDrum();
	Sound::initParams(&paramManager);
	kit->addDrum(drum);
	drum->setupAsBlankSynth(&paramManager);
	currentSong->backUpParamManager(drum, nullptr, &paramManager, true);
	*drumOut = drum;
	return kit;
}

/// Give @p instrument a clip of its own, the way the new-track gesture does (InstrumentClip::setAudioInstrument, then
/// setActiveClip), without putting the clip in the song. A synth renders only through its active clip, and reads its
/// arp settings from it, so a synth with none can neither sound nor be heard. The clip is leaked, as the harness
/// fixtures are: the song never reaches it.
InstrumentClip* attachClip(Instrument* instrument) {
	void* memory = deluge::memory::alloc_fast(sizeof(InstrumentClip));
	if (memory == nullptr) {
		fail("no memory for a clip");
		return nullptr;
	}
	auto* clip = new (memory) InstrumentClip(currentSong);
	if (clip->setAudioInstrument(instrument, currentSong, true, nullptr) != Error::NONE) {
		fail("the clip could not take the instrument");
		return nullptr;
	}
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(clip);
	if (instrument->type == OutputType::KIT) {
		// A note row per drum, as the new-track gesture gives a kit clip: the kit's render kills the voices of any
		// drum its active clip has no row for.
		clip->setupAsNewKitClipIfNecessary(modelStack);
	}
	instrument->setActiveClip(modelStack);
	return clip;
}

/// Hold a note on @p s, the audition path's shape: no note row.
bool sound(Sound& s, ModControllable* mc, ArpeggiatorBase* arp, InstrumentClip* clip, ParamManager* paramManager) {
	if (paramManager == nullptr) {
		return fail("the sound has no ParamManager");
	}
	char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* ms =
	    setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, mc, clip, paramManager);
	s.Sound::noteOn(ms, arp, kNote, zeroMPEValues);
	if (s.voices().empty()) {
		return fail("the sound never sounded");
	}
	return true;
}

/// Build the running leg's Sound and hold a note on it.
bool setupLeg() {
	g_synth = nullptr;
	g_kit = nullptr;
	g_drum = nullptr;
	g_sound = nullptr;
	switch (g_leg) {
	case Leg::Synth:
	case Leg::Unedited: {
		g_synth = makeSynth();
		if (g_synth == nullptr) {
			return false;
		}
		InstrumentClip* clip = attachClip(g_synth);
		if (clip == nullptr) {
			return false;
		}
		if (g_leg == Leg::Synth) {
			currentSong->addOutput(g_synth);
		}
		else if (g_synth->editedByUser) {
			// deleteOrAddToHibernationListOutput() would hibernate it, and nothing would be destroyed.
			return fail("a new synth reads as edited by the user");
		}
		g_sound = g_synth;
		return sound(*g_synth, g_synth, &g_synth->arpeggiator, clip, &clip->paramManager);
	}
	case Leg::Kit:
	case Leg::DrumTail: {
		g_kit = makeKitWithSynthDrum(&g_drum);
		if (g_kit == nullptr) {
			return false;
		}
		InstrumentClip* clip = attachClip(g_kit);
		if (clip == nullptr) {
			return false;
		}
		if (g_leg == Leg::Kit) {
			currentSong->addOutput(g_kit);
		}
		NoteRow* row = clip->getNoteRowForDrum(g_drum);
		if (row == nullptr) {
			return fail("the kit's clip has no row for the drum");
		}
		g_sound = g_drum;
		return sound(*g_drum, g_drum, &g_drum->arpeggiator, clip, &row->paramManager);
	}
	case Leg::Done:
		break;
	}
	return true;
}

/// Installed around a deletion: a destruction that ends in std::terminate (a pure virtual call from a voice unassigned
/// into a half-destroyed Sound) still reports the leg's count and the run's result before the process aborts. The count
/// is taken in ~Sound's body, before its members -- the voices -- are destroyed, so it is already in.
[[noreturn]] void reportTerminatedDeletion() {
	printf("SL-%s destroyedWithLiveVoices=%d terminated=1\n", legName(g_leg),
	       Sound::soundsDestroyedWithLiveVoices() - g_before);
	printf("SL-%s FAIL the process terminated inside the deletion\n", legName(g_leg));
	g_failures++;
	printf("SL-RESULT failures=%d\n", g_failures);
	fflush(stdout);
	std::abort();
}

/// Delete the running leg's Sound through its path.
void deleteLeg() {
	switch (g_leg) {
	case Leg::Synth:
		currentSong->deleteOutputThatIsInMainList(g_synth);
		break;
	case Leg::Unedited:
		currentSong->deleteOrAddToHibernationListOutput(g_synth);
		break;
	case Leg::Kit:
		currentSong->deleteOutputThatIsInMainList(g_kit);
		break;
	case Leg::DrumTail: {
		// The tail the drum-delete sites share (the clip view's drum delete, folder import, instrument load, preset
		// replace, the slicer): unlink the drum, drop its backed-up ParamManagers, destroy it and free it. The kit
		// itself is leaked, as the harness fixtures are.
		//
		// A sounding drum is in its kit's drumsWithRenderingActive, and Kit::drumRemoved() freezes with E321 on an
		// ALPHA_OR_BETA_VERSION build (the host's) when a drum still in that set is removed -- which is what the clip
		// view's deleteDrum() does with a drum that is still sounding. The erase is the one the preset-replace site
		// makes before its removeDrum(); without it this leg never reaches the destruction it is here to measure.
		g_kit->drumsWithRenderingActive.erase(g_drum);
		g_kit->removeDrum(g_drum);
		currentSong->deleteBackedUpParamManagersForModControllable(g_drum);
		void* toDealloc = dynamic_cast<void*>(g_drum);
		g_drum->~SoundDrum();
		delugeDealloc(toDealloc);
		break;
	}
	case Leg::Done:
		break;
	}
	g_synth = nullptr;
	g_kit = nullptr;
	g_drum = nullptr;
	g_sound = nullptr;
}

/// Render @p count blocks through the app's render entry, as the device's audio task would. The output is discarded.
void renderBlocks(int32_t count) {
	static DelugeStereoSample silence[kRenderBlockFrames]{};
	static DelugeStereoSample output[kRenderBlockFrames]{};
	for (int32_t block = 0; block < count; block++) {
		deluge_app_render(silence, output, kRenderBlockFrames);
	}
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
		g_timerAtNoteOn = AudioEngine::audioSampleTimer;
		g_stage = Stage::Sounding;
		break;
	case Stage::Sounding: {
		// The render has to run over the note, so the voice is live in it, not just allocated.
		renderBlocks(kRenderBlocksPerNote);
		const uint32_t samples = AudioEngine::audioSampleTimer - g_timerAtNoteOn;
		const auto voices = static_cast<int32_t>(g_sound->numActiveVoices());
		printf("SL-PROBE leg=%s voices=%d renderedSamples=%u\n", legName(g_leg), voices,
		       static_cast<unsigned>(samples));
		if (samples == 0 || voices == 0) {
			fail(samples == 0 ? "the render never ran over the note" : "the note ended before the deletion");
			g_stage = Stage::Summary;
			break;
		}
		g_stage = Stage::Delete;
		break;
	}
	case Stage::Delete: {
		g_before = Sound::soundsDestroyedWithLiveVoices();
		const std::terminate_handler previous = std::set_terminate(reportTerminatedDeletion);
		deleteLeg();
		std::set_terminate(previous);
		g_ticks = 0;
		g_stage = Stage::Settle;
		break;
	}
	case Stage::Settle:
		// A block per tick, so a destruction deferred to the render's next blocks has them.
		renderBlocks(1);
		if (++g_ticks < kSettleTicks) {
			break;
		}
		g_stage = Stage::Report;
		break;
	case Stage::Report:
		printf("SL-%s destroyedWithLiveVoices=%d\n", legName(g_leg), Sound::soundsDestroyedWithLiveVoices() - g_before);
		fflush(stdout);
		g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
		g_stage = Stage::Setup;
		break;
	case Stage::Summary:
		printf("SL-RESULT failures=%d\n", g_failures);
		// The Rust runner ends a failing run with process::exit, which does NOT flush C stdio.
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

void deluge_scenario_sound_lifetime_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Setup;
	g_leg = Leg::Synth;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_ticks = 0;
}

bool deluge_scenario_sound_lifetime_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_sound_lifetime_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// Nothing this tick queued runs anywhere else: the step is done when the tick returns.
	g_stepDone = true;
}

bool deluge_scenario_sound_lifetime_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_sound_lifetime_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
