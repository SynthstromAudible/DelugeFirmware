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

#include "harness/sample_load_tree_scenario.h"

#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/qwerty_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/save/save_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/session_view.h"
#include "harness/preset_fixture_loader.h"
#include "io/file.hpp"
#include "libdeluge/app.h"
#include "libdeluge/audio_load.h"
#include "libdeluge/file_bytes.h"
#include "libdeluge/types.h"
#include "memory/fast_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/async_sample_load.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/audio/load_trace.h"
#include "storage/audio/sample_load_walk.h"
#include "storage/byte_channel.h"
#include "storage/multi_range/multi_range.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases printf to an implementation this harness has no
// `putchar_` for -- every line below would become an undefined symbol at link time.
#ifdef printf
#undef printf
#endif

/// Reaches the protected ENTER gesture and `collectingSamples`, so the fixture song is saved the way a user saves it.
class SampleLoadTreeProbe {
public:
	static bool openSongSave(const char* dir) {
		Browser::currentDir = dir;
		Browser::enteredText.clear();
		return openUI(&saveSongUI);
	}
	/// Tick the browser just opened while its listing runs, and say whether it still is. Nothing else in the headless
	/// harness ticks a browser.
	static bool listingInProgress() {
		if (Browser::isListingInProgress()) {
			getCurrentUI()->graphicsRoutine();
		}
		return Browser::isListingInProgress();
	}
	static void typeName(const char* name) { Browser::enteredText = name; }
	static void collectMedia() { saveSongUI.collectingSamples = true; }
	static void pressEnter() { saveSongUI.enterKeyPress(); }
};

namespace {

constexpr const char* kSongDir = "SONGS";
constexpr const char* kSongName = "SLTSONG1";
constexpr const char* kSongPath = "SONGS/SLTSONG1.XML";
/// Where a collect-media save puts the song's samples, under their flattened names.
constexpr const char* kSongFolder = "SONGS/SLTSONG1";

constexpr const char* kSynthSample = "SAMPLES/SLTSYN.WAV";
constexpr const char* kWaveTable = "SAMPLES/SLTWT.WAV";
constexpr const char* kKitSample = "SAMPLES/SLTKIT.WAV";
constexpr const char* kClipSample = "SAMPLES/SLTAUD.WAV";
constexpr const char* kKitDrumName = "SLTD1";
/// The kit preset the new-kit-track gesture picks up for the song: one drum, SLTD1, on kKitSample.
constexpr const char* kSongKitPath = "KITS/SLTSKIT.XML";

constexpr const char* kSynthPresetDir = "SYNTHS";
constexpr const char* kSynthPresetName = "SLTPSYN";
constexpr const char* kSynthPresetPath = "SYNTHS/SLTPSYN.XML";
/// Named in the preset under SAMPLES/, but on the card only in the preset's own folder, SYNTHS/SLTPSYN/.
constexpr const char* kSynthPresetSample = "SAMPLES/SLTPSYN.WAV";
constexpr const char* kSynthPresetResolved = "SYNTHS/SLTPSYN/SLTPSYN.WAV";

constexpr const char* kKitPresetDir = "KITS";
constexpr const char* kKitPresetName = "SLTPKIT";
constexpr const char* kKitPresetPath = "KITS/SLTPKIT.XML";
constexpr const char* kKitPresetSampleA = "SAMPLES/SLTPKA.WAV";
constexpr const char* kKitPresetResolvedA = "KITS/SLTPKIT/SLTPKA.WAV";
constexpr const char* kKitPresetSampleB = "SAMPLES/SLTPKB.WAV";

// The crucial-only load's song, SLTSONG2: SLTSONG1's synth, kit and audio clip (all playing), plus a second kit whose
// three drums cover the kit filter, a second synth whose clip is not playing, and a second audio clip that is not
// playing but is still its track's active clip.
constexpr const char* kCrucialSongName = "SLTSONG2";
constexpr const char* kCrucialSongPath = "SONGS/SLTSONG2.XML";
constexpr const char* kCrucialSongFolder = "SONGS/SLTSONG2";
/// The kit preset the new-kit-track gesture picks up for the second kit: it sorts before every other kit preset.
constexpr const char* kCrucialKitPath = "KITS/SLTCKIT.XML";
constexpr const char* kCrucialDrumA = "SLTCDA"; ///< Its row has notes and is not muted.
constexpr const char* kCrucialDrumB = "SLTCDB"; ///< Its row has notes and is muted.
constexpr const char* kCrucialDrumC = "SLTCDC"; ///< Its row has no notes.
constexpr const char* kCrucialKitSampleA = "SAMPLES/SLTCKA.WAV";
constexpr const char* kCrucialKitSampleB = "SAMPLES/SLTCKB.WAV";
constexpr const char* kCrucialKitSampleC = "SAMPLES/SLTCKC.WAV";
constexpr const char* kCrucialSynthSample = "SAMPLES/SLTCSB.WAV";
constexpr const char* kCrucialClipSample = "SAMPLES/SLTCAB.WAV";

/// How long the crucial load may take, armed swap included. Probe 1 measured it finishing well inside this.
constexpr int32_t kMaxCrucialWaitTicks = 20000;
/// Audio rendered per scenario tick while the crucial load waits for its armed swap. This harness renders nothing on
/// its own after boot, so without it the playback clock never reaches the swap.
constexpr int32_t kRenderBlocksPerTick = 4;
constexpr uint32_t kRenderBlockFrames = 128;

constexpr int32_t kMaxWaitTicks = 200000;

/// routineWithClusterLoading calls each load makes, measured on the fixture: the cadence that keeps audio running
/// while a load walks the tree. Pinned so a walk that drops or adds a call is seen.
///
/// The song and crucial counts each dropped by three when song load's essential pass started ticking (Phase 4d).
/// Those calls existed because a blocking walk holding the storage owner was the only thing that could run the
/// audio engine; a ticked walk returns to the run loop between walks, which runs it anyway. The three that
/// remain belong to the passes still walking synchronously.
constexpr int32_t kSongRoutines = 42;
constexpr int32_t kSynthRoutines = 2;
constexpr int32_t kKitRoutines = 3;
constexpr int32_t kCrucialRoutines = 99;

/// Milliseconds the loader holds each load during an async leg, so each load spans several scenario ticks.
constexpr uint32_t kAsyncLatencyMs = 20;
/// Scenario ticks an async leg may take before it is called wedged. Probe 1 measured every leg well inside this.
constexpr int32_t kMaxAsyncTicks = 4000;
/// Synchronous wavetable lookups each async leg makes, in leg order (crucial, song, synth, kit, race, card-gone, stall,
/// probe-orphan): one per wavetable holder its walk reaches. Measured, and pinned.
constexpr std::array<int32_t, 8> kAsyncWavetableReads{1, 1, 0, 0, 1, 1, 0, 1};
/// Milliseconds the byte channel holds each operation in the probe-race leg: long enough that every sample request of
/// the song comes back while the folder's exists probe is still out.
constexpr uint32_t kRaceProbeLatencyMs = 600;
/// Milliseconds the byte channel holds each operation in the card-gone leg, so the probe is still out at the eject.
constexpr uint32_t kCardGoneProbeLatencyMs = 400;
/// Milliseconds the loader holds each load in the stall leg: longer than AsyncSampleLoad::kMaxStalledTicks scenario
/// ticks, so the first load stalls, and over well before a second load's bound runs out.
constexpr uint32_t kStallHoldMs = 1600;

enum class Stage : uint8_t {
	SongKit,
	Build,
	SaveOpen,
	SaveOpenWait,
	Save,
	SaveWait,
	Release,
	LoadOpen,
	LoadListing,
	LoadWait,
	SongCheck,
	Presets,
	SynthPreset,
	KitPreset,
	CrucialKit,
	CrucialBuild,
	CrucialSaveOpen,
	CrucialSaveOpenWait,
	CrucialSave,
	CrucialSaveWait,
	CrucialRelease,
	CrucialPlay,
	CrucialLoadOpen,
	CrucialListing,
	CrucialWait,
	CrucialCheck,
	AsyncReference,
	AsyncRelease,
	AsyncBegin,
	AsyncWait,
	AsyncCheck,
	Report,
	Done,
};

Stage g_stage = Stage::SongKit;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_waitTicks = 0;
int32_t g_songRoutines = -1;
int32_t g_synthRoutines = -1;
int32_t g_kitRoutines = -1;
int32_t g_crucialRoutines = -1;
bool g_crucialClockAtLoad = false;
int32_t g_crucialRendered = 0;
int32_t g_crucialSwapTick = -1;

// ── The load trace ───────────────────────────────────────────────────────────

const char* g_traceLabel = "";
/// "LT" for the song and preset windows, whose lines are compared with the saved baseline; "LTC" for the crucial load,
/// whose lines go to a file of their own.
const char* g_tracePrefix = "LT";
int32_t g_traceRoutines = 0;
/// Raise the abort the first time a holder is bound with a file, so a walk that checks for it stops after one load.
bool g_abortArmed = false;
bool g_abortRaised = false;

void snapshotCrucial();

/// While the probe-race and card-gone legs run, only the folder probe's trace lines are printed.
bool g_probeLinesOnly = false;
/// Lookups that waited for the folder probe ("probe wait"), since the last reset.
int32_t g_probeWaits = 0;
/// Set to catch the next "probe collect" line's answer in g_firstProbeAnswer (a bytes::ByteIo value; -1 for none).
bool g_firstProbeAnswerArmed = false;
int32_t g_firstProbeAnswer = -1;

void noteProbeLine(const char* line) {
	if (std::strncmp(line, "probe wait ", 11) == 0) {
		g_probeWaits++;
	}
	const char* answer = std::strstr(line, " answer=");
	if (g_firstProbeAnswerArmed && std::strncmp(line, "probe collect ", 14) == 0 && answer != nullptr) {
		g_firstProbeAnswer = std::atoi(answer + std::strlen(" answer="));
		g_firstProbeAnswerArmed = false;
	}
}

void traceSink(const char* line) {
	const bool probeLine = (std::strncmp(line, "probe ", 6) == 0);
	if (probeLine) {
		noteProbeLine(line);
	}
	if (g_probeLinesOnly && !probeLine) {
		return;
	}
	if (std::strcmp(line, "routine") == 0) {
		g_traceRoutines++;
	}
	printf("%s %s %s\n", g_tracePrefix, g_traceLabel, line);
	// LoadSongUI traces "crucial end" right after the crucial-only pass, before any later pass can bind anything.
	if (std::strcmp(line, "crucial end") == 0) {
		snapshotCrucial();
	}
	if (g_abortArmed && !g_abortRaised && std::strncmp(line, "bind ", 5) == 0 && std::strstr(line, " file=1")) {
		QwertyUI::predictionInterrupted = true;
		g_abortRaised = true;
	}
}

void openTrace(const char* label, const char* prefix = "LT") {
	g_tracePrefix = prefix;
	g_traceLabel = label;
	g_traceRoutines = 0;
	(void)deluge::storage::load_trace::setSink(&traceSink);
	printf("%s %s begin\n", prefix, label);
}

int32_t closeTrace() {
	(void)deluge::storage::load_trace::setSink(nullptr);
	printf("%s %s end routines=%d\n", g_tracePrefix, g_traceLabel, g_traceRoutines);
	return g_traceRoutines;
}

// ── Fixture construction ─────────────────────────────────────────────────────

AudioFileHolder* firstHolder(Source& source) {
	MultiRange* range = source.getOrCreateFirstRange();
	return (range != nullptr) ? range->getAudioFileHolder() : nullptr;
}

bool fail(const char* what) {
	printf("SLT-SETUP FAIL %s\n", what);
	g_failures++;
	return false;
}

/// A sample drum, the way the slicer builds one, added to @p kit with its ParamManager backed up under @p clip.
SoundDrum* addSampleDrum(Kit& kit, Clip* clip, const char* name, const char* samplePath, bool load) {
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
	drum->drumName = name;
	Sound::initParams(&paramManager);
	kit.addDrum(drum);
	drum->setupAsSample(&paramManager);
	drum->sources[0].setOscType(OscType::SAMPLE);
	AudioFileHolder* holder = firstHolder(drum->sources[0]);
	if (holder == nullptr) {
		fail("a drum has no sample range");
		return nullptr;
	}
	holder->filePath = samplePath;
	if (load) {
		(void)holder->loadFile(false, false, true);
	}
	currentSong->backUpParamManager(drum, clip, &paramManager, true);
	return drum;
}

/// The song: the boot synth as a sample source and a wavetable source, a kit clip with one sample drum, and an audio
/// clip. Every holder is loaded, so a collect-media save copies every file.
bool buildSong() {
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
	synth->sources[1].setOscType(OscType::WAVETABLE);
	AudioFileHolder* sampleHolder = firstHolder(synth->sources[0]);
	AudioFileHolder* waveTableHolder = firstHolder(synth->sources[1]);
	if (sampleHolder == nullptr || waveTableHolder == nullptr) {
		return fail("the synth's sources have no ranges");
	}
	sampleHolder->filePath = kSynthSample;
	waveTableHolder->filePath = kWaveTable;
	(void)synth->loadAllAudioFiles(true);

	// The new-kit-track gesture, which takes the first kit preset nobody in the song is using: kSongKitPath.
	Clip* kitClipBase = deluge::harness::createNewInstrumentClip(OutputType::KIT, 1);
	if (kitClipBase == nullptr || kitClipBase->output == nullptr || kitClipBase->output->type != OutputType::KIT) {
		return fail("no kit clip could be created from the song kit preset");
	}
	AudioFileHolder* kitHolder = nullptr;
	for (Drum* drum = static_cast<Kit*>(kitClipBase->output)->firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->type == DrumType::SOUND && static_cast<SoundDrum*>(drum)->drumName == kKitDrumName) {
			kitHolder = firstHolder(static_cast<SoundDrum*>(drum)->sources[0]);
		}
	}
	if (kitHolder == nullptr) {
		return fail("the kit clip has no SLTD1 drum");
	}

	Clip* audioClipBase = sessionView.createNewClip(OutputType::AUDIO, 2);
	if (audioClipBase == nullptr || audioClipBase->type != ClipType::AUDIO) {
		return fail("no audio clip could be created");
	}
	auto* audioClip = static_cast<AudioClip*>(audioClipBase);
	audioClip->sampleHolder.filePath = kClipSample;
	audioClip->loadSample(true);

	printf("SLT-SETUP built synth=%d wavetable=%d kitDrum=%d audioClip=%d\n",
	       sampleHolder->audioFile != nullptr ? 1 : 0, waveTableHolder->audioFile != nullptr ? 1 : 0,
	       kitHolder->audioFile != nullptr ? 1 : 0, audioClip->sampleHolder.audioFile != nullptr ? 1 : 0);
	return true;
}

/// Drop every holder's file except the audio clip's, delete them from memory and remove their originals from the
/// card, so the song load can only find them among the collected copies. The audio clip's file stays resident and
/// is claimed by the load's memory-only pass.
void releaseSongFiles() {
	for (Output* output = currentSong->firstOutput; output != nullptr; output = output->next) {
		if (output->type == OutputType::SYNTH) {
			auto* synth = static_cast<SoundInstrument*>(output);
			for (int32_t s = 0; s < kNumSources; s++) {
				for (int32_t r = 0; r < std::ssize(synth->sources[s].ranges); r++) {
					synth->sources[s].ranges.getElement(r)->getAudioFileHolder()->setAudioFile(nullptr);
				}
			}
		}
		else if (output->type == OutputType::KIT) {
			for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->type != DrumType::SOUND) {
					continue;
				}
				auto* soundDrum = static_cast<SoundDrum*>(drum);
				for (int32_t r = 0; r < std::ssize(soundDrum->sources[0].ranges); r++) {
					soundDrum->sources[0].ranges.getElement(r)->getAudioFileHolder()->setAudioFile(nullptr);
				}
			}
		}
	}
	for (const char* path : {kSynthSample, kWaveTable, kKitSample}) {
		if (!audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path)) {
			printf("SLT-SETUP FAIL %s is still referenced and could not be released\n", path);
			g_failures++;
		}
		(void)deluge::io::unlink(path);
	}
}

/// Write a preset through the firmware serialiser, as writeBlankSynthPresetFile in the song-preview scenario does.
bool writeInstrumentDocument(Instrument& instrument, const char* path, const char* closingTag) {
	Error error = StorageManager::createXMLFile(path, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return fail("a preset file could not be created");
	}
	instrument.writeToFile(nullptr, currentSong);
	error = GetSerializer().closeFileAfterWriting(path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", closingTag);
	return error == Error::NONE || fail("a preset file could not be closed");
}

/// A kit preset with one sample drum per entry of @p drums (name, sample path), written through the firmware
/// serialiser. The kit is leaked, as the song-preview scenario's instruments are.
bool writeKitPreset(const char* path, std::initializer_list<std::pair<const char*, const char*>> drums) {
	ParamManagerForTimeline kitParams;
	Instrument* kitBase = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	if (kitBase == nullptr) {
		return fail("no kit for a preset");
	}
	auto* kit = static_cast<Kit*>(kitBase);
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(kit->toModControllable()), nullptr, &kitParams,
	                                true);
	for (const auto& [name, sample] : drums) {
		if (addSampleDrum(*kit, nullptr, name, sample, /*load=*/false) == nullptr) {
			return false;
		}
	}
	return writeInstrumentDocument(*kit, path, "\n</kit>\n");
}

/// A synth preset with one sample source, and a kit preset with two sample drums. The instruments are leaked, as the
/// song-preview scenario's are: they are never added to the song, so nothing else scans them.
bool writePresets() {
	ParamManagerForTimeline synthParams;
	Instrument* synthBase = StorageManager::createNewInstrument(OutputType::SYNTH, &synthParams);
	if (synthBase == nullptr) {
		return fail("no synth for the preset");
	}
	auto* synth = static_cast<SoundInstrument*>(synthBase);
	synth->setupAsSample(&synthParams);
	synth->sources[0].setOscType(OscType::SAMPLE);
	AudioFileHolder* holder = firstHolder(synth->sources[0]);
	if (holder == nullptr) {
		return fail("the preset synth has no sample range");
	}
	holder->filePath = kSynthPresetSample;
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(synth->toModControllable()), nullptr,
	                                &synthParams, true);
	if (!writeInstrumentDocument(*synth, kSynthPresetPath, "\n</sound>\n")) {
		return false;
	}

	return writeKitPreset(kKitPresetPath, {{"SLTPKA", kKitPresetSampleA}, {"SLTPKB", kKitPresetSampleB}});
}

// ── Holder outcomes ──────────────────────────────────────────────────────────

/// Records every demand a walk reports, and loads nothing: a root's holders, in load order.
class DemandCollector final : public deluge::storage::SampleLoadVisitor {
public:
	std::vector<deluge::storage::SampleDemand> demands;

	deluge::storage::Walk rangeCheckpoint(int32_t /*rangeIndex*/) override { return deluge::storage::Walk::Continue; }
	deluge::storage::Walk drumCheckpoint() override { return deluge::storage::Walk::Continue; }
	void clipCheckpoint() override {}
	void beginInstrumentScope(std::string_view /*dirPath*/, std::string_view /*name*/, ThingType /*thing*/,
	                          deluge::storage::ScopeRule /*rule*/) override {}
	void endInstrumentScope() override {}
	void demand(const deluge::storage::SampleDemand& demand) override { demands.push_back(demand); }
};

std::vector<deluge::storage::SampleDemand> songDemands(Song& song) {
	DemandCollector collector;
	song.walkSampleDemands(collector);
	return std::move(collector.demands);
}

std::vector<deluge::storage::SampleDemand> outputDemands(Output& output) {
	DemandCollector collector;
	(void)output.walkSampleDemands(collector);
	return std::move(collector.demands);
}

/// What a load left in one holder: the path it names, whether a file is bound, where that file was read from, and,
/// for an audio clip, the clip's name.
struct HolderOutcome {
	std::string path;
	bool bound = false;
	std::string alt;
	std::string clipName;

	bool operator==(const HolderOutcome&) const = default;
};

std::vector<HolderOutcome> snapshot(const std::vector<deluge::storage::SampleDemand>& demands) {
	std::vector<HolderOutcome> outcomes;
	outcomes.reserve(demands.size());
	for (const deluge::storage::SampleDemand& demand : demands) {
		const AudioFile* file = demand.holder->audioFile;
		outcomes.push_back({.path = demand.holder->filePath,
		                    .bound = file != nullptr,
		                    .alt = (file != nullptr) ? file->loadedFromAlternatePath : std::string{},
		                    .clipName = (demand.clipToName != nullptr) ? demand.clipToName->name : std::string{}});
	}
	return outcomes;
}

void unbindAll(const std::vector<deluge::storage::SampleDemand>& demands) {
	for (const deluge::storage::SampleDemand& demand : demands) {
		demand.holder->setAudioFile(nullptr);
	}
}

/// Each preset as its blocking load left it, and the instrument it was loaded into.
std::vector<HolderOutcome> g_refSynth;
std::vector<HolderOutcome> g_refKit;
Instrument* g_refSynthInstrument = nullptr;
Instrument* g_refKitInstrument = nullptr;

// ── Checks ───────────────────────────────────────────────────────────────────

void reportHolder(const char* load, const char* owner, AudioFileHolder* holder, const char* wantPath, bool wantBound,
                  const char* wantAlt) {
	const AudioFile* file = holder->audioFile;
	const char* alt = (file != nullptr) ? file->loadedFromAlternatePath.c_str() : "";
	printf("SLT-HOLDER load=%s owner=%s path=%s bound=%d alt=%s\n", load, owner, holder->filePath.c_str(),
	       file != nullptr ? 1 : 0, alt);
	if (wantPath != nullptr
	    && (holder->filePath != wantPath || (file != nullptr) != wantBound || std::strcmp(alt, wantAlt) != 0)) {
		printf("SLT-HOLDER FAIL load=%s owner=%s wanted path=%s bound=%d alt=%s\n", load, owner, wantPath,
		       wantBound ? 1 : 0, wantAlt);
		g_failures++;
	}
}

std::string collected(const char* samplePath) {
	return std::string(kSongFolder) + "/" + (samplePath + std::strlen("SAMPLES/"));
}

void checkSong() {
	bool sawSynth = false;
	bool sawKit = false;
	bool sawClip = false;
	for (Output* output = currentSong->firstOutput; output != nullptr; output = output->next) {
		if (output->type == OutputType::SYNTH) {
			auto* synth = static_cast<SoundInstrument*>(output);
			if (synth->sources[0].oscType != OscType::SAMPLE) {
				continue;
			}
			sawSynth = true;
			const std::string sampleAlt = collected(kSynthSample);
			const std::string waveAlt = collected(kWaveTable);
			reportHolder("song", "synth-sample", firstHolder(synth->sources[0]), kSynthSample, true, sampleAlt.c_str());
			reportHolder("song", "synth-wavetable", firstHolder(synth->sources[1]), kWaveTable, true, waveAlt.c_str());
		}
		else if (output->type == OutputType::KIT) {
			for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->type != DrumType::SOUND) {
					continue;
				}
				auto* soundDrum = static_cast<SoundDrum*>(drum);
				const bool fixture = soundDrum->drumName == kKitDrumName;
				sawKit = sawKit || fixture;
				const std::string alt = collected(kKitSample);
				reportHolder("song", fixture ? "kit-drum" : "kit-other", firstHolder(soundDrum->sources[0]),
				             fixture ? kKitSample : nullptr, true, alt.c_str());
			}
		}
	}
	for (int32_t c = 0; c < std::ssize(currentSong->sessionClips); c++) {
		Clip* clip = currentSong->sessionClips[c];
		if (clip->type != ClipType::AUDIO) {
			continue;
		}
		auto* audioClip = static_cast<AudioClip*>(clip);
		sawClip = true;
		reportHolder("song", "audio-clip", &audioClip->sampleHolder, kClipSample, true, "");
		printf("SLT-CLIP name=%s\n", audioClip->name.c_str());
		if (audioClip->name != kClipSample) {
			printf("SLT-CLIP FAIL the audio clip's name is not its sample's path\n");
			g_failures++;
		}
	}
	printf("SLT-SONG synth=%d kit=%d clip=%d routines=%d\n", sawSynth ? 1 : 0, sawKit ? 1 : 0, sawClip ? 1 : 0,
	       g_songRoutines);
	if (!sawSynth || !sawKit || !sawClip) {
		printf("SLT-SONG FAIL the loaded song is missing a fixture part\n");
		g_failures++;
	}
}

void loadSynthPreset() {
	std::string name = kSynthPresetName;
	std::string dir = kSynthPresetDir;
	Instrument* instrument = nullptr;
	openTrace("synth");
	Error error =
	    deluge::harness::loadPresetFixture(currentSong, OutputType::SYNTH, &instrument, kSynthPresetPath, &name, &dir);
	Error loadError = Error::NONE;
	if (error == Error::NONE) {
		loadError = instrument->loadAllAudioFiles(true);
	}
	g_synthRoutines = closeTrace();
	printf("SLT-SYNTH error=%d loadError=%d routines=%d\n", static_cast<int>(error), static_cast<int>(loadError),
	       g_synthRoutines);
	if (error != Error::NONE || instrument == nullptr) {
		printf("SLT-SYNTH FAIL the synth preset did not load\n");
		g_failures++;
		return;
	}
	reportHolder("synth", "preset-sample", firstHolder(static_cast<SoundInstrument*>(instrument)->sources[0]),
	             kSynthPresetResolved, true, "");
	g_refSynthInstrument = instrument;
	g_refSynth = snapshot(outputDemands(*instrument));
}

void loadKitPresetWithAbort() {
	std::string name = kKitPresetName;
	std::string dir = kKitPresetDir;
	Instrument* instrument = nullptr;
	openTrace("kit");
	Error error =
	    deluge::harness::loadPresetFixture(currentSong, OutputType::KIT, &instrument, kKitPresetPath, &name, &dir);
	Error loadError = Error::NONE;
	if (error == Error::NONE) {
		const auto modeBefore = currentUIMode;
		currentUIMode = UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED;
		QwertyUI::predictionInterrupted = false;
		g_abortArmed = true;
		g_abortRaised = false;
		loadError = instrument->loadAllAudioFiles(true);
		g_abortArmed = false;
		QwertyUI::predictionInterrupted = false;
		currentUIMode = modeBefore;
	}
	g_kitRoutines = closeTrace();
	printf("SLT-KIT error=%d loadError=%d abortRaised=%d routines=%d\n", static_cast<int>(error),
	       static_cast<int>(loadError), g_abortRaised ? 1 : 0, g_kitRoutines);
	if (error != Error::NONE || instrument == nullptr) {
		printf("SLT-KIT FAIL the kit preset did not load\n");
		g_failures++;
		return;
	}
	if (loadError != Error::ABORTED_BY_USER || !g_abortRaised) {
		printf("SLT-KIT FAIL the abort raised after the first drum did not stop the load\n");
		g_failures++;
	}
	int32_t index = 0;
	for (Drum* drum = static_cast<Kit*>(instrument)->firstDrum; drum != nullptr; drum = drum->next, index++) {
		if (drum->type != DrumType::SOUND) {
			continue;
		}
		AudioFileHolder* holder = firstHolder(static_cast<SoundDrum*>(drum)->sources[0]);
		if (index == 0) {
			reportHolder("kit", "preset-drum-a", holder, kKitPresetResolvedA, true, "");
		}
		else {
			reportHolder("kit", "preset-drum-b", holder, kKitPresetSampleB, false, "");
		}
	}
	g_refKitInstrument = instrument;
	g_refKit = snapshot(outputDemands(*instrument));
}

// ── The crucial-only load ────────────────────────────────────────────────────

/// A holder of SLTSONG2, and whether the crucial-only pass must bind it.
struct CrucialHolder {
	const char* owner;
	const char* path;
	bool boundAfterCrucialPass;
};

/// The crucial pass binds a sound only when its track's active clip is playing, a kit drum only when its row is
/// unmuted and has notes, and an audio clip whenever it is the active clip on its track, playing or not.
constexpr std::array<CrucialHolder, 9> kCrucialHolders{{
    {"synth-a-sample", kSynthSample, true},
    {"synth-a-wavetable", kWaveTable, true},
    {"synth-b-sample", kCrucialSynthSample, false},
    {"kit1-drum", kKitSample, false},
    {"kit2-drum-a", kCrucialKitSampleA, true},
    {"kit2-drum-b", kCrucialKitSampleB, false},
    {"kit2-drum-c", kCrucialKitSampleC, false},
    {"audio-a", kClipSample, true},
    {"audio-b", kCrucialClipSample, true},
}};

/// Each holder's bound state right after the crucial pass: 1 bound, 0 not, -1 not found.
std::array<int8_t, kCrucialHolders.size()> g_crucialBound{};
bool g_crucialSnapshotTaken = false;

/// Visit every sample holder of @p song: each synth's source ranges, each kit's sound drums, each session audio clip.
template <typename Visit>
void forEachSongHolder(Song& song, Visit visit) {
	for (Output* output = song.firstOutput; output != nullptr; output = output->next) {
		if (output->type == OutputType::SYNTH) {
			auto* synth = static_cast<SoundInstrument*>(output);
			for (int32_t s = 0; s < kNumSources; s++) {
				for (int32_t r = 0; r < std::ssize(synth->sources[s].ranges); r++) {
					visit(synth->sources[s].ranges.getElement(r)->getAudioFileHolder());
				}
			}
		}
		else if (output->type == OutputType::KIT) {
			for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->type != DrumType::SOUND) {
					continue;
				}
				auto* soundDrum = static_cast<SoundDrum*>(drum);
				for (int32_t r = 0; r < std::ssize(soundDrum->sources[0].ranges); r++) {
					visit(soundDrum->sources[0].ranges.getElement(r)->getAudioFileHolder());
				}
			}
		}
	}
	for (Clip* clip : song.sessionClips) {
		if (clip->type == ClipType::AUDIO) {
			visit(&static_cast<AudioClip*>(clip)->sampleHolder);
		}
	}
}

/// Called from the trace sink at "crucial end", on the storage owner, while the song being loaded is preLoadedSong.
void snapshotCrucial() {
	g_crucialBound.fill(-1);
	g_crucialSnapshotTaken = (preLoadedSong != nullptr);
	if (preLoadedSong == nullptr) {
		return;
	}
	forEachSongHolder(*preLoadedSong, [](AudioFileHolder* holder) {
		for (size_t i = 0; i < kCrucialHolders.size(); i++) {
			if (holder->filePath == kCrucialHolders[i].path) {
				g_crucialBound[i] = (holder->audioFile != nullptr) ? 1 : 0;
			}
		}
	});
}

/// SLTSONG2, built from the loaded SLTSONG1 through the firmware's own gestures. Every holder is loaded, so the
/// collect-media save copies every file.
bool buildCrucialSong() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];

	// A second kit, from kCrucialKitPath: drum A's row gets a note, drum B's gets a note and is muted, drum C's stays
	// empty. Playing.
	Clip* kitClipBase = deluge::harness::createNewInstrumentClip(OutputType::KIT, 3);
	if (kitClipBase == nullptr || kitClipBase->output == nullptr || kitClipBase->output->type != OutputType::KIT) {
		return fail("no kit clip could be created from the crucial kit preset");
	}
	auto* kitClip = static_cast<InstrumentClip*>(kitClipBase);
	kitClip->activeIfNoSolo = true;
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, kitClip);
	int32_t drums = 0;
	for (Drum* drum = static_cast<Kit*>(kitClip->output)->firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->type != DrumType::SOUND) {
			continue;
		}
		const std::string& name = static_cast<SoundDrum*>(drum)->drumName;
		if (name != kCrucialDrumA && name != kCrucialDrumB && name != kCrucialDrumC) {
			continue;
		}
		drums++;
		if (name == kCrucialDrumC) {
			continue;
		}
		ModelStackWithNoteRow* rowStack = kitClip->getNoteRowForDrum(modelStack, drum);
		NoteRow* row = (rowStack != nullptr) ? rowStack->getNoteRowAllowNull() : nullptr;
		if (row == nullptr) {
			return fail("a crucial kit drum has no note row");
		}
		if (row->attemptNoteAdd(0, 1, 64, row->getDefaultProbability(), row->getDefaultIterance(),
		                        row->getDefaultFill(rowStack), rowStack, nullptr)
		    == 0) {
			return fail("a note could not be added to a crucial kit row");
		}
		row->muted = (name == kCrucialDrumB);
	}
	if (drums != 3) {
		return fail("the crucial kit clip does not have its three drums");
	}

	// A second synth, from whichever synth preset the gesture picks, on its own sample. Not playing.
	Clip* synthClipBase = deluge::harness::createNewInstrumentClip(OutputType::SYNTH, 4);
	if (synthClipBase == nullptr || synthClipBase->output == nullptr
	    || synthClipBase->output->type != OutputType::SYNTH) {
		return fail("no second synth clip could be created");
	}
	synthClipBase->activeIfNoSolo = false;
	auto* synthB = static_cast<SoundInstrument*>(synthClipBase->output);
	synthB->sources[0].setOscType(OscType::SAMPLE);
	AudioFileHolder* synthBHolder = firstHolder(synthB->sources[0]);
	if (synthBHolder == nullptr) {
		return fail("the second synth has no sample range");
	}
	synthBHolder->setAudioFile(nullptr);
	synthBHolder->filePath = kCrucialSynthSample;
	(void)synthBHolder->loadFile(false, false, true);

	// A second audio clip, on a track of its own: its track's active clip, but not playing.
	Clip* audioClipBase = sessionView.createNewClip(OutputType::AUDIO, 5);
	if (audioClipBase == nullptr || audioClipBase->type != ClipType::AUDIO) {
		return fail("no second audio clip could be created");
	}
	auto* audioClipB = static_cast<AudioClip*>(audioClipBase);
	audioClipB->activeIfNoSolo = false;
	audioClipB->sampleHolder.filePath = kCrucialClipSample;
	audioClipB->loadSample(true);

	int32_t playing = 0;
	for (Clip* clip : currentSong->sessionClips) {
		playing += currentSong->isClipActive(clip) ? 1 : 0;
	}
	printf("SLT-CRUCIAL setup clips=%d playing=%d kitDrums=%d synthB=%d audioB=%d audioBActiveOnTrack=%d\n",
	       static_cast<int>(currentSong->sessionClips.size()), playing, drums,
	       synthBHolder->audioFile != nullptr ? 1 : 0, audioClipB->sampleHolder.audioFile != nullptr ? 1 : 0,
	       audioClipB->isActiveOnOutput() ? 1 : 0);
	return true;
}

/// Drop every holder's file, delete each from memory and remove every original the card still has, so the crucial
/// load finds nothing in memory and only the collected copies in SLTSONG2's folder on the card.
void releaseCrucialSongFiles() {
	forEachSongHolder(*currentSong, [](AudioFileHolder* holder) { holder->setAudioFile(nullptr); });
	for (const CrucialHolder& entry : kCrucialHolders) {
		if (!audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(entry.path)) {
			printf("SLT-CRUCIAL FAIL %s is still referenced and could not be released\n", entry.path);
			g_failures++;
		}
		(void)deluge::io::unlink(entry.path);
	}
}

/// Render kRenderBlocksPerTick blocks through the app's render entry, as the device's audio task would, so the
/// playback clock moves toward the armed swap. The output is discarded.
void renderWhileArmed() {
	static DelugeStereoSample silence[kRenderBlockFrames]{};
	static DelugeStereoSample output[kRenderBlockFrames]{};
	for (int32_t block = 0; block < kRenderBlocksPerTick; block++) {
		deluge_app_render(silence, output, kRenderBlockFrames);
		g_crucialRendered++;
	}
}

void checkCrucialLoad() {
	printf("SLT-CRUCIAL-SUMMARY clockAtLoad=%d swapAfterTicks=%d renderedBlocks=%d routines=%d snapshot=%d\n",
	       g_crucialClockAtLoad ? 1 : 0, g_crucialSwapTick, g_crucialRendered, g_crucialRoutines,
	       g_crucialSnapshotTaken ? 1 : 0);
	if (!g_crucialClockAtLoad) {
		printf("SLT-CRUCIAL FAIL playback was not running when the load began\n");
		g_failures++;
	}
	if (!g_crucialSnapshotTaken) {
		printf("SLT-CRUCIAL FAIL the load never traced \"crucial end\": it did not take the crucial-only pass\n");
		g_failures++;
	}
	if (g_crucialSwapTick < 0) {
		printf("SLT-CRUCIAL FAIL the load finished without its armed swap\n");
		g_failures++;
	}
	for (size_t i = 0; i < kCrucialHolders.size(); i++) {
		const CrucialHolder& entry = kCrucialHolders[i];
		printf("SLT-CRUCIAL pass holder=%s path=%s bound=%d\n", entry.owner, entry.path, g_crucialBound[i]);
		if (g_crucialSnapshotTaken && g_crucialBound[i] != (entry.boundAfterCrucialPass ? 1 : 0)) {
			printf("SLT-CRUCIAL FAIL holder=%s wanted bound=%d after the crucial pass\n", entry.owner,
			       entry.boundAfterCrucialPass ? 1 : 0);
			g_failures++;
		}
	}
	// After the swap and the load's later passes, every holder is bound to its collected copy.
	std::array<int32_t, kCrucialHolders.size()> found{};
	forEachSongHolder(*currentSong, [&found](AudioFileHolder* holder) {
		for (size_t i = 0; i < kCrucialHolders.size(); i++) {
			const CrucialHolder& entry = kCrucialHolders[i];
			if (holder->filePath != entry.path) {
				continue;
			}
			found[i]++;
			const std::string alt = std::string(kCrucialSongFolder) + "/" + (entry.path + std::strlen("SAMPLES/"));
			reportHolder("crucial", entry.owner, holder, entry.path, true, alt.c_str());
		}
	});
	for (size_t i = 0; i < kCrucialHolders.size(); i++) {
		if (found[i] != 1) {
			printf("SLT-CRUCIAL FAIL holder=%s found %d times in the loaded song\n", kCrucialHolders[i].owner,
			       found[i]);
			g_failures++;
		}
	}
}

// ── The async loads ──────────────────────────────────────────────────────────
//
// Each load above runs again through AsyncSampleLoad, ticked from dispatch_step on the interaction tier rather than
// on the storage owner: SLTSONG2's crucial walk and full walk, the synth preset, and the kit preset with its abort.
// Before each, every file the load binds is dropped from memory, so each must come through the async loader, and the
// loader holds every load kAsyncLatencyMs so each spans several ticks. Each must end as its blocking load did.

enum class AsyncLeg : uint8_t { Crucial, Song, Synth, Kit, Race, CardGone, Stall, Orphan, Done };

const char* asyncLegName(AsyncLeg leg) {
	switch (leg) {
	case AsyncLeg::Crucial:
		return "crucial";
	case AsyncLeg::Song:
		return "song";
	case AsyncLeg::Synth:
		return "synth";
	case AsyncLeg::Kit:
		return "kit";
	case AsyncLeg::Race:
		return "race";
	case AsyncLeg::CardGone:
		return "card-gone";
	case AsyncLeg::Stall:
		return "stall";
	case AsyncLeg::Orphan:
		return "probe-orphan";
	case AsyncLeg::Done:
		break;
	}
	return "done";
}

const char* asyncStateName(deluge::storage::AsyncSampleLoad::State state) {
	using State = deluge::storage::AsyncSampleLoad::State;
	switch (state) {
	case State::Loading:
		return "Loading";
	case State::Loaded:
		return "Loaded";
	case State::Failed:
		return "Failed";
	}
	return "?";
}

const char* asyncReasonName(deluge::storage::AsyncSampleLoad::Reason reason) {
	using Reason = deluge::storage::AsyncSampleLoad::Reason;
	switch (reason) {
	case Reason::None:
		return "None";
	case Reason::Aborted:
		return "Aborted";
	case Reason::CardUnusable:
		return "CardUnusable";
	case Reason::Stalled:
		return "Stalled";
	}
	return "?";
}

AsyncLeg g_asyncLeg = AsyncLeg::Crucial;
/// SLTSONG2 as its blocking load left it: the full walk's holders, in load order.
std::vector<HolderOutcome> g_refSong;
/// The instrument an async preset leg loads, read again from its preset file.
Instrument* g_asyncInstrument = nullptr;
deluge::storage::AudioLoadContext g_asyncContext{};
std::optional<deluge::storage::AsyncSampleLoad> g_asyncLoad;
int32_t g_asyncTicks = 0;
int32_t g_sampleOpensBefore = 0;
int32_t g_probesBefore = 0;
/// The leg's AsyncOnly sample lookups that reached the blocking card read, and that probed the folder, on the
/// interaction tier.
int32_t g_asyncSampleOpens = 0;
int32_t g_asyncProbes = 0;

/// Where the card-gone leg is. Its first load is ticked until the folder probe is out, then the card is ejected and it
/// is ticked once more; then the card is put back and a second load on the same context runs to its end.
enum class CardGoneStep : uint8_t { AwaitProbing, EjectAndTick, AwaitSecond };
CardGoneStep g_cardGoneStep = CardGoneStep::AwaitProbing;
/// How the card-gone leg's first load ended, and its context's folder status right after.
deluge::storage::AsyncSampleLoad::State g_cardGoneFirstState = deluge::storage::AsyncSampleLoad::State::Loading;
deluge::storage::AsyncSampleLoad::Reason g_cardGoneFirstReason = deluge::storage::AsyncSampleLoad::Reason::None;
AlternateLoadDirStatus g_cardGoneStatusAfter = AlternateLoadDirStatus::NONE_SET;

/// The synth leg's instrument, which holds the preset's file until the stall leg reads the preset again.
Instrument* g_asyncSynthInstrument = nullptr;
/// Where the stall leg is: its first load is ticked until it ends, then the loader's hold is lifted and a second load
/// of the same instrument runs to its end.
enum class StallStep : uint8_t { AwaitFirst, AwaitSecond };
StallStep g_stallStep = StallStep::AwaitFirst;
/// How the stall leg's first load ended.
deluge::storage::AsyncSampleLoad::Progress g_stallFirst{};

/// Where the probe-orphan leg is: a song load is ticked until its folder probe is out, then dropped for a load of only
/// the song's wavetable holder on the same context.
enum class OrphanStep : uint8_t { AwaitProbing, AwaitWavetable };
OrphanStep g_orphanStep = OrphanStep::AwaitProbing;
/// The song's wavetable holder, as its walk demands it.
deluge::storage::SampleDemand g_orphanDemand{};

/// Drop the reference instrument's files, and read the preset again into a fresh instrument for the async leg.
void reloadPreset(Instrument* reference, const char* residentPath, OutputType type, const char* path,
                  const char* presetName, const char* presetDir) {
	if (reference != nullptr) {
		unbindAll(outputDemands(*reference));
	}
	if (!audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(residentPath)) {
		printf("SLT-ASYNC FAIL %s is still referenced and could not be released\n", residentPath);
		g_failures++;
	}
	std::string name = presetName;
	std::string dir = presetDir;
	g_asyncInstrument = nullptr;
	const Error error = deluge::harness::loadPresetFixture(currentSong, type, &g_asyncInstrument, path, &name, &dir);
	if (error != Error::NONE || g_asyncInstrument == nullptr) {
		printf("SLT-ASYNC FAIL leg=%s the preset could not be read again (error %d)\n", asyncLegName(g_asyncLeg),
		       static_cast<int>(error));
		g_failures++;
		g_asyncInstrument = nullptr;
	}
}

/// On the storage owner: drop what the leg's load will bind.
void releaseForAsyncLeg() {
	switch (g_asyncLeg) {
	case AsyncLeg::Crucial:
	case AsyncLeg::Song:
	case AsyncLeg::Race:
	case AsyncLeg::CardGone:
	case AsyncLeg::Orphan:
		releaseCrucialSongFiles();
		break;
	case AsyncLeg::Synth:
		reloadPreset(g_refSynthInstrument, kSynthPresetResolved, OutputType::SYNTH, kSynthPresetPath, kSynthPresetName,
		             kSynthPresetDir);
		g_asyncSynthInstrument = g_asyncInstrument;
		break;
	case AsyncLeg::Kit:
		reloadPreset(g_refKitInstrument, kKitPresetResolvedA, OutputType::KIT, kKitPresetPath, kKitPresetName,
		             kKitPresetDir);
		break;
	case AsyncLeg::Stall:
		// The synth leg's instrument still holds the preset's file; the stall leg reads the preset into another.
		reloadPreset(g_asyncSynthInstrument, kSynthPresetResolved, OutputType::SYNTH, kSynthPresetPath,
		             kSynthPresetName, kSynthPresetDir);
		break;
	case AsyncLeg::Done:
		break;
	}
}

/// Build the leg's load. False when there is nothing to load.
bool beginAsyncLeg() {
	using deluge::storage::AsyncSampleLoad;
	switch (g_asyncLeg) {
	case AsyncLeg::Crucial:
		g_asyncContext = deluge::storage::AudioLoadContext::forSong(kSongDir, kCrucialSongName);
		g_asyncLoad.emplace(AsyncSampleLoad::forCrucialSong(*currentSong, &g_asyncContext));
		break;
	case AsyncLeg::Song:
	case AsyncLeg::Race:
	case AsyncLeg::CardGone:
		g_asyncContext = deluge::storage::AudioLoadContext::forSong(kSongDir, kCrucialSongName);
		g_asyncLoad.emplace(AsyncSampleLoad::forSong(*currentSong, &g_asyncContext));
		break;
	case AsyncLeg::Orphan:
		g_orphanDemand = {};
		for (const deluge::storage::SampleDemand& demand : songDemands(*currentSong)) {
			if (demand.holder->audioFileType == AudioFileType::WAVETABLE) {
				g_orphanDemand = demand;
			}
		}
		if (g_orphanDemand.holder == nullptr) {
			return false;
		}
		g_asyncContext = deluge::storage::AudioLoadContext::forSong(kSongDir, kCrucialSongName);
		g_asyncLoad.emplace(AsyncSampleLoad::forSong(*currentSong, &g_asyncContext));
		break;
	case AsyncLeg::Synth:
	case AsyncLeg::Kit:
	case AsyncLeg::Stall:
		if (g_asyncInstrument == nullptr) {
			return false;
		}
		// No context: the preset's own folder is the load's, as it is for the blocking preset load.
		g_asyncLoad.emplace(AsyncSampleLoad::forOutput(*g_asyncInstrument, nullptr));
		break;
	case AsyncLeg::Done:
		return false;
	}
	if (g_asyncLeg == AsyncLeg::Kit) {
		// The same abort as the blocking kit load: raised the first time a holder is bound with a file.
		openTrace("async-kit", "LTA");
		QwertyUI::predictionInterrupted = false;
		g_abortArmed = true;
		g_abortRaised = false;
	}
	if (g_asyncLeg == AsyncLeg::Race || g_asyncLeg == AsyncLeg::CardGone) {
		// Only the folder probe's lines: the race leg counts the lookups that waited for the probe, and the card-gone
		// leg reads the first answer its second load collects.
		g_probeLinesOnly = true;
		g_probeWaits = 0;
		g_firstProbeAnswerArmed = false;
		g_firstProbeAnswer = -1;
		g_cardGoneStep = CardGoneStep::AwaitProbing;
		openTrace(asyncLegName(g_asyncLeg), "LTP");
		deluge_file_bytes_set_latency_ms((g_asyncLeg == AsyncLeg::Race) ? kRaceProbeLatencyMs
		                                                                : kCardGoneProbeLatencyMs);
	}
	g_stallStep = StallStep::AwaitFirst;
	if (g_asyncLeg == AsyncLeg::Orphan) {
		// Held, so the probe is still out when the song's load is dropped.
		g_orphanStep = OrphanStep::AwaitProbing;
		deluge_file_bytes_set_latency_ms(kCardGoneProbeLatencyMs);
	}
	g_sampleOpensBefore = deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoad);
	g_probesBefore = deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoadProbe);
	deluge_audio_load_set_latency_ms((g_asyncLeg == AsyncLeg::Stall) ? kStallHoldMs : kAsyncLatencyMs);
	g_asyncTicks = 0;
	return true;
}

void onOwner(void (*fn)());

/// The card-gone leg's steps around a tick of its load. False while the leg is still between its steps.
bool stepCardGone(const deluge::storage::AsyncSampleLoad::Progress& progress) {
	using deluge::storage::AsyncSampleLoad;
	switch (g_cardGoneStep) {
	case CardGoneStep::AwaitProbing:
		if (progress.state == AsyncSampleLoad::State::Loading
		    && g_asyncContext.alternateDirStatus == AlternateLoadDirStatus::PROBING) {
			g_cardGoneStep = CardGoneStep::EjectAndTick;
			return false;
		}
		// A load that ended before its probe went out is reported by the check.
		return progress.state != AsyncSampleLoad::State::Loading;
	case CardGoneStep::EjectAndTick:
		g_cardGoneFirstState = progress.state;
		g_cardGoneFirstReason = progress.reason;
		g_cardGoneStatusAfter = g_asyncContext.alternateDirStatus;
		// The card back, on the owner as the card-detect routine puts it back; the probe's hold lifted; and a second
		// load on the same context, whose first folder answer is the one this leg is about.
		onOwner([] { audioFileManager.reinitEjectedCard(); });
		deluge_file_bytes_set_latency_ms(0);
		g_firstProbeAnswerArmed = true;
		g_asyncLoad.emplace(AsyncSampleLoad::forSong(*currentSong, &g_asyncContext));
		g_cardGoneStep = CardGoneStep::AwaitSecond;
		return false;
	case CardGoneStep::AwaitSecond:
		break;
	}
	return true;
}

/// The stall leg's step around a tick of its load. False while the leg is between its two loads.
bool stepStall(const deluge::storage::AsyncSampleLoad::Progress& progress) {
	using deluge::storage::AsyncSampleLoad;
	if (g_stallStep == StallStep::AwaitSecond) {
		return true;
	}
	if (progress.state == AsyncSampleLoad::State::Loading) {
		return false;
	}
	// The first load has ended. Lift the hold and load the same instrument again: the request the first load left in
	// flight answers once its hold runs out, well inside the second load's bound.
	g_stallFirst = progress;
	deluge_audio_load_set_latency_ms(0);
	g_asyncLoad.emplace(AsyncSampleLoad::forOutput(*g_asyncInstrument, nullptr));
	g_stallStep = StallStep::AwaitSecond;
	return false;
}

/// The probe-orphan leg's step around a tick of its load. False while the leg is between its two loads.
bool stepOrphan(const deluge::storage::AsyncSampleLoad::Progress& progress) {
	using deluge::storage::AsyncSampleLoad;
	switch (g_orphanStep) {
	case OrphanStep::AwaitProbing:
		if (progress.state == AsyncSampleLoad::State::Loading
		    && g_asyncContext.alternateDirStatus == AlternateLoadDirStatus::PROBING) {
			// Drop the song's load with its probe out, lift the probe's hold, and load only the song's wavetable on the
			// same context: no sample is left to collect the probe's answer.
			deluge_file_bytes_set_latency_ms(0);
			g_asyncLoad.emplace(AsyncSampleLoad::forHolder(g_orphanDemand, &g_asyncContext));
			g_orphanStep = OrphanStep::AwaitWavetable;
			return false;
		}
		// A load that ended before its probe went out is reported by the check.
		return progress.state != AsyncSampleLoad::State::Loading;
	case OrphanStep::AwaitWavetable:
		break;
	}
	return true;
}

/// One tick of the leg's load, on the interaction tier. True once the load has ended, or has run past its bound.
bool tickAsyncLeg() {
	const auto modeBefore = currentUIMode;
	if (g_asyncLeg == AsyncLeg::Kit) {
		// The preset browser's load mode, in which shouldAbortLoading() sees the abort.
		currentUIMode = UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED;
	}
	if (g_asyncLeg == AsyncLeg::CardGone && g_cardGoneStep == CardGoneStep::EjectAndTick) {
		// The eject, on this tier and in the same step as the tick after it, so nothing puts the card back in between.
		// setCardEjected() flushes the loader's and the byte channel's tables, as the card-detect routine's call does.
		audioFileManager.setCardEjected();
	}
	const deluge::storage::AsyncSampleLoad::Progress progress = g_asyncLoad->tick();
	currentUIMode = modeBefore;
	g_asyncTicks++;
	if (g_asyncLeg == AsyncLeg::CardGone && !stepCardGone(progress) && g_asyncTicks < kMaxAsyncTicks) {
		return false;
	}
	if (g_asyncLeg == AsyncLeg::Stall && !stepStall(progress) && g_asyncTicks < kMaxAsyncTicks) {
		return false;
	}
	if (g_asyncLeg == AsyncLeg::Orphan && !stepOrphan(progress) && g_asyncTicks < kMaxAsyncTicks) {
		return false;
	}
	const bool ended = (progress.state != deluge::storage::AsyncSampleLoad::State::Loading);
	if (!ended && g_asyncTicks < kMaxAsyncTicks) {
		return false;
	}
	deluge_audio_load_set_latency_ms(0);
	if (g_asyncLeg == AsyncLeg::Orphan) {
		deluge_file_bytes_set_latency_ms(0);
	}
	if (g_asyncLeg == AsyncLeg::Race || g_asyncLeg == AsyncLeg::CardGone) {
		deluge_file_bytes_set_latency_ms(0);
		g_probeLinesOnly = false;
		g_firstProbeAnswerArmed = false;
		(void)closeTrace();
	}
	if (g_asyncLeg == AsyncLeg::Kit) {
		g_abortArmed = false;
		QwertyUI::predictionInterrupted = false;
		(void)closeTrace();
	}
	g_asyncSampleOpens =
	    deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoad) - g_sampleOpensBefore;
	g_asyncProbes =
	    deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoadProbe) - g_probesBefore;
	if (!ended) {
		printf("SLT-ASYNC FAIL leg=%s the load had not ended after %d ticks\n", asyncLegName(g_asyncLeg), g_asyncTicks);
		g_failures++;
	}
	return true;
}

/// Print every holder the async load left, and fail each that differs from what the blocking load left.
void compareOutcomes(const char* leg, const std::vector<HolderOutcome>& want, const std::vector<HolderOutcome>& got) {
	for (size_t i = 0; i < got.size(); i++) {
		const HolderOutcome& outcome = got[i];
		printf("SLT-ASYNC holder leg=%s index=%zu path=%s bound=%d alt=%s name=%s\n", leg, i, outcome.path.c_str(),
		       outcome.bound ? 1 : 0, outcome.alt.c_str(), outcome.clipName.c_str());
		if (i < want.size() && !(outcome == want[i])) {
			const HolderOutcome& wanted = want[i];
			printf("SLT-ASYNC FAIL leg=%s index=%zu wanted path=%s bound=%d alt=%s name=%s, as the blocking load left "
			       "it\n",
			       leg, i, wanted.path.c_str(), wanted.bound ? 1 : 0, wanted.alt.c_str(), wanted.clipName.c_str());
			g_failures++;
		}
	}
	if (got.size() != want.size()) {
		printf("SLT-ASYNC FAIL leg=%s walked %zu holders; the blocking load walked %zu\n", leg, got.size(),
		       want.size());
		g_failures++;
	}
}

/// The crucial leg against the crucial pass's snapshot: the same holders bound, each to its collected copy.
void checkAsyncCrucial() {
	std::array<int32_t, kCrucialHolders.size()> found{};
	forEachSongHolder(*currentSong, [&found](AudioFileHolder* holder) {
		for (size_t i = 0; i < kCrucialHolders.size(); i++) {
			const CrucialHolder& entry = kCrucialHolders[i];
			if (holder->filePath != entry.path) {
				continue;
			}
			found[i]++;
			const AudioFile* file = holder->audioFile;
			const std::string alt = (file != nullptr) ? file->loadedFromAlternatePath : std::string{};
			const std::string collectedCopy =
			    std::string(kCrucialSongFolder) + "/" + (entry.path + std::strlen("SAMPLES/"));
			printf("SLT-ASYNC holder leg=crucial owner=%s path=%s bound=%d alt=%s\n", entry.owner, entry.path,
			       file != nullptr ? 1 : 0, alt.c_str());
			const bool wantBound = (g_crucialBound[i] == 1);
			if ((file != nullptr) != wantBound || (wantBound && alt != collectedCopy)) {
				printf("SLT-ASYNC FAIL leg=crucial owner=%s wanted bound=%d alt=%s, as the crucial pass left it\n",
				       entry.owner, wantBound ? 1 : 0, wantBound ? collectedCopy.c_str() : "");
				g_failures++;
			}
		}
	});
	for (size_t i = 0; i < kCrucialHolders.size(); i++) {
		if (found[i] != 1) {
			printf("SLT-ASYNC FAIL leg=crucial owner=%s found %d times in the song\n", kCrucialHolders[i].owner,
			       found[i]);
			g_failures++;
		}
	}
}

/// The kit leg: stopped by its abort after as many drums as the blocking load bound, each from the kit's own folder.
/// Which drum binds first depends on which result the loader returns first, so its identity is reported, not required.
void checkAsyncKit() {
	const std::vector<HolderOutcome> got = snapshot(outputDemands(*g_asyncInstrument));
	const auto wantBound = std::ranges::count_if(g_refKit, [](const HolderOutcome& outcome) { return outcome.bound; });
	int32_t gotBound = 0;
	for (size_t i = 0; i < got.size(); i++) {
		const HolderOutcome& outcome = got[i];
		printf("SLT-ASYNC holder leg=kit index=%zu path=%s bound=%d alt=%s\n", i, outcome.path.c_str(),
		       outcome.bound ? 1 : 0, outcome.alt.c_str());
		if (!outcome.bound) {
			continue;
		}
		gotBound++;
		if (!outcome.path.starts_with("KITS/SLTPKIT/") || !outcome.alt.empty()) {
			printf("SLT-ASYNC FAIL leg=kit index=%zu bound a file that is not its copy in the kit's folder\n", i);
			g_failures++;
		}
	}
	if (gotBound != wantBound || got.size() != g_refKit.size()) {
		printf("SLT-ASYNC FAIL leg=kit bound %d of %zu drums; the blocking load bound %d of %zu\n", gotBound,
		       got.size(), static_cast<int32_t>(wantBound), g_refKit.size());
		g_failures++;
	}
}

/// The card-gone leg: its first load ended on the ejected card with its probe still out, and the second load's first
/// folder answer was the stale one, removed at the collect -- so the probe that went out on the old card was never
/// taken as an answer about the new one.
void checkCardGone() {
	using deluge::storage::AsyncSampleLoad;
	printf("SLT-ASYNC probe leg=card-gone first=%s/%s statusAfter=%d firstAnswer=%d\n",
	       asyncStateName(g_cardGoneFirstState), asyncReasonName(g_cardGoneFirstReason),
	       static_cast<int>(g_cardGoneStatusAfter), g_firstProbeAnswer);
	if (g_cardGoneFirstState != AsyncSampleLoad::State::Failed
	    || g_cardGoneFirstReason != AsyncSampleLoad::Reason::CardUnusable) {
		printf("SLT-ASYNC FAIL leg=card-gone the first load did not end Failed/CardUnusable on the ejected card\n");
		g_failures++;
	}
	if (g_cardGoneStatusAfter != AlternateLoadDirStatus::PROBING) {
		printf("SLT-ASYNC FAIL leg=card-gone the first load did not end with its folder probe still out\n");
		g_failures++;
	}
	if (g_firstProbeAnswer != static_cast<int32_t>(deluge::storage::bytes::ByteIo::Unknown)) {
		printf("SLT-ASYNC FAIL leg=card-gone the second load's first folder answer was not the stale one, so this leg "
		       "proves nothing\n");
		g_failures++;
	}
}

/// The stall leg's first load: held far past the bound, it ended Failed/Stalled on exactly the bound's walk, with
/// nothing bound. The second load, once the hold was lifted, is compared with the blocking load by the caller.
void checkStall() {
	using deluge::storage::AsyncSampleLoad;
	printf("SLT-ASYNC probe leg=stall first=%s/%s ticks=%d stalledTicks=%d bound=%d inFlight=%d\n",
	       asyncStateName(g_stallFirst.state), asyncReasonName(g_stallFirst.reason), g_stallFirst.ticks,
	       g_stallFirst.stalledTicks, g_stallFirst.bound, g_stallFirst.inFlight);
	if (g_stallFirst.state != AsyncSampleLoad::State::Failed
	    || g_stallFirst.reason != AsyncSampleLoad::Reason::Stalled) {
		printf("SLT-ASYNC FAIL leg=stall the held load did not end Failed/Stalled\n");
		g_failures++;
	}
	if (g_stallFirst.stalledTicks != AsyncSampleLoad::kMaxStalledTicks
	    || g_stallFirst.ticks != AsyncSampleLoad::kMaxStalledTicks) {
		printf("SLT-ASYNC FAIL leg=stall the held load did not end on the bound's walk (%d)\n",
		       AsyncSampleLoad::kMaxStalledTicks);
		g_failures++;
	}
	if (g_stallFirst.bound != 0) {
		printf("SLT-ASYNC FAIL leg=stall the held load bound a holder\n");
		g_failures++;
	}
}

/// The probe-orphan leg: the wavetable-only load ended bound to its collected copy in the song's folder.
void checkOrphan() {
	const AudioFile* file = g_orphanDemand.holder->audioFile;
	const std::string alt = (file != nullptr) ? file->loadedFromAlternatePath : std::string{};
	const std::string collectedCopy = std::string(kCrucialSongFolder) + "/" + (kWaveTable + std::strlen("SAMPLES/"));
	printf("SLT-ASYNC holder leg=probe-orphan path=%s bound=%d alt=%s status=%d\n",
	       g_orphanDemand.holder->filePath.c_str(), file != nullptr ? 1 : 0, alt.c_str(),
	       static_cast<int>(g_asyncContext.alternateDirStatus));
	if (file == nullptr || alt != collectedCopy) {
		printf("SLT-ASYNC FAIL leg=probe-orphan the wavetable was not bound to its collected copy %s\n",
		       collectedCopy.c_str());
		g_failures++;
	}
}

void checkAsyncLeg() {
	using deluge::storage::AsyncSampleLoad;
	const char* leg = asyncLegName(g_asyncLeg);
	if (!g_asyncLoad.has_value()) {
		printf("SLT-ASYNC leg=%s state=none\n", leg);
		printf("SLT-ASYNC FAIL leg=%s had nothing to load\n", leg);
		g_failures++;
		return;
	}
	const AsyncSampleLoad::Progress& progress = g_asyncLoad->progress();
	const auto index = static_cast<size_t>(g_asyncLeg);
	printf("SLT-ASYNC leg=%s state=%s reason=%s ticks=%d bound=%d empty=%d inFlight=%d wavetableReads=%d "
	       "sampleOpens=%d probes=%d\n",
	       leg, asyncStateName(progress.state), asyncReasonName(progress.reason), progress.ticks, progress.bound,
	       progress.emptyHolders, progress.inFlight, progress.wavetableReads, g_asyncSampleOpens, g_asyncProbes);

	const bool wantAborted = (g_asyncLeg == AsyncLeg::Kit);
	const bool endedRight = wantAborted ? (progress.state == AsyncSampleLoad::State::Failed
	                                       && progress.reason == AsyncSampleLoad::Reason::Aborted)
	                                    : (progress.state == AsyncSampleLoad::State::Loaded);
	if (!endedRight) {
		printf("SLT-ASYNC FAIL leg=%s ended %s/%s; the blocking load ended %s\n", leg, asyncStateName(progress.state),
		       asyncReasonName(progress.reason), wantAborted ? "by its abort" : "loaded");
		g_failures++;
	}
	if (g_asyncSampleOpens != 0) {
		printf("SLT-ASYNC FAIL leg=%s %d sample lookup(s) read the card on the interaction tier\n", leg,
		       g_asyncSampleOpens);
		g_failures++;
	}
	if (g_asyncProbes != 0) {
		printf("SLT-ASYNC FAIL leg=%s %d folder probe(s) opened the card on the interaction tier\n", leg,
		       g_asyncProbes);
		g_failures++;
	}
	if (progress.wavetableReads != kAsyncWavetableReads[index]) {
		printf("SLT-ASYNC FAIL leg=%s wavetableReads=%d pinned=%d\n", leg, progress.wavetableReads,
		       kAsyncWavetableReads[index]);
		g_failures++;
	}

	switch (g_asyncLeg) {
	case AsyncLeg::Crucial:
		checkAsyncCrucial();
		break;
	case AsyncLeg::Song:
		compareOutcomes(leg, g_refSong, snapshot(songDemands(*currentSong)));
		break;
	case AsyncLeg::Synth:
		compareOutcomes(leg, g_refSynth, snapshot(outputDemands(*g_asyncInstrument)));
		break;
	case AsyncLeg::Kit:
		checkAsyncKit();
		break;
	case AsyncLeg::Race:
		compareOutcomes(leg, g_refSong, snapshot(songDemands(*currentSong)));
		printf("SLT-ASYNC probe leg=race waits=%d\n", g_probeWaits);
		if (g_probeWaits == 0) {
			printf("SLT-ASYNC FAIL leg=race no lookup waited for the folder probe, so the race never happened\n");
			g_failures++;
		}
		break;
	case AsyncLeg::CardGone:
		compareOutcomes(leg, g_refSong, snapshot(songDemands(*currentSong)));
		checkCardGone();
		break;
	case AsyncLeg::Stall:
		compareOutcomes(leg, g_refSynth, snapshot(outputDemands(*g_asyncInstrument)));
		checkStall();
		break;
	case AsyncLeg::Orphan:
		checkOrphan();
		break;
	case AsyncLeg::Done:
		break;
	}
}

/// Queue @p fn on the storage owner: the builds and preset loads do card I/O, which belongs there.
void onOwner(void (*fn)()) {
	if (!deluge::storage::Owner::run([](void* ctx) { reinterpret_cast<void (*)()>(ctx)(); },
	                                 reinterpret_cast<void*>(fn))) {
		printf("SLT-SETUP FAIL the storage owner's queue was full\n");
		g_failures++;
	}
}

/// Whether the blank song's own preset request has landed. The boot song starts on the built-in default synth and asks
/// InstrumentLoad for the first SYNTHS preset on the interaction tier; a scenario that acted on the boot clip before
/// that landed would be working on an Instrument about to be replaced, which no user can do.
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
	case Stage::SongKit:
		onOwner([] {
			if (!writeKitPreset(kSongKitPath, {{kKitDrumName, kKitSample}})) {
				g_stage = Stage::Report;
			}
		});
		g_stage = Stage::Build;
		break;
	case Stage::Build:
		if (g_failures != 0) {
			g_stage = Stage::Report;
			break;
		}
		onOwner([] {
			if (!buildSong()) {
				g_stage = Stage::Report;
			}
		});
		g_stage = Stage::SaveOpen;
		break;
	case Stage::SaveOpen:
		if (g_failures != 0) {
			g_stage = Stage::Report;
			break;
		}
		changeRootUI(&arrangerView);
		(void)SampleLoadTreeProbe::openSongSave(kSongDir);
		g_stage = Stage::SaveOpenWait;
		break;
	case Stage::SaveOpenWait:
		if (!SampleLoadTreeProbe::listingInProgress()) {
			g_stage = Stage::Save;
		}
		break;
	case Stage::Save:
		SampleLoadTreeProbe::typeName(kSongName);
		SampleLoadTreeProbe::collectMedia();
		SampleLoadTreeProbe::pressEnter();
		g_waitTicks = 0;
		g_stage = Stage::SaveWait;
		break;
	case Stage::SaveWait:
		if (!saveSongUI.collectInFlight() && !saveSongUI.docInFlight() && !SaveUI::saveInFlight()) {
			g_stage = Stage::Release;
		}
		else if (++g_waitTicks > kMaxWaitTicks) {
			printf("SLT-SETUP FAIL the song save never finished\n");
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	case Stage::Release:
		onOwner(releaseSongFiles);
		g_stage = Stage::LoadOpen;
		break;
	case Stage::LoadOpen:
		currentSong->setSongFullPath(kSongPath);
		(void)openUI(&loadSongUI);
		g_waitTicks = 0;
		g_stage = Stage::LoadListing;
		break;
	case Stage::LoadListing:
		if (Browser::isListingInProgress()) {
			loadSongUI.graphicsRoutine();
			if (++g_waitTicks > kMaxWaitTicks) {
				printf("SLT-SONG FAIL the load browser's listing never finished\n");
				g_failures++;
				g_stage = Stage::Report;
			}
			break;
		}
		openTrace("song");
		loadSongUI.beginLoad();
		g_waitTicks = 0;
		g_stage = Stage::LoadWait;
		break;
	case Stage::LoadWait:
		if (!loadSongUI.loadInFlight()) {
			g_songRoutines = closeTrace();
			g_stage = Stage::SongCheck;
		}
		else if (++g_waitTicks > kMaxWaitTicks) {
			(void)closeTrace();
			printf("SLT-SONG FAIL the song load never finished\n");
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	case Stage::SongCheck:
		checkSong();
		g_stage = Stage::Presets;
		break;
	case Stage::Presets:
		onOwner([] {
			if (!writePresets()) {
				g_stage = Stage::Report;
			}
		});
		g_stage = Stage::SynthPreset;
		break;
	case Stage::SynthPreset:
		onOwner(loadSynthPreset);
		g_stage = Stage::KitPreset;
		break;
	case Stage::KitPreset:
		onOwner(loadKitPresetWithAbort);
		g_stage = Stage::CrucialKit;
		break;
	case Stage::CrucialKit:
		onOwner([] {
			if (!writeKitPreset(kCrucialKitPath, {{kCrucialDrumA, kCrucialKitSampleA},
			                                      {kCrucialDrumB, kCrucialKitSampleB},
			                                      {kCrucialDrumC, kCrucialKitSampleC}})) {
				g_stage = Stage::Report;
			}
		});
		g_stage = Stage::CrucialBuild;
		break;
	case Stage::CrucialBuild:
		onOwner([] {
			if (!buildCrucialSong()) {
				g_stage = Stage::Report;
			}
		});
		g_stage = Stage::CrucialSaveOpen;
		break;
	case Stage::CrucialSaveOpen:
		// Saved from the session view: a song saved from the arranger view records its arrangement position, and a
		// load while playing then prepares it for arranger playback, which moves every session clip's playing state
		// aside and leaves the crucial-only pass nothing to bind but audio clips.
		changeRootUI(&sessionView);
		(void)SampleLoadTreeProbe::openSongSave(kSongDir);
		g_stage = Stage::CrucialSaveOpenWait;
		break;
	case Stage::CrucialSaveOpenWait:
		if (!SampleLoadTreeProbe::listingInProgress()) {
			g_stage = Stage::CrucialSave;
		}
		break;
	case Stage::CrucialSave:
		SampleLoadTreeProbe::typeName(kCrucialSongName);
		SampleLoadTreeProbe::collectMedia();
		SampleLoadTreeProbe::pressEnter();
		g_waitTicks = 0;
		g_stage = Stage::CrucialSaveWait;
		break;
	case Stage::CrucialSaveWait:
		if (!saveSongUI.collectInFlight() && !saveSongUI.docInFlight() && !SaveUI::saveInFlight()) {
			g_stage = Stage::CrucialRelease;
		}
		else if (++g_waitTicks > kMaxWaitTicks) {
			printf("SLT-CRUCIAL FAIL the crucial song's save never finished\n");
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	case Stage::CrucialRelease:
		onOwner(releaseCrucialSongFiles);
		g_stage = Stage::CrucialPlay;
		break;
	case Stage::CrucialPlay:
		// Session playback of SLTSONG2 as saved: its playing clips are what the armed swap will sync to.
		changeRootUI(&sessionView);
		playbackHandler.playButtonPressed(kInternalButtonPressLatency);
		g_stage = Stage::CrucialLoadOpen;
		break;
	case Stage::CrucialLoadOpen:
		currentSong->setSongFullPath(kCrucialSongPath);
		(void)openUI(&loadSongUI);
		g_waitTicks = 0;
		g_stage = Stage::CrucialListing;
		break;
	case Stage::CrucialListing:
		if (Browser::isListingInProgress()) {
			loadSongUI.graphicsRoutine();
			if (++g_waitTicks > kMaxWaitTicks) {
				printf("SLT-CRUCIAL FAIL the load browser's listing never finished\n");
				g_failures++;
				g_stage = Stage::Report;
			}
			break;
		}
		g_crucialClockAtLoad = playbackHandler.isEitherClockActive();
		openTrace("crucial", "LTC");
		loadSongUI.beginLoad();
		g_waitTicks = 0;
		g_stage = Stage::CrucialWait;
		break;
	case Stage::CrucialWait:
		if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED
		    || currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED) {
			renderWhileArmed();
		}
		else if (g_crucialRendered > 0 && g_crucialSwapTick < 0) {
			g_crucialSwapTick = g_waitTicks;
		}
		if (!loadSongUI.loadInFlight()) {
			g_crucialRoutines = closeTrace();
			playbackHandler.endPlayback();
			g_stage = Stage::CrucialCheck;
		}
		else if (++g_waitTicks > kMaxCrucialWaitTicks) {
			(void)closeTrace();
			printf("SLT-CRUCIAL FAIL the crucial song's load never finished\n");
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	case Stage::CrucialCheck:
		checkCrucialLoad();
		g_stage = Stage::AsyncReference;
		break;
	case Stage::AsyncReference:
		// SLTSONG2 as its blocking load left it, before any async leg drops a file.
		g_refSong = snapshot(songDemands(*currentSong));
		g_asyncLeg = AsyncLeg::Crucial;
		g_stage = Stage::AsyncRelease;
		break;
	case Stage::AsyncRelease:
		onOwner(releaseForAsyncLeg);
		g_stage = Stage::AsyncBegin;
		break;
	case Stage::AsyncBegin:
		g_asyncLoad.reset();
		g_stage = beginAsyncLeg() ? Stage::AsyncWait : Stage::AsyncCheck;
		break;
	case Stage::AsyncWait:
		if (tickAsyncLeg()) {
			g_stage = Stage::AsyncCheck;
		}
		break;
	case Stage::AsyncCheck:
		checkAsyncLeg();
		g_asyncLeg = static_cast<AsyncLeg>(static_cast<uint8_t>(g_asyncLeg) + 1);
		g_stage = (g_asyncLeg == AsyncLeg::Done) ? Stage::Report : Stage::AsyncRelease;
		break;
	case Stage::Report:
		printf("SLT-SUMMARY songRoutines=%d synthRoutines=%d kitRoutines=%d\n", g_songRoutines, g_synthRoutines,
		       g_kitRoutines);
		for (const auto& [load, got, pinned] :
		     {std::tuple{"song", g_songRoutines, kSongRoutines}, std::tuple{"synth", g_synthRoutines, kSynthRoutines},
		      std::tuple{"kit", g_kitRoutines, kKitRoutines},
		      std::tuple{"crucial", g_crucialRoutines, kCrucialRoutines}}) {
			if (got != pinned) {
				printf("SLT-CADENCE FAIL load=%s routines=%d pinned=%d\n", load, got, pinned);
				g_failures++;
			}
		}
		printf("SLT-RESULT failures=%d\n", g_failures);
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

void deluge_scenario_sample_load_tree_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::SongKit;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_waitTicks = 0;
	g_songRoutines = -1;
	g_synthRoutines = -1;
	g_kitRoutines = -1;
	g_crucialRoutines = -1;
	g_crucialClockAtLoad = false;
	g_crucialRendered = 0;
	g_crucialSwapTick = -1;
	g_crucialSnapshotTaken = false;
	g_crucialBound.fill(-1);
	g_asyncLeg = AsyncLeg::Crucial;
	g_refSong.clear();
	g_refSynth.clear();
	g_refKit.clear();
	g_refSynthInstrument = nullptr;
	g_refKitInstrument = nullptr;
	g_asyncInstrument = nullptr;
	g_asyncLoad.reset();
	g_asyncTicks = 0;
	g_probeLinesOnly = false;
	g_probeWaits = 0;
	g_firstProbeAnswerArmed = false;
	g_firstProbeAnswer = -1;
	g_cardGoneStep = CardGoneStep::AwaitProbing;
	g_asyncSynthInstrument = nullptr;
	g_stallStep = StallStep::AwaitFirst;
	g_stallFirst = {};
	g_orphanStep = OrphanStep::AwaitProbing;
	g_orphanDemand = {};
}

bool deluge_scenario_sample_load_tree_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_sample_load_tree_dispatch_step(void) {
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

bool deluge_scenario_sample_load_tree_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_sample_load_tree_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
