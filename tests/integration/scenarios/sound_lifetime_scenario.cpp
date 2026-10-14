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

#include "scenarios/sound_lifetime_scenario.h"

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
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>
#include <string_view>
#include <unistd.h>

// The firmware headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded `printf_`. This
// harness links the Rust BSP, which has no `putchar_` provider, and every line this file writes IS the gate's output.
#ifdef printf
#undef printf
#endif

namespace {

/// The note every leg holds. A blank synth sustains it until a note-off, which no leg sends.
constexpr int32_t kNote = 60;
/// The render block the scenario drives, and how many it renders with each note held before deleting it. Nothing
/// else renders here: the golden harness skips the audio task's own render once boot has finished.
constexpr uint32_t kRenderBlockFrames = 128;
constexpr int32_t kRenderBlocksPerNote = 4;
/// How many steps a leg waits after its deletion before it reads the count, so any deferred destruction has run.
constexpr int32_t kSettleTicks = 10;

/// Each leg is one real deletion path, run on a Sound that is sounding when it is deleted.
/// SYNTH..DELETEDRUM delete a sounding Sound through a real path; OPPORTUNISTIC and BURST drive the retirement itself
/// with blank drums, each in a single step.
enum class Leg : uint8_t { Synth, Unedited, Kit, DeleteDrum, Opportunistic, Burst, Done };

enum class Stage : uint8_t { Setup, Sounding, Delete, Settle, Report, Reclaim, Summary, Done };

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
/// The clip the leg's instrument plays from (never in the song; see attachClip()).
InstrumentClip* g_clip = nullptr;
/// The Sound the leg holds a note on, for the checks: the synth, or the drum.
Sound* g_sound = nullptr;
/// Whether the leg's output is in the song. The render walks only the song's outputs, so only these legs' voices are
/// actually rendered; the others' voices are live but never rendered.
bool g_inSong = false;
/// The render's sample clock when the note went on: the leg deletes only once it has moved.
uint32_t g_timerAtNoteOn = 0;
/// The counter before the deletion.
int32_t g_before = 0;
/// Which step of the deletion is running, for the abort report: a deletion that aborts has no other way to say where.
const char* volatile g_step = nullptr;
/// The handlers the deletion's own replaced, restored at the report.
std::terminate_handler g_previousTerminate = nullptr;
void (*g_previousAbort)(int) = SIG_DFL;
/// Set by the terminate handler, so the abort report can tell a std::terminate from a firmware freeze.
volatile bool g_terminated = false;
/// DELETEDRUM: whether the drum was in its kit's renderingDrums when the deletion began. Kit::drumRemoved()
/// freezes with E321 on exactly that, on an ALPHA_OR_BETA_VERSION build (the host's).
bool g_renderingActiveAtDelete = false;

const char* legName(Leg leg) {
	switch (leg) {
	case Leg::Synth:
		return "SYNTH";
	case Leg::Unedited:
		return "UNEDITED";
	case Leg::Kit:
		return "KIT";
	case Leg::DeleteDrum:
		return "DELETEDRUM";
	case Leg::Opportunistic:
		return "OPPORTUNISTIC";
	case Leg::Burst:
		return "BURST";
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
	currentSong->live.param_backup.backUp(static_cast<ModControllableAudio*>(synth->toModControllable()), nullptr,
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
	currentSong->live.param_backup.backUp(static_cast<ModControllableAudio*>(kit->toModControllable()), nullptr,
	                                      &kitParams, true);

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
	currentSong->live.param_backup.backUp(drum, nullptr, &paramManager, true);
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
	auto* clip = new (memory) InstrumentClip(*currentSong);
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
	g_clip = nullptr;
	g_sound = nullptr;
	g_inSong = false;
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
		g_clip = clip;
		if (g_leg == Leg::Synth) {
			currentSong->addOutput(g_synth);
			g_inSong = true;
		}
		else if (g_synth->editedByUser) {
			// deleteOrAddToHibernationListOutput() would hibernate it, and nothing would be destroyed.
			return fail("a new synth reads as edited by the user");
		}
		g_sound = g_synth;
		return sound(*g_synth, g_synth, &g_synth->arpeggiator, clip, &clip->paramManager);
	}
	case Leg::Kit:
	case Leg::DeleteDrum: {
		g_kit = makeKitWithSynthDrum(&g_drum);
		if (g_kit == nullptr) {
			return false;
		}
		InstrumentClip* clip = attachClip(g_kit);
		if (clip == nullptr) {
			return false;
		}
		g_clip = clip;
		// Both kit legs' kits are in the song: the clip view deletes a drum of the kit on screen.
		currentSong->addOutput(g_kit);
		g_inSong = true;
		NoteRow* row = clip->getNoteRowForDrum(g_drum);
		if (row == nullptr) {
			return fail("the kit's clip has no row for the drum");
		}
		g_sound = g_drum;
		return sound(*g_drum, g_drum, &g_drum->arpeggiator, clip, &row->paramManager);
	}
	case Leg::Opportunistic:
	case Leg::Burst:
	case Leg::Done:
		break;
	}
	return true;
}

/// Installed around a deletion. std::terminate (a pure virtual call from a voice unassigned into a half-destroyed
/// Sound) is recorded and turned into the abort the report below catches.
[[noreturn]] void noteTerminatedDeletion() {
	g_terminated = true;
	std::abort();
}

/// Installed around a deletion as the SIGABRT handler. Both ways a deletion can end the process arrive here: a firmware
/// freeze (FREEZE_WITH_ERROR, which on the host BSP aborts) and std::terminate (via the handler above). The leg still
/// reports its count and the run its result before the process dies with the abort it was going to die with. The
/// count is taken in ~Sound's body, before its members -- the voices -- are destroyed, so it is already in.
void reportAbortedDeletion(int /*signal*/) {
	const char* leg = legName(g_leg);
	const char* step = (g_step != nullptr) ? g_step : "?";
	const char* why = g_terminated ? "std::terminate (a pure virtual call) inside the deletion"
	                  : (g_step != nullptr && g_renderingActiveAtDelete && std::string_view{g_step} == "removeDrum")
	                      ? "a freeze in Kit::removeDrum with the drum still in renderingDrums (E321)"
	                      : "an abort inside the deletion";
	char line[512];
	int n = snprintf(line, sizeof(line),
	                 "SL-%s destroyedWithLiveVoices=%d aborted=1 step=%s\nSL-%s FAIL the process ended with %s\n"
	                 "SL-RESULT failures=%d\n",
	                 leg, Sound::soundsDestroyedWithLiveVoices() - g_before, step, leg, why, g_failures + 1);
	fflush(stdout);
	if (n > 0) {
		(void)write(STDOUT_FILENO, line, static_cast<size_t>(std::min<int>(n, sizeof(line) - 1)));
	}
	std::signal(SIGABRT, SIG_DFL);
	std::raise(SIGABRT);
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
	case Leg::DeleteDrum: {
		// InstrumentClipView::deleteDrum(), step for step, with the leg's clip in place of the current clip. It is the
		// only drum-delete site that can destroy a sounding drum: the preset-replace site kills the drum's voices
		// first (wontBeRenderedForAWhile()), and the folder-import, slicer and instrument-load sites only drop drums
		// that were never in a kit.
		int32_t noteRowIndex = 0;
		NoteRow* noteRow = g_clip->getNoteRowForDrum(g_drum, &noteRowIndex);
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, g_clip);

		// Sampled before the erase, so a deleteDrum that stopped erasing (and froze in removeDrum) is named E321.
		g_renderingActiveAtDelete = g_kit->renderingDrums.contains(g_drum);
		g_step = "stopNote";
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    (noteRow != nullptr) ? modelStack->addNoteRow(noteRowIndex, noteRow) : nullptr;
		if (noteRow != nullptr) {
			noteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow);
		}
		g_kit->renderingDrums.remove(g_drum);

		g_step = "removeDrum";
		g_kit->removeDrum(g_drum);

		g_step = "noteRow";
		if (noteRow != nullptr) {
			noteRow->setDrum(g_kit->getFirstUnassignedDrum(g_clip), g_kit, modelStackWithNoteRow);
			if (noteRow->drum == nullptr && noteRow->hasNoNotes() && g_clip->getNumNoteRows() > 1) {
				if (noteRowIndex == 0) {
					g_clip->yScroll--;
				}
				g_clip->deleteNoteRow(modelStack, noteRowIndex);
			}
		}

		g_step = "retire";
		currentSong->live.param_backup.deleteForModControllable(g_drum);
		g_kit->renderingDrums.remove(g_drum);
		g_drum->kit = nullptr;
		AudioEngine::retire(g_drum);
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
		g_step = nullptr;
		break;
	}
	case Leg::Opportunistic:
	case Leg::Burst:
	case Leg::Done:
		break;
	}
	g_synth = nullptr;
	g_kit = nullptr;
	g_drum = nullptr;
	g_clip = nullptr;
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

/// A blank drum in no kit, the way the folder-import and slicer sites build one: registered in the Sound registry by
/// its constructor, never sounded.
SoundDrum* makeBlankDrum() {
	void* memory = deluge::memory::alloc_fast(sizeof(SoundDrum));
	if (memory == nullptr) {
		fail("no memory for a blank drum");
		return nullptr;
	}
	return new (memory) SoundDrum();
}

/// Blocks OPPORTUNISTIC renders between its two retires: enough for the render to let go of the first drum and for
/// two epochs to begin after its stamp.
constexpr int32_t kOpportunisticBlocks = 3;
/// Drums BURST retires in one step: more than the retire ring's 64 slots, so the overflow list is used and the
/// render's 16-per-block drain takes several blocks.
constexpr int32_t kBurstDrums = 80;
/// Blocks BURST renders, reclaiming after each, before it gives up waiting for everything to be freed.
constexpr int32_t kBurstMaxBlocks = 32;

/// OPPORTUNISTIC, in one step: retire A, render until the render has let go of it and two blocks have begun, retire
/// B. B's retire must free A on its own -- no reclaim-task tick runs inside a step -- so exactly B is left pending.
void runOpportunisticLeg() {
	SoundDrum* a = makeBlankDrum();
	SoundDrum* b = makeBlankDrum();
	if (a == nullptr || b == nullptr) {
		return;
	}
	const std::size_t before = AudioEngine::retiredPending();
	AudioEngine::retire(a);
	renderBlocks(kOpportunisticBlocks);
	AudioEngine::retire(b);
	const std::size_t pending = AudioEngine::retiredPending() - before;
	printf("SL-OPPORTUNISTIC pending=%u\n", static_cast<unsigned>(pending));
	if (pending != 1) {
		g_failures++;
	}
}

/// BURST, in one step: retire more drums than the ring holds, then render and reclaim block by block. Every drum must
/// be let go of by the render before it is destroyed (violations counts destroys that found the drum still in the
/// Sound registry), and all must be freed.
void runBurstLeg() {
	const std::size_t violationsBefore = AudioEngine::retireViolations();
	for (int32_t i = 0; i < kBurstDrums; i++) {
		SoundDrum* drum = makeBlankDrum();
		if (drum == nullptr) {
			return;
		}
		AudioEngine::retire(drum);
	}
	int32_t blocks = 0;
	// Everything still retired -- the burst, and whatever OPPORTUNISTIC left -- must come free.
	while (AudioEngine::retiredPending() > 0 && blocks < kBurstMaxBlocks) {
		renderBlocks(1);
		AudioEngine::reclaimRetired();
		blocks++;
	}
	const std::size_t violations = AudioEngine::retireViolations() - violationsBefore;
	const std::size_t pending = AudioEngine::retiredPending();
	printf("SL-BURST drums=%d blocks=%d violations=%u pending=%u\n", kBurstDrums, blocks,
	       static_cast<unsigned>(violations), static_cast<unsigned>(pending));
	if (violations != 0 || pending != 0) {
		g_failures++;
	}
}

void runTick() {
	switch (g_stage) {
	case Stage::Setup:
		if (g_leg == Leg::Opportunistic || g_leg == Leg::Burst) {
			if (g_leg == Leg::Opportunistic) {
				runOpportunisticLeg();
			}
			else {
				runBurstLeg();
			}
			g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
			break;
		}
		if (g_leg == Leg::Done) {
			g_ticks = 0;
			g_stage = Stage::Reclaim;
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
		// Blocks of render with the note held. The render reaches the voice only when the leg's output is in the song
		// (inSong=1); otherwise the voice is live but never rendered. clockSamples is how far the render's clock
		// moved, not a claim that this voice was rendered.
		renderBlocks(kRenderBlocksPerNote);
		const uint32_t samples = AudioEngine::audioSampleTimer - g_timerAtNoteOn;
		const auto voices = static_cast<int32_t>(g_sound->numActiveVoices());
		printf("SL-PROBE leg=%s voices=%d inSong=%d clockSamples=%u\n", legName(g_leg), voices, g_inSong ? 1 : 0,
		       static_cast<unsigned>(samples));
		if (samples == 0 || voices == 0) {
			fail(samples == 0 ? "the render's clock never moved" : "the note ended before the deletion");
			g_stage = Stage::Summary;
			break;
		}
		g_stage = Stage::Delete;
		break;
	}
	case Stage::Delete: {
		g_before = Sound::soundsDestroyedWithLiveVoices();
		g_step = nullptr;
		g_terminated = false;
		// The handlers stay in until the report: the deletion hands the leg's objects to the render, and they are
		// destroyed during the settle, two blocks later.
		g_previousTerminate = std::set_terminate(noteTerminatedDeletion);
		g_previousAbort = std::signal(SIGABRT, reportAbortedDeletion);
		deleteLeg();
		g_ticks = 0;
		g_stage = Stage::Settle;
		break;
	}
	case Stage::Settle:
		// A block per tick, so a destruction deferred to the render's next blocks has them.
		g_step = "settle";
		renderBlocks(1);
		if (++g_ticks < kSettleTicks) {
			break;
		}
		g_stage = Stage::Report;
		break;
	case Stage::Report:
		std::signal(SIGABRT, g_previousAbort);
		std::set_terminate(g_previousTerminate);
		g_step = nullptr;
		printf("SL-%s destroyedWithLiveVoices=%d\n", legName(g_leg), Sound::soundsDestroyedWithLiveVoices() - g_before);
		fflush(stdout);
		if (AudioEngine::retiredPending() != 0) {
			// The count covers only what was destroyed: an object still waiting would not be in it.
			fail("the deletion's objects were still waiting to be destroyed at the report");
		}
		g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
		g_stage = Stage::Setup;
		break;
	case Stage::Reclaim: {
		// Every leg's deletion handed its objects to the render, which frees them two blocks later: once the
		// render has run a few more blocks (and the app loop's reclaim job with them), nothing may be pending.
		renderBlocks(1);
		if (++g_ticks < kSettleTicks) {
			break;
		}
		const std::size_t pending = AudioEngine::retiredPending();
		printf("SL-RECLAIM pending=%u\n", static_cast<unsigned>(pending));
		if (pending != 0) {
			g_failures++;
		}
		g_stage = Stage::Summary;
		break;
	}
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
