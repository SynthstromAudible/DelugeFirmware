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
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/qwerty_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/save/save_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/session_view.h"
#include "io/file.hpp"
#include "memory/fast_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/audio/load_trace.h"
#include "storage/multi_range/multi_range.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

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

constexpr int32_t kMaxWaitTicks = 200000;

/// routineWithClusterLoading calls each load makes, measured on the fixture: the cadence that keeps audio running
/// while a load walks the tree. Pinned so a walk that drops or adds a call is seen.
constexpr int32_t kSongRoutines = 45;
constexpr int32_t kSynthRoutines = 2;
constexpr int32_t kKitRoutines = 3;

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

// ── The load trace ───────────────────────────────────────────────────────────

const char* g_traceLabel = "";
int32_t g_traceRoutines = 0;
/// Raise the abort the first time a holder is bound with a file, so a walk that checks for it stops after one load.
bool g_abortArmed = false;
bool g_abortRaised = false;

void traceSink(const char* line) {
	if (std::strcmp(line, "routine") == 0) {
		g_traceRoutines++;
	}
	printf("LT %s %s\n", g_traceLabel, line);
	if (g_abortArmed && !g_abortRaised && std::strncmp(line, "bind ", 5) == 0 && std::strstr(line, " file=1")) {
		QwertyUI::predictionInterrupted = true;
		g_abortRaised = true;
	}
}

void openTrace(const char* label) {
	g_traceLabel = label;
	g_traceRoutines = 0;
	(void)deluge::storage::load_trace::setSink(&traceSink);
	printf("LT %s begin\n", label);
}

int32_t closeTrace() {
	(void)deluge::storage::load_trace::setSink(nullptr);
	printf("LT %s end routines=%d\n", g_traceLabel, g_traceRoutines);
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
	Clip* kitClipBase = sessionView.reproCreateNewInstrumentClip(OutputType::KIT, 1);
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
	Error error = StorageManager::loadInstrumentFromFile(currentSong, nullptr, OutputType::SYNTH, false, &instrument,
	                                                     kSynthPresetPath, &name, &dir);
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
}

void loadKitPresetWithAbort() {
	std::string name = kKitPresetName;
	std::string dir = kKitPresetDir;
	Instrument* instrument = nullptr;
	openTrace("kit");
	Error error = StorageManager::loadInstrumentFromFile(currentSong, nullptr, OutputType::KIT, false, &instrument,
	                                                     kKitPresetPath, &name, &dir);
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
}

/// Queue @p fn on the storage owner: the builds and preset loads do card I/O, which belongs there.
void onOwner(void (*fn)()) {
	if (!deluge::storage::Owner::run([](void* ctx) { reinterpret_cast<void (*)()>(ctx)(); },
	                                 reinterpret_cast<void*>(fn))) {
		printf("SLT-SETUP FAIL the storage owner's queue was full\n");
		g_failures++;
	}
}

void runTick() {
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
		g_stage = Stage::Report;
		break;
	case Stage::Report:
		printf("SLT-SUMMARY songRoutines=%d synthRoutines=%d kitRoutines=%d\n", g_songRoutines, g_synthRoutines,
		       g_kitRoutines);
		for (const auto& [load, got, pinned] :
		     {std::tuple{"song", g_songRoutines, kSongRoutines}, std::tuple{"synth", g_synthRoutines, kSynthRoutines},
		      std::tuple{"kit", g_kitRoutines, kKitRoutines}}) {
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
