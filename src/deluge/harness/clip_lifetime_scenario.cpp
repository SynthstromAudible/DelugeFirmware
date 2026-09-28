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
#include "gui/context_menu/clear_song.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "io/file.hpp"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "libdeluge/app.h"
#include "libdeluge/types.h"
#include "memory/fast_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/render_domain.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/preset_acquisition.h"
#include "storage/storage_manager.h"
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <new>
#include <unistd.h>
#include <vector>

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
/// The most blocks rendered waiting for the render to action a timer tick of this playback past tick 0.
constexpr int32_t kMaxBlocksBefore = 400;
/// Blocks rendered after each of UNDODELETE's undo, redo and log discard (and ARRANGERDELETE's discard), inside one
/// step.
constexpr int32_t kUndoBlocks = 4;
/// A y beyond the grid: the new-clip gesture then appends the clip after every other session clip.
constexpr int32_t kAppendY = 1000;
/// The song SONGLOAD and SONGSWAP load: the song current at the leg's setup, written through the song save's own
/// serialiser (Song::writeToFile()) -- a fixture is never hand-written XML.
constexpr const char* kSongDir = "SONGS";
constexpr const char* kSongPath = "SONGS/CLLOAD.XML";
/// Stamped over the old song's swing after the fixture is written, so the loaded song can be told from the old one by
/// value: the new Song may land at the address the old one freed.
constexpr int8_t kSwingDecoy = 5;
/// The most steps a song leg waits for its clear or load to finish (and for the load browser's listing).
constexpr int32_t kMaxAwaitSteps = 20000;
/// Blocks rendered per step while an armed swap waits for its launch: the launch waits for the longest clip's loop,
/// some 700 blocks.
constexpr int32_t kSwapBlocksPerStep = 8;
/// Blocks rendered before CL-RECLAIM, one per step, so the render drains and the reclaim task runs between them.
constexpr int32_t kReclaimBlocks = 10;
/// BURSTSONG's song: this many outputs -- half synths, half kits of kBurstDrumsPerKit drums -- each with two session
/// clips. The kits make the let-go's cost show: every drum is a Sound in the registry.
constexpr int32_t kBurstOutputs = 20;
constexpr int32_t kBurstClipsPerOutput = 2;
constexpr int32_t kBurstDrumsPerKit = 32;
/// The MIDI note the song legs inject once their clear or load has returned.
constexpr uint8_t kMidiNote = 64;
constexpr uint8_t kMidiVelocity = 100;

/// Each leg is one real deletion path, run on a playing clip or a sounding Sound. The clip legs' risky ones -- a
/// dangling pointer the render reads on every tick -- run after the others; WONTSOUND runs last, after the first
/// CL-RECLAIM, and its own retired drum is drained by the second CL-RECLAIM printed once it is done. EPOCH0 runs at
/// boot, before the render has run (deluge_scenario_clip_lifetime_boot()), not from the step machine.
enum class Leg : uint8_t {
	Epoch0,
	SessionDelete,
	ArrangerDelete,
	SwapType,
	UndoDelete,
	ArrangerNoUndo,
	RecordSource,
	SyncScale,
	SaveKit,
	SongClear,
	SongLoad,
	SongSwap,
	BurstSong,
	ClearPlaying,
	WontSound,
	Done
};

enum class Stage : uint8_t { Setup, Before, Delete, Listing, Await, After, Undo, Report, Reclaim, Summary, Done };

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
/// The render's sample clock when the leg's playback was set up: a timer tick at or after it belongs to this playback.
uint32_t g_timerAtStart = 0;
/// SYNCSCALE: the song's tick magnitudes before the leg made its clip the sync-scaling clip, restored afterwards.
int32_t g_magnitudeBefore = 0;
int32_t g_magnitudeOffsetBefore = 0;
/// UNDODELETE: whether the undo put the clip back in the arranger (arrangementOnlyClips, with an instance).
bool g_restored = false;
/// The Sound the leg holds a note on: the clip's synth, or a kit leg's drum. Not read once the deletion has run.
Sound* g_sound = nullptr;
/// SAVEKIT and WONTSOUND: the leg's kit, and the drum it sounds (SAVEKIT's has no note row anywhere).
Kit* g_kit = nullptr;
SoundDrum* g_drum = nullptr;
/// SONGLOAD and SONGSWAP: the swing the fixture song was written with, which only a real load brings back.
int8_t g_fixtureSwing = 0;
/// Steps a song leg has waited for its listing, then its clear or load.
int32_t g_awaitSteps = 0;
/// Whether CL-RECLAIM has been printed this run.
bool g_reclaimDone = false;
/// Song legs: AudioEngine::retiredPending() just after the clear or load returned or began, when the stopped-clock
/// paths have already built the new song; Song::retiredPendingAtLastConstruction() once the leg's clear or load is
/// over; and AudioEngine::retireViolations() at the start of the deletion.
int32_t g_pendingAfterDelete = 0;
/// CLEARPLAYING: the temp-recorded Sample its audio clip holds. Compared by value only once the clear has run.
Sample* g_tempSample = nullptr;
/// BURSTSONG: AudioEngine::retireDrainMicrosMax() over the clear -- the longest block-top let-go.
uint32_t g_letGoMicros = 0;
int32_t g_pendingAtBuild = -1;
std::size_t g_violationsBefore = 0;

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
	case Leg::Epoch0:
		return "EPOCH0";
	case Leg::SessionDelete:
		return "SESSIONDELETE";
	case Leg::ArrangerDelete:
		return "ARRANGERDELETE";
	case Leg::SwapType:
		return "SWAPTYPE";
	case Leg::UndoDelete:
		return "UNDODELETE";
	case Leg::ArrangerNoUndo:
		return "ARRANGERNOUNDO";
	case Leg::RecordSource:
		return "RECORDSOURCE";
	case Leg::SyncScale:
		return "SYNCSCALE";
	case Leg::SaveKit:
		return "SAVEKIT";
	case Leg::SongClear:
		return "SONGCLEAR";
	case Leg::SongLoad:
		return "SONGLOAD";
	case Leg::SongSwap:
		return "SONGSWAP";
	case Leg::BurstSong:
		return "BURSTSONG";
	case Leg::ClearPlaying:
		return "CLEARPLAYING";
	case Leg::WontSound:
		return "WONTSOUND";
	case Leg::Done:
		break;
	}
	return "DONE";
}

/// The legs that delete a clip: their probe and dangling lines are about references to that clip.
bool isClipLeg(Leg leg) {
	return static_cast<uint8_t>(leg) >= static_cast<uint8_t>(Leg::SessionDelete)
	       && static_cast<uint8_t>(leg) <= static_cast<uint8_t>(Leg::SyncScale);
}

/// The legs that replace the whole song.
bool isSongLeg(Leg leg) {
	return leg == Leg::SongClear || leg == Leg::SongLoad || leg == Leg::SongSwap || leg == Leg::BurstSong
	       || leg == Leg::ClearPlaying;
}

/// The legs that clear the song through the context menu's Create New Song.
bool isClearLeg(Leg leg) {
	return leg == Leg::SongClear || leg == Leg::BurstSong || leg == Leg::ClearPlaying;
}

/// The legs that follow an external clock: Create New Song then keeps playback going (runAccept() ends only internal
/// and arrangement playback), so the clear swaps songs with the clock running.
bool legFollowsExternalClock(Leg leg) {
	return leg == Leg::ClearPlaying;
}

/// The stopped-clock song legs: the old song must be freed before the new one is built.
bool isStoppedSongLeg(Leg leg) {
	return leg == Leg::SongClear || leg == Leg::SongLoad || leg == Leg::BurstSong;
}

/// The legs that load the fixture song through the load browser.
bool isLoadLeg(Leg leg) {
	return leg == Leg::SongLoad || leg == Leg::SongSwap;
}

/// Whether the leg runs with the internal clock going. The stopped-clock song paths are the ones that destroy the old
/// song before the new one is built.
bool legPlays(Leg leg) {
	return !isStoppedSongLeg(leg) && !legFollowsExternalClock(leg);
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

/// A blank-synth drum, added to @p kit the way sound_lifetime_scenario.cpp builds one, with its ParamManager backed up
/// in the song.
SoundDrum* makeDrum(Kit* kit) {
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
	return drum;
}

/// A playing-ready session kit clip, made through the new-clip gesture from a kit of one blank-synth drum (handed over
/// as makeSynthClip() hands over its synth). The gesture gives the clip a note row for that drum, with no notes.
InstrumentClip* makeKitClip() {
	ParamManagerForTimeline kitParams;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	if (instrument == nullptr) {
		fail("no kit");
		return nullptr;
	}
	auto* kit = static_cast<Kit*>(instrument);
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(kit->toModControllable()), nullptr, &kitParams,
	                                true);
	SoundDrum* drum = makeDrum(kit);
	if (drum == nullptr) {
		return nullptr;
	}

	deluge::storage::AcquireResult acquired;
	acquired.instrument = kit;
	acquired.loadedFromFile = true;
	Clip* clip = sessionView.createNewInstrumentClipAcquired(OutputType::KIT, kAppendY, &acquired);
	if (clip == nullptr) {
		fail("the new-clip gesture made no kit clip");
		return nullptr;
	}
	clip->activeIfNoSolo = true;
	g_kit = kit;
	g_drum = drum;
	return static_cast<InstrumentClip*>(clip);
}

/// Hold a note on @p drum, the audition path's shape (no note row), with @p paramManager.
bool soundDrum(SoundDrum* drum, InstrumentClip* clip, ParamManager* paramManager) {
	if (paramManager == nullptr) {
		return fail("the drum has no ParamManager");
	}
	char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* ms =
	    setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, drum, clip, paramManager);
	drum->Sound::noteOn(ms, &drum->arpeggiator, kNote, zeroMPEValues);
	return true;
}

/// SAVEKIT's drum's ParamManager: it has no note row, so the one backed up in the song.
ParamManager* orphanParamManager() {
	return currentSong->getBackedUpParamManagerPreferablyWithClip(g_drum, nullptr);
}

/// Write the current song to kSongPath through the song save's own serialiser: Song::writeToFile() over a file the
/// storage manager created, as the preset fixtures elsewhere are written.
bool writeFixtureSong() {
	(void)deluge::io::mkdir(kSongDir);
	(void)deluge::io::unlink(kSongPath);
	g_fixtureSwing = currentSong->swingAmount;
	Error error =
	    StorageManager::createXMLFile(kSongPath, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return fail("the fixture song could not be created");
	}
	currentSong->writeToFile();
	error = GetSerializer().closeFileAfterWriting(kSongPath, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", nullptr);
	if (error != Error::NONE) {
		return fail("the fixture song could not be closed");
	}
	// The old song now differs from the file, so a real load can be told from none.
	currentSong->swingAmount = static_cast<int8_t>(g_fixtureSwing + kSwingDecoy);
	return true;
}

/// SAVEKIT's save: the song save's serialisation, into memory as SaveSongUI::serializeSongToBuffer() does it. Saving
/// a song (not a kit) is what reaches Kit::writeDataToFile()'s destroy of a drum no note row has. Returns the bytes.
int32_t serializeSong() {
	auto& writer = static_cast<FileWriter&>(smSerializer);
	writer.setMemoryBased();
	g_step = "writeToFile";
	currentSong->writeToFile();
	const int32_t written = writer.bytesWritten();
	writer.clearMemoryBased();
	GetSerializer().reset();
	return written;
}

/// A copy of @p source appended to the session, on the same output: the clip-copy gesture's clone
/// (SessionView::gridCloneClip()), placed after every other session clip.
Clip* cloneIntoSession(Clip* source) {
	char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* ms = setupModelStackWithSong(memory, currentSong)->addTimelineCounter(source);
	if (source->clone(ms, false) != Error::NONE) {
		fail("a clip could not be cloned");
		return nullptr;
	}
	Clip* clone = static_cast<Clip*>(ms->getTimelineCounter());
	if (!currentSong->sessionClips.insertClipAt(clone, static_cast<int32_t>(currentSong->sessionClips.size()))) {
		fail("no room for a cloned clip");
		return nullptr;
	}
	return clone;
}

/// BURSTSONG's song: @p first and kBurstOutputs - 1 more synth clips, each on a synth of its own, then a second clip
/// on each of those synths.
bool buildBurstSong(InstrumentClip* first) {
	std::vector<Clip*> originals{first};
	for (int32_t i = 1; i < kBurstOutputs; i++) {
		const bool kit = (i % 2) != 0;
		InstrumentClip* clip = kit ? makeKitClip() : makeSynthClip();
		if (clip == nullptr) {
			return false;
		}
		if (kit) {
			for (int32_t d = 1; d < kBurstDrumsPerKit; d++) {
				if (makeDrum(g_kit) == nullptr) {
					return false;
				}
			}
		}
		originals.push_back(clip);
	}
	// makeKitClip() points these at its kit; BURSTSONG's leg is about the synth clip it started from.
	g_kit = nullptr;
	g_drum = nullptr;
	for (Clip* original : originals) {
		for (int32_t copy = 1; copy < kBurstClipsPerOutput; copy++) {
			if (cloneIntoSession(original) == nullptr) {
				return false;
			}
		}
	}
	int32_t outputs = 0;
	for (Output* output = currentSong->firstOutput; output != nullptr; output = output->next) {
		outputs++;
	}
	const auto sounds = AudioEngine::sounds.walk([](const auto& all) { return static_cast<int32_t>(all.size()); });
	printf("CL-BURST outputs=%d clips=%d sounds=%d\n", outputs, static_cast<int32_t>(currentSong->sessionClips.size()),
	       sounds);
	fflush(stdout);
	return true;
}

/// CLEARPLAYING's audio clip: a new audio track's clip holding what a finished, unsaved recording leaves behind -- a
/// Sample listed in the audio file manager with a TEMP path (tempFilePathForRecording), held by the clip's
/// SampleHolder. Built the way SampleRecorder::setup() and its first card routine build one (placement-new, adopt,
/// a setup reason, a listed file path), with the setup reason then dropped as a finished recording drops its own.
/// No audio is recorded: the Sample has no clusters. What the leg needs is the ownership, not the audio.
bool makeTempRecordedAudioClip() {
	Clip* clip = sessionView.createNewAudioClipForHarness(kAppendY);
	if (clip == nullptr || clip->type != ClipType::AUDIO) {
		return fail("no audio clip");
	}
	void* memory = deluge::memory::alloc_external(sizeof(Sample), 16);
	if (memory == nullptr) {
		return fail("no memory for a Sample");
	}
	auto* sample = new (memory) Sample;
	audioFileManager.adoptAudioFileObject(sample);
	sample->addReason();
	if (sample->initialize(1) != Error::NONE) {
		return fail("the Sample could not be initialised");
	}
	sample->byteDepth = 3;
	sample->numChannels = 1;
	sample->lengthInSamples = 44100;
	sample->audioDataLengthBytes = 44100 * 3;
	sample->audioDataStartPosBytes = 44;
	sample->filePath = "SAMPLES/CLIPS/CLPLAY.WAV";
	sample->tempFilePathForRecording = "SAMPLES/CLIPS/TEMP/CLPLAY.WAV";
	if (!audioFileManager.audioFiles.insertElement(sample)) {
		return fail("the Sample could not be listed");
	}
	static_cast<AudioClip*>(clip)->sampleHolder.filePath = sample->filePath;
	static_cast<AudioClip*>(clip)->sampleHolder.setAudioFile(sample);
	sample->removeReason("CLPL"); // the recorder's own reason, gone once the recording finished
	g_tempSample = sample;
	return true;
}

/// Whether the audio file manager still lists @p file. Compared by value only.
bool audioFileListed(AudioFile const* file) {
	for (AudioFile const* listed : audioFileManager.audioFiles) {
		if (listed == file) {
			return true;
		}
	}
	return false;
}

/// Build the running leg's clips, with the clock stopped, and point the song's references at the victim.
bool setupLeg() {
	g_victim = nullptr;
	g_recorder = nullptr;
	g_restored = false;
	g_sound = nullptr;
	g_kit = nullptr;
	g_drum = nullptr;
	g_tempSample = nullptr;
	if (g_leg == Leg::SaveKit || g_leg == Leg::WontSound) {
		InstrumentClip* clip = makeKitClip();
		if (clip == nullptr) {
			return false;
		}
		g_victim = clip;
		if (g_leg == Leg::SaveKit) {
			// A second drum, added after the clip was made: no note row in any clip has it. Kit::writeDataToFile()
			// destroys exactly such a drum when the song is saved.
			SoundDrum* orphan = makeDrum(g_kit);
			if (orphan == nullptr) {
				return false;
			}
			if (currentSong->findNoteRowForDrum(g_kit, orphan) != nullptr) {
				return fail("the orphan drum has a note row");
			}
			g_drum = orphan;
		}
		else {
			// The drum's note row has no notes, so it will not sound again: deleteSoundsWhichWontSound() deletes the
			// drum if the drum itself is not rendering.
			NoteRow* row = clip->getNoteRowForDrum(g_drum);
			if (row == nullptr || !row->hasNoNotes()) {
				return fail("the kit clip has no empty note row for its drum");
			}
		}
		g_sound = g_drum;
		return true;
	}
	if (isLoadLeg(g_leg) && !writeFixtureSong()) {
		return false;
	}
	InstrumentClip* clip = makeSynthClip();
	if (clip == nullptr) {
		return false;
	}
	g_victim = clip;
	g_sound = static_cast<SoundInstrument*>(clip->output);
	switch (g_leg) {
	case Leg::SessionDelete:
	case Leg::SwapType:
		// The clip the user is on: its pad was pressed to select it before the delete or the type change.
		selectClip(clip);
		break;
	case Leg::ArrangerDelete:
	case Leg::UndoDelete:
	case Leg::ArrangerNoUndo: {
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
		// The user entered the clip from the arranger and came back out: it is still the current clip and the view's.
		// The arranger's own gesture is made from the arranger.
		selectClip(clip);
		changeRootUI(&arrangerView);
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
	case Leg::SongClear: {
		// The synth the user is playing in the old song, and a kit drum sounding beside it: the clear must take the
		// drum out of its kit's rendering list before the kit is destroyed (Kit::~Kit freezes E462 otherwise).
		selectClip(clip);
		InstrumentClip* kitClip = makeKitClip();
		if (kitClip == nullptr) {
			return false;
		}
		SoundDrum* drum = g_drum;
		NoteRow* row = kitClip->getNoteRowForDrum(drum);
		// The kit is part of the old song's fixture only: the leg's probes and reports stay about the synth.
		g_kit = nullptr;
		g_drum = nullptr;
		if (row == nullptr || !soundDrum(drum, kitClip, &row->paramManager)) {
			return fail("the old song's kit drum could not be sounded");
		}
		break;
	}
	case Leg::SongLoad:
	case Leg::SongSwap:
		// The synth the user is playing in the old song.
		selectClip(clip);
		break;
	case Leg::BurstSong:
		if (!buildBurstSong(clip)) {
			return false;
		}
		selectClip(clip);
		break;
	case Leg::ClearPlaying:
		if (!makeTempRecordedAudioClip()) {
			return false;
		}
		selectClip(clip);
		break;
	case Leg::Epoch0:
	case Leg::SaveKit:
	case Leg::WontSound:
	case Leg::Done:
		break;
	}
	return true;
}

/// Hold the leg's note: on the clip's synth, or on the kit leg's drum.
bool soundLeg() {
	switch (g_leg) {
	case Leg::SaveKit:
		return soundDrum(g_drum, static_cast<InstrumentClip*>(g_victim), orphanParamManager());
	case Leg::WontSound:
		// Left silent (see deleteLeg()'s Leg::WontSound case for why): a real noteOn/off here would clear itself out
		// of renderingDrums at the very moment skippingRendering flips back true
		// (SoundDrum::setSkippingRendering()), leaving nothing for the delete branch below to actually exercise.
		return true;
	default:
		holdNote(static_cast<InstrumentClip*>(g_victim));
		return true;
	}
}

[[noreturn]] void noteTerminatedDeletion() {
	g_terminated = true;
	std::abort();
}

/// Installed around a deletion and the blocks after it, for SIGABRT and SIGSEGV. Every way a deletion (or a render
/// reading what it left dangling) can end the process arrives here: a firmware freeze (FREEZE_WITH_ERROR aborts on the
/// host BSP), a std::terminate (via the handler above) and a fault. The leg still reports its counts, marked aborted=1
/// with the step it died in, and the run its result, before the process dies with the signal it was going to die with.
/// @note Not async-signal-safe: snprintf and fflush may run here while the signal interrupted stdio or the allocator.
///       Accepted for a host-only diagnostic that is about to die anyway, as in sound_lifetime_scenario.cpp; the line
///       goes out through write(), which is.
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

/// The arranger's delete gesture on @p clip's one instance: ArrangerView::deleteClipInstance(output, clipInstance) and
/// the four-argument overload it calls, step for step (both are private). Skipped: the gesture's UI-only steps --
/// clearing lastInteractedClipInstance (private; MIDI-follow feedback), the OLED clear and exitSubModeWithoutAction().
/// With @p logged, the action is a real one, so Song::deletingClipInstanceForClip() takes its logged branch: the
/// clip-existence consequence detaches the clip and keeps it for the undo, and the clip is destroyed only when the log
/// is discarded. Without it, the gesture runs as it does when ActionLogger::getNewAction() has no action to give (no
/// memory for undo history): the action is null, and deletingClipInstanceForClip() deletes the clip at once
/// (deleteClipObject()) -- the one arranger path that leaves the song's currentClip on the deleted clip.
void arrangerDeleteGesture(Clip* clip, bool logged) {
	Output* output = clip->output;
	int32_t index = -1;
	for (int32_t i = 0; i < std::ssize(output->clipInstances); i++) {
		if (output->clipInstances[i].clip == clip) {
			index = i;
			break;
		}
	}
	if (index < 0) {
		fail("the clip has no instance in the arranger");
		return;
	}
	ClipInstance* clipInstance = &output->clipInstances[index];

	g_step = "setActiveModControllableTimelineCounter";
	view.setActiveModControllableTimelineCounter(currentSong);
	arrangement.rowEdited(output, clipInstance->pos, clipInstance->pos + clipInstance->length, clipInstance->clip,
	                      nullptr);
	g_step = "getNewAction";
	Action* action =
	    logged ? actionLogger.getNewAction(ActionType::CLIP_INSTANCE_EDIT, ActionAddition::NOT_ALLOWED) : nullptr;
	if (logged && action == nullptr) {
		// deletingClipInstanceForClip() would then take its no-undo-memory branch, which ARRANGERNOUNDO drives.
		fail("no action for the arranger delete");
		return;
	}

	// ArrangerView::deleteClipInstance(output, index, clipInstance, action, false):
	if (action != nullptr) {
		g_step = "recordClipInstanceExistenceChange";
		action->recordClipInstanceExistenceChange(output, clipInstance, ExistenceChangeType::DELETE);
	}
	output->clipInstances.erase(output->clipInstances.begin() + index);
	g_step = "deletingClipInstanceForClip";
	currentSong->deletingClipInstanceForClip(output, clip, action, true);
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
	case Leg::ArrangerDelete:
	case Leg::UndoDelete:
		arrangerDeleteGesture(clip, true);
		break;
	case Leg::ArrangerNoUndo:
		arrangerDeleteGesture(clip, false);
		break;
	case Leg::SwapType:
		// The session view's change-to-audio gesture: Song::replaceInstrumentClipWithAudioClip() -> swapClips(), then
		// the view is pointed at the new clip -- after the old one is gone.
		g_step = "replaceInstrumentClipWithAudioClip";
		sessionView.replaceInstrumentClipWithAudioClip(clip);
		break;
	case Leg::SyncScale:
		// SessionView::removeClip() refuses the sync-scaling clip; removeSessionClip() itself does not.
		clip->stopAllNotesPlaying(currentSong);
		g_step = "removeSessionClip";
		currentSong->removeSessionClip(clip, currentSong->sessionClips.getIndexForClip(clip));
		break;
	case Leg::SaveKit: {
		// The kit's render kills the voices of a drum its clip has no row for, so the note held since the setup is
		// gone by now -- but the drum stays in the set the render walks. Said before the save, which may end the
		// process.
		printf("CL-SAVE leg=SAVEKIT voicesAtSave=%d renderingActive=%d\n",
		       static_cast<int32_t>(g_drum->numActiveVoices()), g_kit->renderingDrums.contains(g_drum) ? 1 : 0);
		fflush(stdout);
		const int32_t bytes = serializeSong();
		printf("CL-SAVE leg=SAVEKIT bytes=%d\n", bytes);
		fflush(stdout);
		break;
	}
	case Leg::SongClear:
	case Leg::BurstSong:
	case Leg::ClearPlaying:
		// The context menu's accept: ClearSongContextMenu::runAccept(), which with the clock stopped goes through
		// deleteOldSongBeforeLoadingNew() and then builds the new song; with an external clock running it arms the
		// swap, builds the new song, swaps it in and hands the old one over, then frees the temp-recorded Samples.
		g_step = "clearSong";
		(void)deluge::gui::context_menu::clearSong.acceptCurrentOption();
		break;
	case Leg::SongLoad:
	case Leg::SongSwap:
		// The load browser on the fixture song, as song_load_channel_scenario.cpp opens it. The load itself begins
		// once its listing has landed (Stage::Listing).
		g_step = "openLoadSongUI";
		currentSong->setSongFullPath(kSongPath);
		(void)openUI(&loadSongUI);
		break;
	case Leg::Epoch0:
		break;
	case Leg::WontSound:
		// The drum's note row has no notes, so it will not sound again -- but a real note would clear itself out of
		// renderingDrums at the same moment skippingRendering flips back true
		// (SoundDrum::setSkippingRendering() is the only writer of the flag), so a genuinely-played drum would
		// already be out of the set by the time it gets here. Put the drum back in the set by hand instead: skipping
		// rendering, yet still in renderingDrums, is a state no natural note-on/off path reaches at this
		// call site, so this does not reproduce a real precondition -- it proves only that
		// deleteSoundsWhichWontSound() hands a SoundDrum over through AudioEngine::retire() correctly (no E321, the
		// drum actually gone, reclaimed by the end of the run). The site's real safety value is on release builds:
		// the hasActiveVoices() check below is ALPHA_OR_BETA_VERSION-only, and retire()'s let-go now kills a drum's
		// voices unconditionally before it is freed, whichever build this runs on. No render block runs between the
		// insert and the call that reaches it: Kit::renderGlobalEffectableForClip's E254 freezes a set member that
		// still reads skippingRendering.
		g_kit->renderingDrums.add(g_drum);
		g_step = "songSwapAboutToHappen";
		AudioEngine::songSwapAboutToHappen();
		break;
	case Leg::Done:
		break;
	}
}

/// SAVEKIT and WONTSOUND, just after the deletion: whether the kit still lists the drum, and whether the drum is still
/// in the set of drums the kit's render walks. Compared by value only: the drum may be freed.
void printDrumDangling() {
	bool inKit = false;
	for (Drum const* drum = g_kit->firstDrum; drum != nullptr; drum = drum->next) {
		inKit = inKit || drum == g_drum;
	}
	printf("CL-DANGLING leg=%s inKit=%d renderingActive=%d\n", legName(g_leg), inKit ? 1 : 0,
	       g_kit->renderingDrums.contains(g_drum) ? 1 : 0);
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

/// A song leg's clean-up: the old song is gone, and with it every pointer the leg held (the new song may have been
/// allocated where they pointed, so none is compared). Stop the clock and point the UI at the new song's first clip.
void cleanUpSongLeg() {
	g_victim = nullptr;
	g_recorder = nullptr;
	g_sound = nullptr;
	if (playbackHandler.isEitherClockActive()) {
		playbackHandler.endPlayback();
	}
	if (isLoadLeg(g_leg)) {
		// The image is this run's own, but a later load leg writes the same path.
		(void)deluge::io::unlink(kSongPath);
	}
	changeRootUI(&sessionView);
	g_home = (currentSong != nullptr && !currentSong->sessionClips.empty()) ? currentSong->sessionClips[0] : nullptr;
	if (g_home == nullptr) {
		fail("the new song has no clip");
		return;
	}
	selectClip(g_home);
}

/// Put back what the leg left pointing at a clip that may be gone, stop the clock, and point the UI home.
void cleanUpLeg() {
	if (isSongLeg(g_leg)) {
		cleanUpSongLeg();
		return;
	}
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
	}
	if (g_leg == Leg::SyncScale) {
		// Whether the leg or the render dropped the sync-scaling clip, the tick scale it set is still in place.
		currentSong->insideWorldTickMagnitude = g_magnitudeBefore;
		currentSong->insideWorldTickMagnitudeOffsetFromBPM = g_magnitudeOffsetBefore;
	}
	if (g_recorder != nullptr && g_recorder->beingRecordedFromClip == victim) {
		g_recorder->beingRecordedFromClip = nullptr;
	}
	if (playbackHandler.isEitherClockActive()) {
		playbackHandler.endPlayback();
	}
	changeRootUI(&sessionView);
	selectClip(g_home);
	g_victim = nullptr;
	g_recorder = nullptr;
	g_sound = nullptr;
}

/// Whether the arranger holds the victim again: in arrangementOnlyClips, with an instance on its output.
bool victimRestoredToArranger() {
	if (currentSong->arrangementOnlyClips.getIndexForClip(g_victim) < 0) {
		return false;
	}
	for (ClipInstance const& instance : g_victim->output->clipInstances) {
		if (instance.clip == g_victim) {
			return true;
		}
	}
	return false;
}

/// Discard the logs, saying first whether the undo log still held an action -- the logged delete that keeps the clip,
/// and whose discard destroys it.
void discardLogs() {
	printf("CL-DISCARD leg=%s held=%d\n", legName(g_leg), actionLogger.firstAction[BEFORE] != nullptr ? 1 : 0);
	fflush(stdout);
	g_step = "deleteAllLogs";
	actionLogger.deleteAllLogs();
	renderBlocks(kUndoBlocks);
}

/// ARRANGERDELETE's second half, in one step: discard the logs, which is when the logged delete actually destroys the
/// clip (ConsequenceClipExistence::prepareForDestruction()).
void runDiscard() {
	discardLogs();
}

/// UNDODELETE's second half, in one step: undo the logged arranger delete, redo it, then discard the logs -- which
/// destroys the clip the log was keeping (ConsequenceClipExistence::prepareForDestruction()).
void runUndo() {
	g_step = "undo";
	actionLogger.undo();
	g_restored = victimRestoredToArranger();
	// Said now as well as in the leg's line: the redo or the log discard below may end the process.
	printf("CL-UNDO leg=%s restored=%d\n", legName(g_leg), g_restored ? 1 : 0);
	fflush(stdout);
	renderBlocks(kUndoBlocks);
	g_step = "redo";
	actionLogger.redo();
	renderBlocks(kUndoBlocks);
	discardLogs();
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

/// Whether the running song leg's clear or load is over. A clear is over once its UI-mode gate is released and the new
/// song's own preset request has landed; a load once LoadSongUI says its load has run to completion.
bool songLegOver() {
	if (isClearLeg(g_leg)) {
		const bool pending = currentSong != nullptr && currentSong->blankPresetPending;
		return currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES && !pending
		       && !deluge::gui::instrumentLoad.inFlight();
	}
	return !loadSongUI.loadInFlight();
}

/// A stopped-clock song leg, once its clear or load has returned: what the rest of the interaction tier does next. A UI
/// timer fires (the play-enable flash, which walks the current song's session clips -- the timer LoadSongUI unsets
/// before it deletes the old song), then a MIDI note arrives on the DIN port and is let go of. A regression guard for
/// "the clear or load left currentSong null or pointing at the old song" -- it runs after the operation returns. It is
/// not evidence about the wait inside it: that no interaction task runs while currentSong is null holds by
/// construction (awaitRetired() runs no scheduler; on this harness the render step runs inline), and nothing here can
/// observe that window.
void runInteractionAfterLoad() {
	g_step = "uiTimer";
	uiTimerManager.setTimer(TimerName::PLAY_ENABLE_FLASH, 0);
	renderBlocks(1); // the timer fires once the render's sample clock has passed its trigger time
	uiTimerManager.routine();
	g_step = "midiNoteIn";
	midiEngine.midiMessageReceived(MIDIDeviceManager::dinMIDIPorts, 0x09, 0, kMidiNote, kMidiVelocity);
	midiEngine.midiMessageReceived(MIDIDeviceManager::dinMIDIPorts, 0x08, 0, kMidiNote, kMidiVelocity);
	uiTimerManager.unsetTimer(TimerName::PLAY_ENABLE_FLASH);
	printf("CL-AFTER leg=%s uiTimer=1 midiNoteIn=1 song=%d\n", legName(g_leg), currentSong != nullptr ? 1 : 0);
	fflush(stdout);
}

/// CL-RECLAIM, before WONTSOUND (which may end the process): render some blocks, so the render lets go of everything
/// retired and the reclaim task runs between them, then reclaim and say what is still waiting.
void runReclaim() {
	renderBlocks(1);
	if (++g_ticks < kReclaimBlocks) {
		return;
	}
	AudioEngine::reclaimRetired();
	printf("CL-RECLAIM pending=%u\n", static_cast<unsigned>(AudioEngine::retiredPending()));
	fflush(stdout);
	g_reclaimDone = true;
	g_stage = Stage::Setup;
}

void runTick() {
	switch (g_stage) {
	case Stage::Setup:
		if (g_leg == Leg::Done) {
			g_stage = Stage::Summary;
			break;
		}
		if (g_leg == Leg::WontSound && !g_reclaimDone) {
			g_ticks = 0;
			g_stage = Stage::Reclaim;
			break;
		}
		if (!setupLeg()) {
			g_stage = Stage::Summary;
			break;
		}
		if (legPlays(g_leg)) {
			// Playback on the internal clock, with no count-in: every active clip starts from the top.
			playbackHandler.setupPlaybackUsingInternalClock(0, false);
		}
		else if (legFollowsExternalClock(g_leg)) {
			// Following a MIDI clock: playback runs, though no clock message ever arrives to tick it.
			playbackHandler.setupPlaybackUsingExternalClock();
		}
		g_timerAtStart = AudioEngine::audioSampleTimer;
		if (!soundLeg()) {
			g_stage = Stage::Summary;
			break;
		}
		g_ticks = 0;
		g_stage = Stage::Before;
		break;
	case Stage::Before: {
		renderBlocks(1);
		// The internal clock's first tick lands some ten thousand samples after playback is set up, so the leg
		// renders until the render has actioned a tick of this playback, not just a few blocks. lastTimerTickActioned
		// keeps the previous playback's value until that first tick, so a tick is told by its time instead. A
		// stopped-clock leg renders its blocks and no more.
		const bool plays = legPlays(g_leg);
		const bool ticked =
		    plays
		    && static_cast<int32_t>(static_cast<uint32_t>(playbackHandler.timeLastTimerTickBig >> 32) - g_timerAtStart)
		           >= 0;
		const auto ticks = ticked ? static_cast<int32_t>(playbackHandler.lastTimerTickActioned) : 0;
		// Past this playback's tick 0, so the clock has moved on from where it started.
		if (++g_ticks < kBlocksBefore || (plays && ticks <= 0 && g_ticks < kMaxBlocksBefore)) {
			break;
		}
		const bool playing = playbackHandler.isEitherClockActive() && currentSong->isClipActive(g_victim);
		const auto voices = static_cast<int32_t>(g_sound->numActiveVoices());
		if (isClipLeg(g_leg)) {
			printf("CL-PROBE leg=%s playing=%d ticks=%d voices=%d current=%d sync=%d view=%d recordSource=%d\n",
			       legName(g_leg), playing ? 1 : 0, ticks, voices, currentSong->getCurrentClip() == g_victim ? 1 : 0,
			       currentSong->syncScalingClip == g_victim ? 1 : 0,
			       view.activeModControllableModelStack.getTimelineCounterAllowNull() == g_victim ? 1 : 0,
			       (g_recorder != nullptr && g_recorder->beingRecordedFromClip == g_victim) ? 1 : 0);
		}
		else if (g_kit != nullptr) {
			printf("CL-PROBE leg=%s playing=%d ticks=%d voices=%d renderingActive=%d\n", legName(g_leg),
			       playing ? 1 : 0, ticks, voices, g_kit->renderingDrums.contains(g_drum) ? 1 : 0);
		}
		else {
			// view=1: the view's active timeline counter is the old song's synth clip, which the armed swap leaves in
			// place until the old song is gone (LoadSongUI::runBeginSegment() says so).
			printf("CL-PROBE leg=%s playing=%d ticks=%d voices=%d view=%d\n", legName(g_leg), playing ? 1 : 0, ticks,
			       voices, view.activeModControllableModelStack.getTimelineCounterAllowNull() == g_victim ? 1 : 0);
		}
		if (plays && ticks <= 0) {
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
		g_violationsBefore = AudioEngine::retireViolations();
		g_pendingAtBuild = -1;
		g_step = nullptr;
		// The handlers stay in until the report: what the deletion leaves dangling is read by the blocks after it.
		installHandlers();
		(void)AudioEngine::retireDrainMicrosMax(true);
		deleteLeg();
		// A clear has built the new song by now; a load has only opened its browser.
		g_pendingAfterDelete = static_cast<int32_t>(AudioEngine::retiredPending());
		g_letGoMicros = AudioEngine::retireDrainMicrosMax(false);
		if (g_leg == Leg::BurstSong) {
			// The whole song's let-go runs in one block's top: its host wall time, the longest such step in the clear.
			printf("CL-LETGO leg=BURSTSONG letGoMicros=%u\n", static_cast<unsigned>(g_letGoMicros));
			fflush(stdout);
		}
		if (g_leg == Leg::ClearPlaying) {
			// runAccept() has returned, so deleteAnyTempRecordedSamplesFromMemory() has run. It frees the Sample the
			// old song's audio clip holds a reason to: the old song must be gone by then (pending=0), or its
			// ~SampleHolder later writes into the freed Sample.
			printf("CL-CLEARSAMPLE leg=CLEARPLAYING sampleFreed=%d pendingAtFree=%d\n",
			       audioFileListed(g_tempSample) ? 0 : 1, g_pendingAfterDelete);
			fflush(stdout);
		}
		g_step = nullptr;
		g_ticks = 0;
		g_awaitSteps = 0;
		if (g_leg == Leg::ArrangerNoUndo || g_leg == Leg::SessionDelete) {
			// Before any block: what the interaction tier itself can still read of the retired clip. Its handlers run
			// between the retire and the render's let-go, so these must already be clear.
			printf("CL-WINDOW leg=%s currentClip=%d view=%d\n", legName(g_leg),
			       currentSong->getCurrentClip() == g_victim ? 1 : 0,
			       view.activeModControllableModelStack.getTimelineCounterAllowNull() == g_victim ? 1 : 0);
		}
		if (isClipLeg(g_leg)) {
			printDangling();
		}
		else if (g_leg == Leg::SaveKit || g_leg == Leg::WontSound) {
			printDrumDangling();
		}
		g_stage = isLoadLeg(g_leg) ? Stage::Listing : isClearLeg(g_leg) ? Stage::Await : Stage::After;
		break;
	case Stage::Listing:
		// The load browser's own listing lands first, as a LOAD press waits for it. Nothing else ticks a browser in
		// the headless harness.
		g_step = "listing";
		if (legPlays(g_leg)) {
			renderBlocks(1);
		}
		if (Browser::isListingInProgress()) {
			loadSongUI.graphicsRoutine();
			if (++g_awaitSteps > kMaxAwaitSteps) {
				fail("the load browser's listing never finished");
				g_stage = Stage::After;
			}
			break;
		}
		g_step = "beginLoad";
		loadSongUI.beginLoad();
		g_awaitSteps = 0;
		g_stage = Stage::Await;
		break;
	case Stage::Await: {
		// The clear or load runs on its own, a tick at a time; an armed swap launches in the render, at the end of the
		// longest clip's loop, and LoadSongUI then destroys the old song (runFinishSegment()).
		g_step = "await";
		renderBlocks(isLoadLeg(g_leg) && legPlays(g_leg) ? kSwapBlocksPerStep : 1);
		const bool over = songLegOver();
		if (!over && ++g_awaitSteps < kMaxAwaitSteps) {
			break;
		}
		if (!over) {
			fail("the clear or load never finished");
		}
		if (isLoadLeg(g_leg)) {
			const bool loaded = currentSong != nullptr && currentSong->swingAmount == g_fixtureSwing;
			printf("CL-LOADED leg=%s loaded=%d steps=%d\n", legName(g_leg), loaded ? 1 : 0, g_awaitSteps);
		}
		else {
			printf("CL-CLEARED leg=%s clips=%d steps=%d\n", legName(g_leg),
			       currentSong != nullptr ? static_cast<int32_t>(currentSong->sessionClips.size()) : -1, g_awaitSteps);
		}
		fflush(stdout);
		if (isStoppedSongLeg(g_leg)) {
			// The clear or load built exactly one Song, after it let go of the old one. A load's retired objects are
			// counted now that it is over; a clear's were counted when it returned.
			g_pendingAtBuild = Song::retiredPendingAtLastConstruction();
			if (isLoadLeg(g_leg)) {
				g_pendingAfterDelete = static_cast<int32_t>(AudioEngine::retiredPending());
			}
			runInteractionAfterLoad();
		}
		g_ticks = 0;
		g_stage = Stage::After;
		break;
	}
	case Stage::After:
		g_step = "render";
		renderBlocks(1);
		if (++g_ticks < kBlocksAfter) {
			break;
		}
		g_stage = (g_leg == Leg::UndoDelete || g_leg == Leg::ArrangerDelete) ? Stage::Undo : Stage::Report;
		break;
	case Stage::Undo:
		if (g_leg == Leg::UndoDelete) {
			runUndo();
		}
		else {
			runDiscard();
		}
		g_stage = Stage::Report;
		break;
	case Stage::Report: {
		g_step = "report";
		if (isClipLeg(g_leg)) {
			// A retired clip is freed only by a reclaim after the render let go of it and two blocks began: render
			// those blocks and reclaim here, so the leg's own clip is destroyed -- and ~Clip()'s check runs -- inside
			// the leg rather than at whatever reclaim comes next.
			g_step = "reclaim";
			renderBlocks(kUndoBlocks);
			AudioEngine::reclaimRetired();
			printf("CL-FREED leg=%s pending=%u\n", legName(g_leg),
			       static_cast<unsigned>(AudioEngine::retiredPending()));
			g_step = "report";
		}
		const int32_t clips = Clip::clipsDestroyedWhileReferenced() - g_clipsBefore;
		const int32_t songs = Song::songsDestroyedWhileReachable() - g_songsBefore;
		const int32_t sounds = Sound::soundsDestroyedWithLiveVoices() - g_soundsBefore;
		if (g_leg == Leg::UndoDelete) {
			printf("CL-%s restored=%d clipsReferenced=%d songsReachable=%d soundsLive=%d\n", legName(g_leg),
			       g_restored ? 1 : 0, clips, songs, sounds);
		}
		else if (isStoppedSongLeg(g_leg)) {
			printf(
			    "CL-%s clipsReferenced=%d songsReachable=%d soundsLive=%d pendingAtBuild=%d pending=%d violations=%d\n",
			    legName(g_leg), clips, songs, sounds, g_pendingAtBuild, g_pendingAfterDelete,
			    static_cast<int32_t>(AudioEngine::retireViolations() - g_violationsBefore));
		}
		else if (g_leg == Leg::SongSwap) {
			printf("CL-%s clipsReferenced=%d songsReachable=%d soundsLive=%d violations=%d\n", legName(g_leg), clips,
			       songs, sounds, static_cast<int32_t>(AudioEngine::retireViolations() - g_violationsBefore));
		}
		else if (g_leg == Leg::ClearPlaying) {
			printf("CL-%s clipsReferenced=%d songsReachable=%d soundsLive=%d pendingAtFree=%d violations=%d\n",
			       legName(g_leg), clips, songs, sounds, g_pendingAfterDelete,
			       static_cast<int32_t>(AudioEngine::retireViolations() - g_violationsBefore));
		}
		else {
			printf("CL-%s clipsReferenced=%d songsReachable=%d soundsLive=%d\n", legName(g_leg), clips, songs, sounds);
		}
		fflush(stdout);
		if (g_leg == Leg::WontSound) {
			// The gate's last leg: render the retired drum's block-and-a-half wait out and reclaim, so this run's own
			// CL-RECLAIM (the required one -- see scripts/clip_lifetime.sh) reflects what WONTSOUND itself retired,
			// not just what the earlier clip legs left behind.
			g_step = "reclaim2";
			renderBlocks(kReclaimBlocks);
			AudioEngine::reclaimRetired();
			printf("CL-RECLAIM pending=%u\n", static_cast<unsigned>(AudioEngine::retiredPending()));
			fflush(stdout);
			g_step = "report";
		}
		g_step = "cleanUp";
		cleanUpLeg();
		restoreHandlers();
		g_step = nullptr;
		g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
		g_stage = (g_failures > 0) ? Stage::Summary : Stage::Setup;
		break;
	}
	case Stage::Reclaim:
		runReclaim();
		break;
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
		return Leg::Epoch0;
	}
	for (auto leg = static_cast<uint8_t>(Leg::Epoch0); leg < static_cast<uint8_t>(Leg::Done); leg++) {
		if (std::strcmp(from, legName(static_cast<Leg>(leg))) == 0) {
			return static_cast<Leg>(leg);
		}
	}
	return Leg::Done;
}

/// EPOCH0: Create New Song at boot, before the scheduler has run the render task once. epoch= is how many blocks the
/// render has begun: 1, the block boot itself renders inline from setupBlankSong() (setUIForLoadedSong() -> the
/// audio routine, before the render task exists), which is also where the first Song appears -- so epoch 0 with a song
/// to clear is never reachable. With nothing scheduling the render, the clear's bounded wait for the old song to be
/// freed must drive the render itself and return, with nothing left waiting, rather than hang.
void runEpoch0() {
	g_leg = Leg::Epoch0;
	const auto epoch = static_cast<unsigned long long>(AudioEngine::renderDomain().audio_epoch());
	g_clipsBefore = Clip::clipsDestroyedWhileReferenced();
	g_songsBefore = Song::songsDestroyedWhileReachable();
	g_soundsBefore = Sound::soundsDestroyedWithLiveVoices();
	g_violationsBefore = AudioEngine::retireViolations();
	installHandlers();
	g_step = "clearSong";
	(void)deluge::gui::context_menu::clearSong.acceptCurrentOption();
	g_step = nullptr;
	printf("CL-EPOCH0 clipsReferenced=%d songsReachable=%d soundsLive=%d epoch=%llu returned=1 pendingAtBuild=%d "
	       "pending=%u violations=%d\n",
	       Clip::clipsDestroyedWhileReferenced() - g_clipsBefore, Song::songsDestroyedWhileReachable() - g_songsBefore,
	       Sound::soundsDestroyedWithLiveVoices() - g_soundsBefore, epoch, Song::retiredPendingAtLastConstruction(),
	       static_cast<unsigned>(AudioEngine::retiredPending()),
	       static_cast<int32_t>(AudioEngine::retireViolations() - g_violationsBefore));
	fflush(stdout);
	restoreHandlers();
}

} // namespace

extern "C" {

void deluge_scenario_clip_lifetime_boot(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	if (firstLeg() == Leg::Epoch0) {
		runEpoch0();
	}
}

void deluge_scenario_clip_lifetime_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Setup;
	g_leg = firstLeg();
	if (g_leg == Leg::Epoch0) {
		// Ran at boot (deluge_scenario_clip_lifetime_boot()).
		g_leg = Leg::SessionDelete;
	}
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_ticks = 0;
	g_reclaimDone = false;
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
