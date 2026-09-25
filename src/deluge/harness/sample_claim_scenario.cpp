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

#include "harness/sample_claim_scenario.h"

#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "gui/context_menu/sample_browser/kit.h"
#include "gui/context_menu/sample_browser/synth.h"
#include "gui/menu_item/file_selector.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/slicer.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/randomize_drums.h"
#include "gui/views/session_view.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "harness/preset_fixture_loader.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/led/pad_leds.h"
#include "libdeluge/audio_load.h" // deluge_audio_load_set_latency_ms
#include "libdeluge/control_surface.h"
#include "libdeluge/file_directory.h" // deluge_file_directory_set_latency_ms
#include "memory/fast_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h" // bytes::interactionTierIo
#include "storage/multi_range/multi_range.h"
#include "storage/owner.h"
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

/// Each leg's file, asked for by that leg only (see pack_sample_claim_image in the harness).
constexpr const char* kSynthSample = "SAMPLES/SCSYN.WAV";
constexpr const char* kCycleSample = "SAMPLES/SCCYCLE.WAV";
constexpr const char* kStereoSample = "SAMPLES/SCSTER.WAV";
/// The gate and stall legs' files: held by the loader from the moment their browser opens, so neither is in memory when
/// its claim begins.
constexpr const char* kGateSample = "SAMPLES/SCGATE.WAV";
constexpr const char* kStallSample = "SAMPLES/SCSTALL.WAV";
/// How long the loader holds each load in the gate leg: long enough that the claim is still loading when the encoder
/// turns, and over well inside the leg's bound.
constexpr uint32_t kGateHoldMs = 400;
/// How long the loader holds each load in the stall leg: far past the claim's stall bound.
constexpr uint32_t kStallHoldMs = 30000;
/// The supersede legs' second files. The landed leg's is in memory from the gate leg, so its preview lands in the tick
/// the cursor reaches it; the loading leg's is not, and the loader holds its preview.
constexpr const char* kSupersedeLanded = "SAMPLES/SCGATE.WAV";
constexpr const char* kSupersedeLoading = "SAMPLES/SCKICK.WAV";
/// How long the loader holds the loading leg's second preview: long enough that the claim the cursor left would finish
/// first, were it not cancelled.
constexpr uint32_t kSupersedeHoldMs = 400;
/// The synth context menu's WaveTable option: Multisamples 0, Basic 1, Single-cycle 2, WaveTable 3.
constexpr int32_t kWaveTableOption = 3;
constexpr const char* kKitPresetSample = "SAMPLES/SCKITD.WAV";
constexpr const char* kKickSample = "SAMPLES/SCKICK.WAV";
constexpr const char* kClipSample = "SAMPLES/SCAUD.WAV";
constexpr const char* kSliceSample = "SAMPLES/SCSLICE.WAV";

/// The one-drum kit presets the new-kit-track gesture picks up, each the first one nobody in the song is using: the kit
/// leg's, then the manual slice leg's. The region slice leaves the first kit with four drums, and a slice indexes the
/// drums it makes from the kit's first.
constexpr const char* kKitPresetPath = "KITS/SCKIT.XML";
constexpr const char* kManualKitPresetPath = "KITS/SCKIT2.XML";
constexpr const char* kKitDrumName = "SCD1";

/// The randomizer legs' kit preset, the third the new-kit-track gesture picks up, and the folder its drums name. The
/// randomizer picks from the folder a drum's current sample sits in, so these files are the randomizer's alone: every
/// other fixture is flat in SAMPLES/, where a pick from this folder can never reach it.
constexpr const char* kRandomizeKitPresetPath = "KITS/SCKIT3.XML";
constexpr const char* kRandomizeFolder = "SAMPLES/RD";
constexpr const char* kRandomizeFirstSample = "SAMPLES/RD/RD1.WAV";

/// The randomizer kit's drums, in the order the preset writes them: two rows that get a note, one muted row with a
/// note, and one row with no notes. Only the first two are eligible for an affect-entire randomize.
struct RandomizeDrumSpec {
	const char* name;
	bool note;
	bool muted;
};
constexpr RandomizeDrumSpec kRandomizeDrums[] = {
    {.name = "RDA", .note = true, .muted = false},
    {.name = "RDB", .note = true, .muted = false},
    {.name = "RDC", .note = true, .muted = true},
    {.name = "RDD", .note = false, .muted = false},
};
constexpr int32_t kRandomizeDrumCount = 4;
/// How many of those rows an affect-entire randomize may touch: the unmuted ones that have notes.
constexpr int32_t kRandomizeEligible = 2;

/// The gate leg's selected drum is pointed into a folder of files nothing has loaded, so its randomized load reaches
/// the loader, whose hold keeps the walk out while the second press and the edit pad arrive.
constexpr const char* kRandomizeGateSample = "SAMPLES/RDG/G1.WAV";
/// The stall leg's first row: alone in its folder, and loaded before the hold, so the walk lands it without the loader.
constexpr const char* kRandomizeStallKept = "SAMPLES/RDK/K1.WAV";
constexpr const char* kRandomizeStallKeptName = "K1";
/// The stall leg's second row: a folder of files nothing has loaded, so its load is held past the stall bound.
constexpr const char* kRandomizeStallSample = "SAMPLES/RDS/S1.WAV";
/// The column the gate leg's edit pad presses: empty on every row, inside the clip.
/// The cancel leg's hold on the DIRECTORY channel: the row's open is still out when the walk is cancelled, so the
/// cancel has to drain rather than end. Shorter than RandomizeDrums' own 300 ms give-up bound, or the drain would
/// give up before the handle it is waiting for ever arrived -- and it is the arrival that has to be collected.
constexpr uint32_t kRandomizeCancelHoldMs = 100;
/// Passes let through before each cancel. The first one puts the row's open on the channel (the walk requests from
/// its own await step, not from the press), so cancelling on it would find nothing outstanding and never drain.
constexpr int32_t kRandomizeCancelPasses = 2;
/// Cancels made, one per round: one more than the eight directory handles libdeluge/file_directory.h documents. A
/// drain that collected its handle without adopting it would have burned every slot before the last round, so the
/// ordinary randomize the leg ends with could not open its folder at all. That walk landing is the claim: counting
/// the collects cannot make it, since the collect is what happens either way.
constexpr int32_t kRandomizeCancelRounds = 9;

constexpr int32_t kRandomizeGateEditX = 1;

/// The folder import legs' folders, each its own: the kit import's three files share the prefix "IMK_", which the kit's
/// drum names drop; the multisample import's three sines sit an octave apart, a second long each.
constexpr const char* kImportKitFolder = "SAMPLES/IMK";
constexpr const char* kImportKitFile = "SAMPLES/IMK/IMK_HAT.WAV";
constexpr const char* kImportMultiFolder = "SAMPLES/IMM";
constexpr const char* kImportMultiFile = "SAMPLES/IMM/IMM_A2.WAV";
/// The kit import's drums, in filename order: the first reuses the kit's one drum.
constexpr const char* kImportKitNames[] = {"HAT", "KICK", "SNARE"};
constexpr int32_t kImportKitCount = 3;
/// The multisample import's files, in pitch order.
constexpr const char* kImportMultiFiles[] = {"SAMPLES/IMM/IMM_A2.WAV", "SAMPLES/IMM/IMM_A3.WAV",
                                             "SAMPLES/IMM/IMM_A4.WAV"};
constexpr int32_t kImportMultiCount = 3;
/// Each range's top note as today's importer sets it, measured: the rounded-down midpoint between detected pitches, and
/// the top range open-ended.
constexpr int32_t kImportMultiTopNotes[] = {50, 62, 32767};
/// The kit import leg's one-drum preset, the fourth the new-kit-track gesture picks up.
constexpr const char* kImportKitPresetPath = "KITS/SCKIT4.XML";
constexpr const char* kImportKitDrumName = "IMD1";
/// The multisample import leg's blank synth preset, the one synth preset on the card.
constexpr const char* kImportSynthPresetPath = "SYNTHS/SCSYN1.XML";
/// Both context menus' first option: the kit menu's "Load All", the synth menu's "Multisamples".
constexpr int32_t kImportOption = 0;

/// The import gate leg's folder and its one-drum kit's preset: files nothing loads before the leg, so the loader's hold
/// keeps the import out while BACK and Select arrive.
constexpr const char* kImportGateFile = "SAMPLES/IMG/IMG_A.WAV";
constexpr const char* kImportGatePresetPath = "KITS/SCKIT5.XML";
constexpr const char* kImportGateDrumName = "IMD2";
constexpr int32_t kImportGateCount = 3;
/// The import stall leg's folder: its first and last files are loaded in the leg's preparation and land at once; the
/// middle one is loaded by nothing, so its load is held past the stall bound. The card lists a folder in the order its
/// packer found the files, so with the held file in the middle one in memory is always listed before it.
constexpr const char* kImportStallKept = "SAMPLES/IMS/IMS_A.WAV";
constexpr const char* kImportStallKeptLast = "SAMPLES/IMS/IMS_C.WAV";

/// How many regions the slice leg cuts the sample into.
constexpr int32_t kSliceRegions = 4;

/// The manual slice leg's slices, each a start frame and a transpose, as pad presses on a playing voice would add them.
constexpr SliceItem kManualSlices[] = {
    {.startPos = 0, .transpose = 1}, {.startPos = 11025, .transpose = 2}, {.startPos = 33075, .transpose = 3}};
constexpr int32_t kManualSliceCount = 3;

/// Scenario ticks a browser listing may take before it is called wedged.
constexpr int32_t kMaxListingTicks = 20000;
/// Scenario ticks a claim or a slice may take before it is called wedged.
constexpr int32_t kMaxLegTicks = 2000;
/// Scenario ticks the stall leg's claim may take to give up before it is called wedged: far past its stall bound.
constexpr int32_t kMaxStallTicks = 20000;

enum class Leg : uint8_t {
	Synth,
	WaveTable,
	Fallback,
	Gate,
	Supersede,
	SupersedeLoading,
	Kit,
	AudioClip,
	Slice,
	SliceManual,
	RandomizeOne,
	RandomizeAll,
	RandomizeGate,
	ImportKit,
	ImportMulti,
	ImportGate,
	Stall,
	RandomizeStall,
	RandomizeCancelScan,
	ImportStall,
	Done
};
enum class Step : uint8_t { Prepare, Open, Listing, Select, SelectWait, PreviewWait, Act, Wait };

Leg g_leg = Leg::Synth;
Step g_step = Step::Prepare;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_waitTicks = 0;
SoundInstrument* g_synth = nullptr;
Clip* g_kitClip = nullptr;
Clip* g_audioClip = nullptr;
/// The folder import legs' clips: a one-drum kit and a synth, each of its own, so no other leg's clip is changed.
Clip* g_importKitClip = nullptr;
Clip* g_importSynthClip = nullptr;
/// The import gate leg's one-drum kit.
Clip* g_importGateClip = nullptr;

/// What the import gate leg saw: whether its presses were tried, whether the import was still out when they were, and
/// whether each was refused.
bool g_imGateTried = false;
bool g_imGateWhileRunning = false;
bool g_imGateSecondRefused = false;
bool g_imGateBackRefused = false;

/// The import stall leg's reference: the two files in memory and their reason counts, and the synth's range paths,
/// before the import.
Sample* g_imStallKept[2]{};
uint32_t g_imStallKeptLeases[2]{};
std::vector<std::string> g_imStallRangePaths{};

/// Where a folder import's card work ran, read when an import leg acts.
struct ImportMark {
	int32_t listings = 0;
	int32_t loads = 0;
	int32_t pitch = 0;
	int32_t sampleIo = 0;
};
ImportMark g_importMark{};

/// Where a leg's card work ran, read when the leg acts: the claim's and the slice's loads begun on the storage owner,
/// the claim's wavetable lookups and pitch detections on the owner, and the interaction tier's blocking sample reads.
struct TierMark {
	int32_t claimLoads = 0;
	int32_t waveTableLookups = 0;
	int32_t pitchScans = 0;
	int32_t sliceLoads = 0;
	int32_t sampleIo = 0;
};
TierMark g_tierMark{};

/// Where the randomizer's card work ran, read when a randomizer leg acts.
struct RandomizeMark {
	int32_t scans = 0;
	int32_t loads = 0;
	int32_t sampleIo = 0;
};
RandomizeMark g_randomizeMark{};

/// Each randomizer row's path when the leg acted: a row the gesture must not touch keeps it.
std::string g_randomizeRowPaths[kRandomizeDrumCount];

/// Whether the gate leg's select-encoder turn moved the browser's selection while its claim was in flight.
bool g_gateMoved = false;

/// Whether the supersede leg has moved the cursor on, which it does the moment the first auto-load claim's pitch
/// detection is out on the storage owner.
bool g_supersedeFired = false;

/// The claim's counts when a leg acted.
int32_t g_giveUpsBefore = 0;
int32_t g_cancelsBefore = 0;
int32_t g_drainsBefore = 0;
int32_t g_startsUnderOpBefore = 0;
int32_t g_finishesBefore = 0;

const char* legName(Leg leg) {
	switch (leg) {
	case Leg::Synth:
		return "synth";
	case Leg::WaveTable:
		return "wavetable";
	case Leg::Fallback:
		return "fallback";
	case Leg::Gate:
		return "gate";
	case Leg::Stall:
		return "stall";
	case Leg::Supersede:
		return "supersede";
	case Leg::SupersedeLoading:
		return "supersede-loading";
	case Leg::Kit:
		return "kit";
	case Leg::AudioClip:
		return "audioclip";
	case Leg::Slice:
		return "slice";
	case Leg::SliceManual:
		return "slice-manual";
	case Leg::RandomizeOne:
		return "randomize-one";
	case Leg::RandomizeAll:
		return "randomize-all";
	case Leg::RandomizeGate:
		return "randomize-gate";
	case Leg::ImportKit:
		return "import-kit";
	case Leg::ImportMulti:
		return "import-multi";
	case Leg::ImportGate:
		return "import-gate";
	case Leg::ImportStall:
		return "import-stall";
	case Leg::RandomizeStall:
		return "randomize-stall";
	case Leg::RandomizeCancelScan:
		return "randomize-cancelscan";
	case Leg::Done:
		return "done";
	}
	return "?";
}

bool fail(const char* leg, const char* what) {
	printf("SC-SETUP FAIL leg=%s %s\n", leg, what);
	g_failures++;
	return false;
}

/// Queue @p fn on the storage owner: the fixture builds read and write the card, which belongs there.
void onOwner(void (*fn)()) {
	if (!deluge::storage::Owner::run([](void* ctx) { reinterpret_cast<void (*)()>(ctx)(); },
	                                 reinterpret_cast<void*>(fn))) {
		fail(legName(g_leg), "the storage owner's queue was full");
	}
}

ImportMark markImport() {
	return {deluge::gui::folderImport.listingsOnOwner(), deluge::gui::folderImport.loadsOnOwner(),
	        deluge::gui::folderImport.pitchOnOwner(),
	        deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoad)};
}

RandomizeMark markRandomize() {
	return {deluge::gui::randomizeDrums.scansOnOwner(), deluge::gui::randomizeDrums.loadsOnOwner(),
	        deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoad)};
}

TierMark markTier() {
	return {deluge::gui::sampleClaim.loadsOnOwner(), deluge::gui::sampleClaim.waveTableLookupsOnOwner(),
	        deluge::gui::sampleClaim.pitchScansOnOwner(), slicer.sliceLoadsOnOwner(),
	        deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoad)};
}

/// The wavetable lookups and pitch detections each leg's claim makes, every one of them on the storage owner.
void wantOwnerWork(Leg leg, int32_t& waveTable, int32_t& pitch) {
	switch (leg) {
	case Leg::Synth:
	case Leg::Gate:
		waveTable = 0;
		pitch = 1;
		return;
	case Leg::WaveTable:
		waveTable = 1;
		pitch = 0;
		return;
	case Leg::Fallback:
		waveTable = 1;
		pitch = 1;
		return;
	case Leg::Supersede:
	case Leg::SupersedeLoading:
		// The cancelled claim's detection, which still runs, and the next claim's.
		waveTable = 0;
		pitch = 2;
		return;
	default:
		waveTable = 0;
		pitch = 0;
		return;
	}
}

/// SC-TIER: since the leg acted, no claim or slice may have begun its load on the storage owner, its wavetable lookups
/// and pitch detections must all have run there, and no sample may have been read from the card on the interaction
/// tier.
void checkTier() {
	const TierMark now = markTier();
	const int32_t loads = (now.claimLoads - g_tierMark.claimLoads) + (now.sliceLoads - g_tierMark.sliceLoads);
	const int32_t waveTable = now.waveTableLookups - g_tierMark.waveTableLookups;
	const int32_t pitch = now.pitchScans - g_tierMark.pitchScans;
	const int32_t sampleIo = now.sampleIo - g_tierMark.sampleIo;
	printf("SC-TIER leg=%s loadsOnOwner=%d waveTableOnOwner=%d pitchOnOwner=%d sampleIo=%d\n", legName(g_leg), loads,
	       waveTable, pitch, sampleIo);
	int32_t wantWaveTable = 0;
	int32_t wantPitch = 0;
	wantOwnerWork(g_leg, wantWaveTable, wantPitch);
	if (loads != 0) {
		printf(
		    "SC-TIER FAIL leg=%s %d load(s) began on the storage owner -- a claim's or a slice's load belongs on the "
		    "interaction tier\n",
		    legName(g_leg), loads);
		g_failures++;
	}
	if (waveTable != wantWaveTable || pitch != wantPitch) {
		printf("SC-TIER FAIL leg=%s wavetable lookups on the owner %d (want %d), pitch detections on the owner %d "
		       "(want %d) -- they read the card synchronously and belong on the owner\n",
		       legName(g_leg), waveTable, wantWaveTable, pitch, wantPitch);
		g_failures++;
	}
	if (sampleIo != 0) {
		printf("SC-TIER FAIL leg=%s %d sample read(s) went to the card on the interaction tier\n", legName(g_leg),
		       sampleIo);
		g_failures++;
	}
}

AudioFileHolder* firstHolder(Source& source) {
	MultiRange* range = source.getOrCreateFirstRange();
	return (range != nullptr) ? range->getAudioFileHolder() : nullptr;
}

// ── Fixture construction ─────────────────────────────────────────────────────

/// A sample drum, the way the slicer builds one, added to @p kit.
SoundDrum* addSampleDrum(Kit& kit, const char* name, const char* samplePath) {
	ParamManagerForTimeline paramManager;
	if (paramManager.setupWithPatching() != Error::NONE) {
		fail("kit", "a drum's ParamManager could not be set up");
		return nullptr;
	}
	void* memory = deluge::memory::alloc_fast(sizeof(SoundDrum));
	if (memory == nullptr) {
		fail("kit", "no memory for a drum");
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
		fail("kit", "a drum has no sample range");
		return nullptr;
	}
	holder->filePath = samplePath;
	currentSong->backUpParamManager(drum, nullptr, &paramManager, true);
	return drum;
}

/// The one-drum kit preset, written through the firmware serialiser. The kit is leaked: it is never added to the song.
/// Write a kit preset of @p count drums to @p path through the firmware's own serialiser, each drum named and
/// pointed at a sample by @p drums.
struct KitPresetDrum {
	const char* name;
	const char* samplePath;
};
bool writeKitPreset(const char* path, const KitPresetDrum* drums, int32_t count) {
	ParamManagerForTimeline kitParams;
	Instrument* kitBase = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	if (kitBase == nullptr) {
		return fail("kit", "no kit for the preset");
	}
	auto* kit = static_cast<Kit*>(kitBase);
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(kit->toModControllable()), nullptr, &kitParams,
	                                true);
	for (int32_t i = 0; i < count; i++) {
		if (addSampleDrum(*kit, drums[i].name, drums[i].samplePath) == nullptr) {
			return false;
		}
	}
	Error error = StorageManager::createXMLFile(path, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return fail("kit", "the kit preset file could not be created");
	}
	kit->writeToFile(nullptr, currentSong);
	error = GetSerializer().closeFileAfterWriting(path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", "\n</kit>\n");
	return error == Error::NONE || fail("kit", "the kit preset file could not be closed");
}

/// A blank synth preset, written through the firmware's own new-synth and save paths (never hand-written: a
/// content-empty preset corrupts the heap on the load that rejects it). The synth is leaked: it is never added to the
/// song.
bool writeSynthPreset(const char* path) {
	ParamManagerForTimeline paramManager;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (instrument == nullptr) {
		return fail("import-multi", "no synth for the preset");
	}
	static_cast<SoundInstrument*>(instrument)->setupAsBlankSynth(&paramManager, false);
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(instrument->toModControllable()), nullptr,
	                                &paramManager, true);
	Error error = StorageManager::createXMLFile(path, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return fail("import-multi", "the synth preset file could not be created");
	}
	instrument->writeToFile(nullptr, currentSong);
	error = GetSerializer().closeFileAfterWriting(path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", "\n</sound>\n");
	return error == Error::NONE || fail("import-multi", "the synth preset file could not be closed");
}

// ── Driving the browser ──────────────────────────────────────────────────────

/// Tick the current UI while a browser listing runs, and say whether it still is. Nothing else in the headless harness
/// ticks a browser.
bool listingBusy() {
	if (Browser::isListingInProgress()) {
		getCurrentUI()->graphicsRoutine();
	}
	return Browser::isListingInProgress();
}

/// Step a pad scroll the browser started (a preview's entry animation) to its end, as PadLEDs::timerRoutine() does on
/// the device, where the scroll ends in the current UI's scrollFinished() within a few milliseconds. Nothing steps it
/// headless, and until it ends the browser's Select refuses a press.
void finishScroll() {
	int32_t steps = 0;
	while (isUIModeActive(UI_MODE_HORIZONTAL_SCROLL) && steps <= kDisplayWidth + kSideBarWidth) {
		PadLEDs::horizontal::renderScroll();
		steps++;
	}
	printf("SC-PROBE leg=%s scroll steps=%d modeAfter=%d\n", legName(g_leg), steps, currentUIMode);
}

/// The Select encoder's press and release on the current UI, as a user's short press delivers them.
void pressSelect() {
	using namespace deluge::hid::button;
	(void)getCurrentUI()->buttonAction(SELECT_ENC, true, false);
	(void)getCurrentUI()->buttonAction(SELECT_ENC, false, false);
}

/// Make @p clip current under @p rootUI, set the sound editor up on its first source's file selector, and open the
/// sample browser: the gesture a user makes to pick a sample.
bool openBrowser(const char* leg, Clip* clip, UI* rootUI, SoundDrum* drum = nullptr) {
	currentSong->setCurrentClip(clip);
	changeRootUI(rootUI);
	if (drum != nullptr) {
		// Selecting a drum is refused while the sound editor or the browser is the current UI, so it happens here, with
		// only the clip view open, as a user auditions a row before opening its file selector.
		auto* kit = static_cast<Kit*>(clip->output);
		static_cast<InstrumentClip*>(clip)->affectEntire = false;
		instrumentClipView.setSelectedDrum(drum, /*shouldRedrawStuff=*/false, kit, /*shouldSendMidiFeedback=*/false);
		printf("SC-PROBE leg=%s rootOnTop=%d selected=%d\n", leg, getCurrentUI() == rootUI ? 1 : 0,
		       kit->selectedDrum == drum ? 1 : 0);
	}
	const bool setup = soundEditor.setup(clip, &deluge::gui::menu_item::file0SelectorMenu, 0);
	if (setup && clip->type != ClipType::AUDIO && soundEditor.currentMultiRange == nullptr
	    && !soundEditor.currentSource->ranges.empty()) {
		// A sound with several ranges sends its file selector to the range menu first; a user picks a range there
		// before the browser opens, and the browser reads the range it opens on.
		soundEditor.setCurrentMultiRange(0);
		printf("SC-PROBE leg=%s rangePicked=0\n", leg);
	}
	const bool opened = setup && openUI(&sampleBrowser);
	printf("SC-SETUP leg=%s setup=%d opened=%d browserOnTop=%d\n", leg, setup ? 1 : 0, opened ? 1 : 0,
	       getCurrentUI() == &sampleBrowser ? 1 : 0);
	return opened || fail(leg, "the sample browser could not be opened");
}

/// Remember every randomizer row's path, so a row the gesture must leave alone can be shown to still hold it.
void recordRandomizeRows() {
	int32_t index = 0;
	for (Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum; drum != nullptr && index < kRandomizeDrumCount;
	     drum = drum->next, index++) {
		AudioFileHolder* holder = firstHolder(static_cast<SoundDrum*>(drum)->sources[0]);
		g_randomizeRowPaths[index] = (holder != nullptr) ? holder->filePath : "";
	}
}

SoundDrum* kitDrum() {
	if (g_kitClip == nullptr || g_kitClip->output == nullptr || g_kitClip->output->type != OutputType::KIT) {
		return nullptr;
	}
	Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum;
	return (drum != nullptr && drum->type == DrumType::SOUND) ? static_cast<SoundDrum*>(drum) : nullptr;
}

/// The randomizer kit's clip current under the clip view, with @p drum selected when one is given. No browser: the
/// gesture is a pad press in the clip view itself.
bool openRandomizeLeg(const char* leg, SoundDrum* drum) {
	if (g_kitClip == nullptr || g_kitClip->output == nullptr || g_kitClip->output->type != OutputType::KIT) {
		return fail(leg, "the randomizer kit clip is missing");
	}
	currentSong->setCurrentClip(g_kitClip);
	changeRootUI(&instrumentClipView);
	auto* kit = static_cast<Kit*>(g_kitClip->output);
	static_cast<InstrumentClip*>(g_kitClip)->affectEntire = false;
	if (drum != nullptr) {
		instrumentClipView.setSelectedDrum(drum, /*shouldRedrawStuff=*/false, kit, /*shouldSendMidiFeedback=*/false);
	}
	const bool onTop = getCurrentUI() == &instrumentClipView;
	printf("SC-SETUP leg=%s clipViewOnTop=%d selected=%d mode=%d\n", leg, onTop ? 1 : 0,
	       (drum != nullptr && kit->selectedDrum == drum) ? 1 : 0, (int)currentUIMode);
	return onTop || fail(leg, "the clip view could not be made current");
}

/// The kit clip current, its first drum selected, and the browser opened on that drum.
bool openBrowserOnKitDrum(const char* leg) {
	SoundDrum* drum = kitDrum();
	if (drum == nullptr) {
		return fail(leg, "the kit clip has no sound drum");
	}
	return openBrowser(leg, g_kitClip, &instrumentClipView, drum);
}

/// The boot song's synth clip current, its first oscillator set to @p osc, and the browser opened on it.
bool openBrowserOnSynth(const char* leg, OscType osc) {
	for (Output* output = currentSong->firstOutput; output != nullptr && g_synth == nullptr; output = output->next) {
		if (output->type == OutputType::SYNTH) {
			g_synth = static_cast<SoundInstrument*>(output);
		}
	}
	Clip* clip = (g_synth != nullptr) ? currentSong->getClipWithOutput(g_synth) : nullptr;
	if (clip == nullptr) {
		return fail(leg, "the boot song has no synth clip");
	}
	g_synth->sources[0].setOscType(osc);
	return openBrowser(leg, clip, &instrumentClipView);
}

// ── The legs ─────────────────────────────────────────────────────────────────

void prepareLeg() {
	switch (g_leg) {
	case Leg::Kit:
		onOwner([] {
			const KitPresetDrum presetDrum{.name = kKitDrumName, .samplePath = kKitPresetSample};
			if (writeKitPreset(kKitPresetPath, &presetDrum, 1)) {
				// The new-kit-track gesture, which takes the first kit preset nobody in the song is using:
				// kKitPresetPath.
				g_kitClip = deluge::harness::createNewInstrumentClip(OutputType::KIT, 1);
			}
			printf("SC-PROBE leg=kit prepared kitClip=%d\n", g_kitClip != nullptr ? 1 : 0);
		});
		break;
	case Leg::AudioClip:
		onOwner([] { g_audioClip = sessionView.createNewClip(OutputType::AUDIO, 2); });
		break;
	case Leg::RandomizeOne:
		onOwner([] {
			// A kit of its own, whose drums all name the randomizer folder's first file: the randomizer picks from the
			// folder a drum's sample sits in, and every other fixture is outside it.
			KitPresetDrum drums[kRandomizeDrumCount];
			for (int32_t i = 0; i < kRandomizeDrumCount; i++) {
				drums[i] = {.name = kRandomizeDrums[i].name, .samplePath = kRandomizeFirstSample};
			}
			if (!writeKitPreset(kRandomizeKitPresetPath, drums, kRandomizeDrumCount)) {
				return;
			}
			g_kitClip = deluge::harness::createNewInstrumentClip(OutputType::KIT, 4);
			if (g_kitClip == nullptr || g_kitClip->output == nullptr || g_kitClip->output->type != OutputType::KIT) {
				fail("randomize-one", "no kit clip could be created from the randomizer preset");
				return;
			}
			// The rows a user would have: notes on three, one of them muted, and one row left empty. An affect-entire
			// randomize walks unmuted rows that have notes, so only the first two are eligible.
			auto* kitClip = static_cast<InstrumentClip*>(g_kitClip);
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, kitClip);
			int32_t drumIndex = 0;
			int32_t rowsWithNotes = 0;
			for (Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum;
			     drum != nullptr && drumIndex < kRandomizeDrumCount; drum = drum->next, drumIndex++) {
				ModelStackWithNoteRow* rowStack = kitClip->getNoteRowForDrum(modelStack, drum);
				NoteRow* row = (rowStack != nullptr) ? rowStack->getNoteRowAllowNull() : nullptr;
				if (row == nullptr) {
					fail("randomize-one", "a randomizer kit drum has no note row");
					return;
				}
				if (kRandomizeDrums[drumIndex].note) {
					if (row->attemptNoteAdd(0, 1, 64, row->getDefaultProbability(), row->getDefaultIterance(),
					                        row->getDefaultFill(rowStack), rowStack, nullptr)
					    == 0) {
						fail("randomize-one", "a note could not be added to a randomizer kit row");
						return;
					}
					rowsWithNotes++;
				}
				row->muted = kRandomizeDrums[drumIndex].muted;
			}
			// The gesture is refused outright while the community feature is off.
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::DrumRandomizer, RuntimeFeatureStateToggle::On);
			printf("SC-PROBE leg=randomize-one prepared kitClip=%d drums=%d rowsWithNotes=%d feature=%d\n",
			       g_kitClip != nullptr ? 1 : 0, drumIndex, rowsWithNotes,
			       runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::DrumRandomizer) ? 1 : 0);
		});
		break;
	case Leg::RandomizeCancelScan:
		onOwner([] {
			// The legs before this one left the first drum on the stall folder. Back to the randomizer's own folder,
			// unbound: a cancelled walk must leave both exactly as they are, so both are what the check reads.
			SoundDrum* drum = kitDrum();
			AudioFileHolder* holder = (drum != nullptr) ? firstHolder(drum->sources[0]) : nullptr;
			if (holder == nullptr) {
				fail("randomize-cancelscan", "the randomizer kit's first drum has no holder");
				return;
			}
			holder->setAudioFile(nullptr);
			holder->filePath = kRandomizeFirstSample;
			drum->drumName = kRandomizeDrums[0].name;
		});
		break;
	case Leg::RandomizeGate:
		onOwner([] {
			SoundDrum* drum = kitDrum();
			AudioFileHolder* holder = (drum != nullptr) ? firstHolder(drum->sources[0]) : nullptr;
			if (holder == nullptr) {
				fail("randomize-gate", "the randomizer kit's first drum has no holder");
				return;
			}
			holder->setAudioFile(nullptr);
			holder->filePath = kRandomizeGateSample;
		});
		break;
	case Leg::RandomizeStall:
		onOwner([] {
			if (g_kitClip == nullptr || g_kitClip->output == nullptr || g_kitClip->output->type != OutputType::KIT) {
				fail("randomize-stall", "the randomizer kit clip is missing");
				return;
			}
			Drum* first = static_cast<Kit*>(g_kitClip->output)->firstDrum;
			Drum* second = (first != nullptr) ? first->next : nullptr;
			Drum* third = (second != nullptr) ? second->next : nullptr;
			AudioFileHolder* kept =
			    (first != nullptr) ? firstHolder(static_cast<SoundDrum*>(first)->sources[0]) : nullptr;
			AudioFileHolder* held =
			    (second != nullptr) ? firstHolder(static_cast<SoundDrum*>(second)->sources[0]) : nullptr;
			AudioFileHolder* after =
			    (third != nullptr) ? firstHolder(static_cast<SoundDrum*>(third)->sources[0]) : nullptr;
			NoteRow* afterRow =
			    (third != nullptr) ? static_cast<InstrumentClip*>(g_kitClip)->getNoteRowForDrum(third) : nullptr;
			if (kept == nullptr || held == nullptr || after == nullptr || afterRow == nullptr) {
				fail("randomize-stall", "the randomizer kit's first three drums have no holders or rows");
				return;
			}
			kept->setAudioFile(nullptr);
			kept->filePath = kRandomizeStallKept;
			// Loaded now, before the hold: the first row's only choice is already in memory when the walk reaches it.
			const Error error =
			    kept->loadFile(/*reversed=*/false, /*manuallySelected=*/true, /*mayActuallyReadFile=*/true);
			held->setAudioFile(nullptr);
			held->filePath = kRandomizeStallSample;
			// The third row: eligible now, and pointing at the first row's file, already in memory.
			after->setAudioFile(nullptr);
			after->filePath = kRandomizeStallKept;
			const Error afterError =
			    after->loadFile(/*reversed=*/false, /*manuallySelected=*/true, /*mayActuallyReadFile=*/true);
			afterRow->muted = false;
			printf("SC-PROBE leg=randomize-stall prepared keptBound=%d error=%d afterBound=%d afterError=%d\n",
			       kept->audioFile != nullptr ? 1 : 0, static_cast<int>(error), after->audioFile != nullptr ? 1 : 0,
			       static_cast<int>(afterError));
		});
		break;
	case Leg::ImportKit:
		onOwner([] {
			// A one-drum kit of its own: the kit menu offers "Load All" only into a kit with one drum.
			const KitPresetDrum presetDrum{.name = kImportKitDrumName, .samplePath = kKitPresetSample};
			if (writeKitPreset(kImportKitPresetPath, &presetDrum, 1)) {
				g_importKitClip = deluge::harness::createNewInstrumentClip(OutputType::KIT, 5);
			}
			printf("SC-PROBE leg=import-kit prepared kitClip=%d\n", g_importKitClip != nullptr ? 1 : 0);
		});
		break;
	case Leg::ImportMulti:
		onOwner([] {
			// A synth of its own, from a preset of its own: the multisample import replaces its source's ranges, and
			// the new-synth-track gesture needs a synth preset nobody in the song is using.
			if (writeSynthPreset(kImportSynthPresetPath)) {
				g_importSynthClip = deluge::harness::createNewInstrumentClip(OutputType::SYNTH, 6);
			}
			printf("SC-PROBE leg=import-multi prepared synthClip=%d type=%d\n", g_importSynthClip != nullptr ? 1 : 0,
			       (g_importSynthClip != nullptr && g_importSynthClip->output != nullptr)
			           ? static_cast<int>(g_importSynthClip->output->type)
			           : -1);
		});
		break;
	case Leg::ImportGate:
		onOwner([] {
			// A one-drum kit of its own: the import kit leg's kit holds three drums by now.
			const KitPresetDrum presetDrum{.name = kImportGateDrumName, .samplePath = kKitPresetSample};
			if (writeKitPreset(kImportGatePresetPath, &presetDrum, 1)) {
				g_importGateClip = deluge::harness::createNewInstrumentClip(OutputType::KIT, 7);
			}
			printf("SC-PROBE leg=import-gate prepared kitClip=%d\n", g_importGateClip != nullptr ? 1 : 0);
		});
		break;
	case Leg::ImportStall:
		onOwner([] {
			// The first and last files in memory before the hold, and the synth's ranges as the multisample leg left
			// them.
			Error error = Error::NONE;
			const char* keptPaths[2] = {kImportStallKept, kImportStallKeptLast};
			for (int32_t i = 0; i < 2; i++) {
				std::string keptPath{keptPaths[i]};
				g_imStallKept[i] = static_cast<Sample*>(
				    audioFileManager.getAudioFileFromFilename(keptPath, true, &error, AudioFileType::SAMPLE));
				g_imStallKeptLeases[i] = (g_imStallKept[i] != nullptr) ? g_imStallKept[i]->leaseCount() : 0;
			}
			g_imStallRangePaths.clear();
			if (g_importSynthClip != nullptr && g_importSynthClip->output != nullptr) {
				Source& source = static_cast<SoundInstrument*>(g_importSynthClip->output)->sources[0];
				for (int32_t i = 0; i < std::ssize(source.ranges); i++) {
					g_imStallRangePaths.push_back(
					    static_cast<MultisampleRange*>(source.ranges.getElement(i))->sampleHolder.filePath);
				}
			}
			printf("SC-PROBE leg=import-stall prepared kept=%d leases=%u,%u ranges=%d error=%d\n",
			       (g_imStallKept[0] != nullptr && g_imStallKept[1] != nullptr) ? 1 : 0, g_imStallKeptLeases[0],
			       g_imStallKeptLeases[1], static_cast<int>(g_imStallRangePaths.size()), static_cast<int>(error));
		});
		break;
	case Leg::SliceManual:
		onOwner([] {
			// A kit of its own: the region slice left the first with four drums.
			const KitPresetDrum presetDrum{.name = kKitDrumName, .samplePath = kKitPresetSample};
			if (writeKitPreset(kManualKitPresetPath, &presetDrum, 1)) {
				g_kitClip = deluge::harness::createNewInstrumentClip(OutputType::KIT, 3);
			}
			const SoundDrum* drum = kitDrum();
			int32_t drums = 0;
			if (g_kitClip != nullptr && g_kitClip->output != nullptr && g_kitClip->output->type == OutputType::KIT) {
				for (Drum* d = static_cast<Kit*>(g_kitClip->output)->firstDrum; d != nullptr; d = d->next) {
					drums++;
				}
			}
			printf("SC-PROBE leg=slice-manual prepared kitClip=%d drums=%d name=%s\n", g_kitClip != nullptr ? 1 : 0,
			       drums, drum != nullptr ? drum->drumName.c_str() : "");
		});
		break;
	default:
		break;
	}
}

bool openLeg() {
	switch (g_leg) {
	case Leg::Synth:
		return openBrowserOnSynth("synth", OscType::SAMPLE);
	case Leg::WaveTable:
		return openBrowserOnSynth("wavetable", OscType::SAMPLE);
	case Leg::Fallback:
		// A wavetable oscillator: the claim tries the file as a wavetable first.
		return openBrowserOnSynth("fallback", OscType::WAVETABLE);
	case Leg::Gate:
		deluge_audio_load_set_latency_ms(kGateHoldMs);
		return openBrowserOnSynth("gate", OscType::SAMPLE);
	case Leg::Stall:
		deluge_audio_load_set_latency_ms(kStallHoldMs);
		return openBrowserOnSynth("stall", OscType::SAMPLE);
	case Leg::Supersede:
		return openBrowserOnSynth("supersede", OscType::SAMPLE);
	case Leg::SupersedeLoading:
		return openBrowserOnSynth("supersede-loading", OscType::SAMPLE);
	case Leg::Kit:
	case Leg::Slice:
	case Leg::SliceManual:
		return openBrowserOnKitDrum(legName(g_leg));
	case Leg::RandomizeOne:
		// The first drum, as a user selects a row before randomizing just that one.
		return openRandomizeLeg("randomize-one", kitDrum());
	case Leg::RandomizeAll:
		return openRandomizeLeg("randomize-all", nullptr);
	case Leg::RandomizeGate:
		deluge_audio_load_set_latency_ms(kGateHoldMs);
		return openRandomizeLeg("randomize-gate", kitDrum());
	case Leg::RandomizeStall:
		deluge_audio_load_set_latency_ms(kStallHoldMs);
		return openRandomizeLeg("randomize-stall", nullptr);
	case Leg::RandomizeCancelScan:
		// The DIRECTORY channel held, not the loader: this leg never reaches a load.
		deluge_file_directory_set_latency_ms(kRandomizeCancelHoldMs);
		return openRandomizeLeg("randomize-cancelscan", kitDrum());
	case Leg::ImportKit: {
		if (g_importKitClip == nullptr || g_importKitClip->output == nullptr
		    || g_importKitClip->output->type != OutputType::KIT) {
			return fail("import-kit", "no kit clip could be created from the import preset");
		}
		Drum* drum = static_cast<Kit*>(g_importKitClip->output)->firstDrum;
		if (drum == nullptr || drum->type != DrumType::SOUND) {
			return fail("import-kit", "the import kit has no sound drum");
		}
		return openBrowser("import-kit", g_importKitClip, &instrumentClipView, static_cast<SoundDrum*>(drum));
	}
	case Leg::ImportGate: {
		if (g_importGateClip == nullptr || g_importGateClip->output == nullptr
		    || g_importGateClip->output->type != OutputType::KIT) {
			return fail("import-gate", "no kit clip could be created from the import gate preset");
		}
		Drum* drum = static_cast<Kit*>(g_importGateClip->output)->firstDrum;
		if (drum == nullptr || drum->type != DrumType::SOUND) {
			return fail("import-gate", "the import gate kit has no sound drum");
		}
		return openBrowser("import-gate", g_importGateClip, &instrumentClipView, static_cast<SoundDrum*>(drum));
	}
	case Leg::ImportStall:
		if (g_importSynthClip == nullptr || g_importSynthClip->output == nullptr) {
			return fail("import-stall", "the multisample leg's synth clip is missing");
		}
		return openBrowser("import-stall", g_importSynthClip, &instrumentClipView);
	case Leg::ImportMulti:
		if (g_importSynthClip == nullptr || g_importSynthClip->output == nullptr
		    || g_importSynthClip->output->type != OutputType::SYNTH) {
			return fail("import-multi", "no synth clip could be created");
		}
		static_cast<SoundInstrument*>(g_importSynthClip->output)->sources[0].setOscType(OscType::SAMPLE);
		return openBrowser("import-multi", g_importSynthClip, &instrumentClipView);
	case Leg::AudioClip:
		if (g_audioClip == nullptr || g_audioClip->type != ClipType::AUDIO) {
			return fail("audioclip", "no audio clip could be created");
		}
		return openBrowser("audioclip", g_audioClip, &audioClipView);
	case Leg::Done:
		break;
	}
	return false;
}

const char* legFile() {
	switch (g_leg) {
	case Leg::Synth:
		return kSynthSample;
	case Leg::WaveTable:
		return kCycleSample;
	case Leg::Fallback:
		return kStereoSample;
	case Leg::Gate:
		return kGateSample;
	case Leg::Stall:
		return kStallSample;
	case Leg::Supersede:
	case Leg::SupersedeLoading:
		return kSynthSample;
	case Leg::Kit:
		return kKickSample;
	case Leg::ImportKit:
		return kImportKitFile;
	case Leg::ImportMulti:
		return kImportMultiFile;
	case Leg::ImportGate:
		return kImportGateFile;
	case Leg::ImportStall:
		return kImportStallKept;
	case Leg::AudioClip:
		return kClipSample;
	case Leg::Slice:
	case Leg::SliceManual:
		return kSliceSample;
	case Leg::RandomizeOne:
	case Leg::RandomizeAll:
	case Leg::RandomizeGate:
	case Leg::RandomizeStall:
	case Leg::RandomizeCancelScan:
		// No browser listing: the randomizer picks the file itself, from the folder the drum's sample sits in.
		return kRandomizeFirstSample;
	case Leg::Done:
		break;
	}
	return "";
}

/// The claim's Select press, or for the slice leg the slicer opened on the selected file and its commit.
/// Whether the current leg is one of the randomizer legs.
bool isRandomize() {
	return g_leg == Leg::RandomizeOne || g_leg == Leg::RandomizeAll || g_leg == Leg::RandomizeGate
	       || g_leg == Leg::RandomizeStall || g_leg == Leg::RandomizeCancelScan;
}

/// Whether the current leg is one of the two supersede legs.
bool isSupersede() {
	return g_leg == Leg::Supersede || g_leg == Leg::SupersedeLoading;
}

/// The file a supersede leg's cursor moves on to.
const char* supersedeSecond() {
	return (g_leg == Leg::Supersede) ? kSupersedeLanded : kSupersedeLoading;
}

/// What the gate leg saw: whether its presses were tried, whether the walk was still out when they were, whether each
/// was refused, and whether the edit pad was taken once the walk had ended.
bool g_rdGateTried = false;
bool g_rdGateWhileRunning = false;
bool g_rdGateSecondWalkRefused = false;
bool g_rdGateEditRefused = false;
bool g_rdGateEditTakenAfter = false;

/// What the cancel leg saw: the round it is on, how many passes that round has let through, whether its cancel has
/// been made, whether the ordinary randomize that follows the rounds has been pressed, and what the rounds left
/// behind -- all read before that last walk touches anything.
int32_t g_rdCancelRound = 0;
int32_t g_rdCancelPasses = 0;
bool g_rdCancelCancelled = false;
bool g_rdCancelFinalPressed = false;
int32_t g_rdCancelDrainsEnteredBefore = 0;
int32_t g_rdCancelDrainsCollectedBefore = 0;
int32_t g_rdCancelScansBefore = 0;
int32_t g_rdCancelDrained = 0;
bool g_rdCancelRowUntouched = false;

/// Every note in the randomizer kit clip's rows.
int32_t kitClipNotes() {
	auto* clip = static_cast<InstrumentClip*>(g_kitClip);
	int32_t notes = 0;
	for (int32_t i = 0; i < std::ssize(clip->noteRows); i++) {
		notes += static_cast<int32_t>(clip->noteRows[i].notes.size());
	}
	return notes;
}

/// The display row showing the selected drum, or -1.
int32_t selectedDrumY() {
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		NoteRow* row = static_cast<InstrumentClip*>(g_kitClip)->getNoteRowOnScreen(y, currentSong);
		if (row != nullptr && row->drum == kitDrum()) {
			return y;
		}
	}
	return -1;
}

/// One press and release of the gate leg's edit pad; whether it changed the clip's notes.
bool pressGateEditPad() {
	const int32_t y = selectedDrumY();
	if (y < 0) {
		return false;
	}
	const int32_t before = kitClipNotes();
	(void)instrumentClipView.padAction(kRandomizeGateEditX, y, 1);
	(void)instrumentClipView.padAction(kRandomizeGateEditX, y, 0);
	return kitClipNotes() != before;
}

/// The gate leg's first wait tick: while the walk is still out, the randomizer pressed again and the edit pad.
void tickRandomizeGate() {
	if (g_rdGateTried) {
		return;
	}
	g_rdGateTried = true;
	g_rdGateWhileRunning = currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	if (!g_rdGateWhileRunning) {
		return;
	}
	using namespace deluge::hid::button;
	const int32_t scansBefore = markRandomize().scans;
	(void)Buttons::buttonAction(LOAD, true, false);
	(void)instrumentClipView.padAction(15, 2, 1);
	(void)instrumentClipView.padAction(15, 2, 0);
	(void)Buttons::buttonAction(LOAD, false, false);
	g_rdGateSecondWalkRefused = markRandomize().scans == scansBefore;
	g_rdGateEditRefused = !pressGateEditPad();
}

/// Whether the current leg is one of the folder import legs.
bool isImport() {
	return g_leg == Leg::ImportKit || g_leg == Leg::ImportMulti || g_leg == Leg::ImportGate
	       || g_leg == Leg::ImportStall;
}

/// The import gate leg's first wait tick: while the import is still out, BACK and Select on the kit menu.
void tickImportGate() {
	if (g_imGateTried) {
		return;
	}
	g_imGateTried = true;
	g_imGateWhileRunning = currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	if (!g_imGateWhileRunning) {
		return;
	}
	using namespace deluge::hid::button;
	auto& menu = deluge::gui::context_menu::sample_browser::kit;
	const int32_t listingsBefore = markImport().listings;
	(void)getCurrentUI()->buttonAction(BACK, true, false);
	(void)getCurrentUI()->buttonAction(BACK, false, false);
	g_imGateBackRefused = getCurrentUI() == &menu;
	(void)getCurrentUI()->buttonAction(SELECT_ENC, true, false);
	(void)getCurrentUI()->buttonAction(SELECT_ENC, false, false);
	g_imGateSecondRefused = markImport().listings == listingsBefore;
}

/// The randomizer's gesture: LOAD held (which the randomizer reads through Buttons::isButtonPressed), AFFECT_ENTIRE
/// held too when @p affectEntire, then the pad. Buttons::buttonAction is what sets the held state a press reads.
void pressRandomize(bool affectEntire) {
	using namespace deluge::hid::button;
	(void)Buttons::buttonAction(LOAD, true, false);
	if (affectEntire) {
		(void)Buttons::buttonAction(AFFECT_ENTIRE, true, false);
	}
	(void)instrumentClipView.padAction(15, 2, 1);
	(void)instrumentClipView.padAction(15, 2, 0);
	if (affectEntire) {
		(void)Buttons::buttonAction(AFFECT_ENTIRE, false, false);
	}
	(void)Buttons::buttonAction(LOAD, false, false);
}

void actLeg() {
	if (isRandomize()) {
		g_randomizeMark = markRandomize();
		recordRandomizeRows();
		g_rdGateTried = false;
		g_rdGateWhileRunning = false;
		g_rdGateSecondWalkRefused = false;
		g_rdGateEditRefused = false;
		g_rdGateEditTakenAfter = false;
		g_rdCancelRound = 0;
		g_rdCancelPasses = 0;
		g_rdCancelCancelled = false;
		g_rdCancelFinalPressed = false;
		g_rdCancelDrained = 0;
		g_rdCancelRowUntouched = false;
		g_rdCancelDrainsEnteredBefore = deluge::gui::randomizeDrums.drainsEntered();
		g_rdCancelDrainsCollectedBefore = deluge::gui::randomizeDrums.drainsCollected();
		g_rdCancelScansBefore = deluge::gui::randomizeDrums.scansRun();
		pressRandomize(g_leg == Leg::RandomizeAll || g_leg == Leg::RandomizeStall);
		return;
	}
	finishScroll();
	g_tierMark = markTier();
	g_giveUpsBefore = deluge::gui::sampleClaim.giveUps();
	g_cancelsBefore = deluge::gui::sampleClaim.cancels();
	g_drainsBefore = deluge::gui::sampleClaim.drains();
	g_startsUnderOpBefore = deluge::gui::sampleClaim.startsUnderOp();
	g_finishesBefore = deluge::gui::sampleClaim.finishes();
	if (isSupersede()) {
		using namespace deluge::hid::button;
		// Auto-load on (the Load button toggles it as it is released), then the file's preview asked for again, as the
		// cursor arriving on it asks: its landing asks for the auto-load claim.
		g_supersedeFired = false;
		(void)getCurrentUI()->buttonAction(LOAD, false, false);
		sampleBrowser.previewFile(legFile(), 0);
		return;
	}
	if (g_leg == Leg::Gate) {
		const auto selected = [] {
			const FileItem* item = sampleBrowser.getCurrentFileItem();
			return std::string{item != nullptr ? item->filename.c_str() : ""};
		};
		const std::string before = selected();
		pressSelect();
		// Browser input while the claim is in flight: the select encoder, turned back one file.
		getCurrentUI()->selectEncoderAction(-1);
		const std::string after = selected();
		g_gateMoved = (after != before);
		printf("SC-PROBE leg=gate before=%s after=%s\n", before.c_str(), after.c_str());
		return;
	}
	if (isImport()) {
		using namespace deluge::hid::button;
		g_importMark = markImport();
		g_imGateTried = false;
		g_imGateWhileRunning = false;
		g_imGateSecondRefused = false;
		g_imGateBackRefused = false;
		// The held legs hold the loader only now: the browser's own preview of the selected file is not held.
		if (g_leg == Leg::ImportGate) {
			deluge_audio_load_set_latency_ms(kGateHoldMs);
		}
		else if (g_leg == Leg::ImportStall) {
			deluge_audio_load_set_latency_ms(kStallHoldMs);
		}
		// A long press on Select opens the context menu for the kind of instrument the browser is choosing for.
		(void)getCurrentUI()->buttonAction(SELECT_ENC, true, false);
		(void)static_cast<UI&>(sampleBrowser).timerCallback();
		deluge::gui::ContextMenu* menu =
		    (g_leg == Leg::ImportKit || g_leg == Leg::ImportGate)
		        ? static_cast<deluge::gui::ContextMenu*>(&deluge::gui::context_menu::sample_browser::kit)
		        : &deluge::gui::context_menu::sample_browser::synth;
		const bool menuOnTop = getCurrentUI() == menu;
		menu->currentOption = kImportOption;
		printf("SC-SETUP leg=%s menuOnTop=%d\n", legName(g_leg), menuOnTop ? 1 : 0);
		(void)getCurrentUI()->buttonAction(SELECT_ENC, true, false);
		return;
	}
	if (g_leg == Leg::WaveTable) {
		using namespace deluge::hid::button;
		// A long press on Select: the press, then what the UI timer's callback does once it has been held.
		(void)getCurrentUI()->buttonAction(SELECT_ENC, true, false);
		(void)static_cast<UI&>(sampleBrowser).timerCallback();
		auto& menu = deluge::gui::context_menu::sample_browser::synth;
		const bool menuOnTop = getCurrentUI() == &menu;
		menu.currentOption = kWaveTableOption;
		printf("SC-SETUP leg=wavetable menuOnTop=%d\n", menuOnTop ? 1 : 0);
		(void)getCurrentUI()->buttonAction(SELECT_ENC, true, false);
		return;
	}
	if (g_leg == Leg::SliceManual) {
		using namespace deluge::hid::button;
		const bool opened = openUI(&slicer);
		// The mode button, as a user switches the slicer to manual; the points as pad presses on a playing voice add
		// them, which a scenario cannot sound.
		(void)slicer.buttonAction(X_ENC, true, false);
		for (int32_t i = 0; i < kManualSliceCount; i++) {
			slicer.manualSlicePoints[i] = kManualSlices[i];
		}
		slicer.numManualSlice = kManualSliceCount;
		slicer.currentSlice = 0;
		printf("SC-SETUP leg=slice-manual slicerOpened=%d slicerOnTop=%d manual=%d\n", opened ? 1 : 0,
		       getCurrentUI() == &slicer ? 1 : 0, slicer.slicerMode == SLICER_MODE_MANUAL ? 1 : 0);
		(void)slicer.buttonAction(SELECT_ENC, true, false);
		// The live points change before the slice runs, as an encoder turn or a pad press can while its load is out:
		// the drums must still be painted from the points as they were when Select was pressed.
		slicer.manualSlicePoints[0].transpose = 24;
		slicer.manualSlicePoints[1].startPos = 5;
		slicer.numManualSlice = 1;
		return;
	}
	if (g_leg != Leg::Slice) {
		pressSelect();
		return;
	}
	// What the browser's Shift+Select does for a brand-new kit (see SampleBrowser::enterKeyPress).
	const bool opened = openUI(&slicer);
	slicer.numClips = kSliceRegions;
	printf("SC-SETUP leg=slice slicerOpened=%d slicerOnTop=%d\n", opened ? 1 : 0, getCurrentUI() == &slicer ? 1 : 0);
	using namespace deluge::hid::button;
	(void)slicer.buttonAction(SELECT_ENC, true, false);
}

/// Whether the leg's claim or slice has ended: a claim that succeeds closes the browser, and a slice closes the slicer.
bool legEnded() {
	if (g_leg == Leg::ImportGate) {
		return g_imGateTried && currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	}
	if (g_leg == Leg::ImportStall) {
		// An import that aborts closes just the menu and releases the gate; one that finishes closes the browser too.
		return currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES
		       && getCurrentUI() != &deluge::gui::context_menu::sample_browser::synth;
	}
	if (g_leg == Leg::RandomizeCancelScan) {
		// Every round releases the gate as it cancels, so the mode says nothing until the ordinary randomize the
		// rounds are there to set up: the leg ends when THAT one has ended.
		return g_rdCancelFinalPressed && currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	}
	if (isRandomize()) {
		// The gesture closes the gate and the walk releases it: the leg ends when the mode is gone -- for the gate leg,
		// once its presses have been tried.
		return (g_leg != Leg::RandomizeGate || g_rdGateTried)
		       && currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	}
	if (isSupersede()) {
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		return g_supersedeFired && !deluge::gui::sampleClaim.inFlight() && holder != nullptr
		       && holder->audioFile != nullptr && holder->filePath == supersedeSecond();
	}
	if (g_leg == Leg::Slice || g_leg == Leg::SliceManual) {
		return !isUIOpen(&slicer);
	}
	if (g_leg == Leg::Stall) {
		// A claim that gives up leaves the browser open and releases the gate; one that binds closes the browser.
		return !isUIOpen(&sampleBrowser) || currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	}
	return !isUIOpen(&sampleBrowser);
}

void checkLeg() {
	checkTier();
	switch (g_leg) {
	case Leg::Synth: {
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		printf("SC-SYNTH bound=%d path=%s osc=%d repeat=%d transpose=%d ticks=%d\n", bound ? 1 : 0,
		       holder != nullptr ? holder->filePath.c_str() : "", static_cast<int>(g_synth->sources[0].oscType),
		       static_cast<int>(g_synth->sources[0].repeatMode),
		       holder != nullptr ? static_cast<SampleHolderForVoice*>(holder)->transpose : 0, g_waitTicks);
		if (!bound) {
			printf("SC-SYNTH FAIL the claim bound no file\n");
			g_failures++;
		}
		break;
	}
	case Leg::Gate: {
		deluge_audio_load_set_latency_ms(0);
		// Released is the gate's own mode gone: a preview's scroll may hold a mode of its own.
		const bool released = (currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES);
		printf("SC-GATE input=selectEncoder refused=%d released=%d ticks=%d\n", g_gateMoved ? 0 : 1, released ? 1 : 0,
		       g_waitTicks);
		if (g_gateMoved) {
			printf("SC-GATE FAIL the select encoder moved the browser's selection while an explicit claim was in "
			       "flight\n");
			g_failures++;
		}
		if (!released) {
			printf("SC-GATE FAIL the gate was still closed once the claim had ended\n");
			g_failures++;
		}
		break;
	}
	case Leg::Stall: {
		deluge_audio_load_set_latency_ms(0);
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		const bool browserOpen = isUIOpen(&sampleBrowser);
		const bool released = (currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES);
		const int32_t giveUps = deluge::gui::sampleClaim.giveUps() - g_giveUpsBefore;
		printf("SC-STALL bound=%d browserOpen=%d released=%d giveUps=%d ticks=%d\n", bound ? 1 : 0, browserOpen ? 1 : 0,
		       released ? 1 : 0, giveUps, g_waitTicks);
		if (bound || !browserOpen || !released || giveUps != 1) {
			printf("SC-STALL FAIL a claim whose load never answered did not give up: it must leave the holder unbound, "
			       "the browser open and the gate released\n");
			g_failures++;
		}
		break;
	}
	case Leg::WaveTable:
	case Leg::Fallback: {
		const bool wantWaveTable = (g_leg == Leg::WaveTable);
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		const OscType osc = g_synth->sources[0].oscType;
		printf("SC-%s bound=%d path=%s osc=%d modKnobMode=%d ticks=%d\n", wantWaveTable ? "WAVETABLE" : "FALLBACK",
		       bound ? 1 : 0, holder != nullptr ? holder->filePath.c_str() : "", static_cast<int>(osc),
		       static_cast<int>(g_synth->modKnobMode), g_waitTicks);
		if (!bound || osc != (wantWaveTable ? OscType::WAVETABLE : OscType::SAMPLE)) {
			printf("SC-%s FAIL the claim did not bind the file as a %s\n", wantWaveTable ? "WAVETABLE" : "FALLBACK",
			       wantWaveTable ? "wavetable" : "sample");
			g_failures++;
		}
		break;
	}
	case Leg::Supersede:
	case Leg::SupersedeLoading: {
		deluge_audio_load_set_latency_ms(0);
		const auto& claim = deluge::gui::sampleClaim;
		const char* supersedeCase = (g_leg == Leg::Supersede) ? "landed" : "loading";
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const int32_t cancels = claim.cancels() - g_cancelsBefore;
		const int32_t drains = claim.drains() - g_drainsBefore;
		const int32_t startsUnderOp = claim.startsUnderOp() - g_startsUnderOpBefore;
		const int32_t finishes = claim.finishes() - g_finishesBefore;
		const bool boundSecond =
		    holder != nullptr && holder->audioFile != nullptr && holder->filePath == supersedeSecond();
		printf("SC-SUPERSEDE case=%s underOp=%d cancels=%d drains=%d startsUnderOp=%d finishes=%d boundSecond=%d "
		       "ticks=%d\n",
		       supersedeCase, g_supersedeFired ? 1 : 0, cancels, drains, startsUnderOp, finishes, boundSecond ? 1 : 0,
		       g_waitTicks);
		if (!g_supersedeFired || cancels != 1 || drains != 1 || startsUnderOp != 0 || finishes != 1 || !boundSecond) {
			printf("SC-SUPERSEDE FAIL case=%s an auto-load claim the cursor left with its pitch detection out must be "
			       "cancelled once and drain before the next claim starts, and only the next file's claim may finish\n",
			       supersedeCase);
			g_failures++;
		}
		// Auto-load off again, and out of the browser, which auto-load leaves open.
		(void)getCurrentUI()->buttonAction(deluge::hid::button::LOAD, false, false);
		sampleBrowser.exitAndNeverDeleteDrum();
		break;
	}
	case Leg::Kit: {
		SoundDrum* drum = kitDrum();
		AudioFileHolder* holder = (drum != nullptr) ? firstHolder(drum->sources[0]) : nullptr;
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		printf("SC-KIT bound=%d path=%s repeat=%d name=%s sideChain=%d ticks=%d\n", bound ? 1 : 0,
		       holder != nullptr ? holder->filePath.c_str() : "",
		       drum != nullptr ? static_cast<int>(drum->sources[0].repeatMode) : -1,
		       drum != nullptr ? drum->drumName.c_str() : "", drum != nullptr ? drum->sideChainSendLevel : 0,
		       g_waitTicks);
		if (!bound) {
			printf("SC-KIT FAIL the claim bound no file\n");
			g_failures++;
		}
		break;
	}
	case Leg::AudioClip: {
		auto* clip = static_cast<AudioClip*>(g_audioClip);
		const bool bound = clip->sampleHolder.audioFile != nullptr;
		printf("SC-AUDIOCLIP bound=%d path=%s loopLength=%d attack=%d ticks=%d\n", bound ? 1 : 0,
		       clip->sampleHolder.filePath.c_str(), clip->loopLength, clip->attack, g_waitTicks);
		if (!bound) {
			printf("SC-AUDIOCLIP FAIL the claim bound no file\n");
			g_failures++;
		}
		break;
	}
	case Leg::Slice: {
		int32_t drums = 0;
		int32_t boundToSlice = 0;
		for (Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum; drum != nullptr; drum = drum->next) {
			drums++;
			if (drum->type != DrumType::SOUND) {
				continue;
			}
			AudioFileHolder* holder = firstHolder(static_cast<SoundDrum*>(drum)->sources[0]);
			if (holder != nullptr && holder->audioFile != nullptr && holder->filePath == kSliceSample) {
				boundToSlice++;
			}
		}
		auto* clip = static_cast<InstrumentClip*>(g_kitClip);
		int32_t rowsWithDrum = 0;
		for (int32_t i = 0; i < std::ssize(clip->noteRows); i++) {
			if (clip->noteRows[i].drum != nullptr) {
				rowsWithDrum++;
			}
		}
		printf("SC-SLICE drums=%d boundToSlice=%d rowsWithDrum=%d ticks=%d\n", drums, boundToSlice, rowsWithDrum,
		       g_waitTicks);
		if (drums != kSliceRegions || boundToSlice != kSliceRegions || rowsWithDrum < kSliceRegions) {
			printf("SC-SLICE FAIL the slice did not end with %d drums on the sliced sample, each with a NoteRow\n",
			       kSliceRegions);
			g_failures++;
		}
		break;
	}
	case Leg::SliceManual: {
		auto* kit = static_cast<Kit*>(g_kitClip->output);
		int32_t drums = 0;
		for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
			drums++;
		}
		int32_t boundToSlice = 0;
		int32_t painted = 0;
		for (int32_t i = 0; i < kManualSliceCount && i < drums; i++) {
			Drum* drum = kit->getDrumFromIndex(i);
			if (drum->type != DrumType::SOUND) {
				continue;
			}
			auto* range =
			    static_cast<MultisampleRange*>(static_cast<SoundDrum*>(drum)->sources[0].getOrCreateFirstRange());
			if (range == nullptr || range->sampleHolder.audioFile == nullptr
			    || !(range->sampleHolder.filePath == kSliceSample)) {
				continue;
			}
			boundToSlice++;
			const int64_t length = static_cast<Sample*>(range->sampleHolder.audioFile)->lengthInSamples;
			const int64_t wantEnd = (i == kManualSliceCount - 1) ? length : kManualSlices[i + 1].startPos;
			const bool match = range->sampleHolder.startPos == static_cast<uint64_t>(kManualSlices[i].startPos)
			                   && range->sampleHolder.endPos == static_cast<uint64_t>(wantEnd)
			                   && range->sampleHolder.transpose == kManualSlices[i].transpose;
			printf("SC-PROBE leg=slice-manual drum=%d start=%llu end=%llu transpose=%d\n", i,
			       static_cast<unsigned long long>(range->sampleHolder.startPos),
			       static_cast<unsigned long long>(range->sampleHolder.endPos), range->sampleHolder.transpose);
			painted += match ? 1 : 0;
		}
		auto* clip = static_cast<InstrumentClip*>(g_kitClip);
		int32_t rowsWithDrum = 0;
		for (int32_t i = 0; i < std::ssize(clip->noteRows); i++) {
			if (clip->noteRows[i].drum != nullptr) {
				rowsWithDrum++;
			}
		}
		printf("SC-SLICE-MANUAL drums=%d boundToSlice=%d painted=%d rowsWithDrum=%d ticks=%d\n", drums, boundToSlice,
		       painted, rowsWithDrum, g_waitTicks);
		if (drums != kManualSliceCount || boundToSlice != kManualSliceCount || painted != kManualSliceCount
		    || rowsWithDrum < kManualSliceCount) {
			printf("SC-SLICE-MANUAL FAIL the manual slice did not end with %d drums on the sliced sample, each with a "
			       "NoteRow and painted from the slice points as they were when Select was pressed\n",
			       kManualSliceCount);
			g_failures++;
		}
		break;
	}
	case Leg::ImportGate: {
		deluge_audio_load_set_latency_ms(0);
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		int32_t drums = 0;
		for (Drum* drum = static_cast<Kit*>(g_importGateClip->output)->firstDrum; drum != nullptr; drum = drum->next) {
			drums++;
		}
		const bool completed = drums == kImportGateCount && !isUIOpen(&sampleBrowser);
		printf("IM-GATE whileRunning=%d secondRefused=%d backRefused=%d released=%d completed=%d ticks=%d\n",
		       g_imGateWhileRunning ? 1 : 0, g_imGateSecondRefused ? 1 : 0, g_imGateBackRefused ? 1 : 0,
		       released ? 1 : 0, completed ? 1 : 0, g_waitTicks);
		if (!g_imGateWhileRunning) {
			printf("IM-GATE FAIL the import had ended before its held loads could land: an import must wait on its "
			       "loads with the gate closed\n");
			g_failures++;
		}
		else if (!g_imGateSecondRefused || !g_imGateBackRefused || !released || !completed) {
			printf(
			    "IM-GATE FAIL BACK or a second Select was taken while the import was out, or the gate stayed closed, "
			    "or the import did not complete once its loads landed\n");
			g_failures++;
		}
		break;
	}
	case Leg::ImportStall: {
		deluge_audio_load_set_latency_ms(0);
		Source& source = static_cast<SoundInstrument*>(g_importSynthClip->output)->sources[0];
		bool rangesKept = std::ssize(source.ranges) == std::ssize(g_imStallRangePaths);
		for (int32_t i = 0; rangesKept && i < std::ssize(source.ranges); i++) {
			rangesKept = static_cast<MultisampleRange*>(source.ranges.getElement(i))->sampleHolder.filePath
			             == g_imStallRangePaths[i];
		}
		bool reasonsKept = true;
		for (int32_t i = 0; i < 2; i++) {
			reasonsKept =
			    reasonsKept && g_imStallKept[i] != nullptr && g_imStallKept[i]->leaseCount() == g_imStallKeptLeases[i];
		}
		const bool menuClosed = !isUIOpen(&deluge::gui::context_menu::sample_browser::synth);
		const bool browserOpen = isUIOpen(&sampleBrowser);
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		// How the import ended: on the async sample load's stall bound, not on some other failure.
		printf("SC-PROBE leg=import-stall giveUps=%d\n", deluge::gui::folderImport.giveUps());
		printf("IM-STALL rangesKept=%d reasonsKept=%d menuClosed=%d browserOpen=%d released=%d\n", rangesKept ? 1 : 0,
		       reasonsKept ? 1 : 0, menuClosed ? 1 : 0, browserOpen ? 1 : 0, released ? 1 : 0);
		if (!rangesKept || !reasonsKept || !menuClosed || !browserOpen || !released) {
			printf("IM-STALL FAIL an import whose second file's load never answered did not abort cleanly: the synth's "
			       "ranges and the loaded files' reasons must be as they were, just the menu closed, the browser open, "
			       "and "
			       "the gate released\n");
			g_failures++;
		}
		break;
	}
	case Leg::ImportKit:
	case Leg::ImportMulti: {
		const ImportMark now = markImport();
		const int32_t listings = now.listings - g_importMark.listings;
		const int32_t loads = now.loads - g_importMark.loads;
		const int32_t pitch = now.pitch - g_importMark.pitch;
		const int32_t sampleIo = now.sampleIo - g_importMark.sampleIo;
		const int32_t files = (g_leg == Leg::ImportKit) ? kImportKitCount : kImportMultiCount;
		const int32_t wantPitch = (g_leg == Leg::ImportKit) ? 0 : kImportMultiCount;
		const int32_t listingsRun = deluge::gui::folderImport.listingsRun();
		printf("IM-TIER leg=%s listingsRun=%d listingsOnOwner=%d loadsOnOwner=%d pitchOnOwner=%d sampleIo=%d\n",
		       legName(g_leg), listingsRun, listings, loads, pitch, sampleIo);
		if (loads != 0 || sampleIo != 0) {
			printf("IM-TIER FAIL leg=%s %d of %d file load(s) began on the storage owner and %d sample read(s) went to "
			       "the "
			       "card on the interaction tier -- a folder import's loads belong on the interaction tier\n",
			       legName(g_leg), loads, files, sampleIo);
			g_failures++;
		}
		// The folder's listing used to run on the storage owner and this leg required it (want 1). It
		// now goes through the directory channel, so zero is the claim: the assertion is KEPT and
		// inverted rather than dropped, because what it guards is still worth guarding -- a listing
		// that came back to the owner would be the regression.
		//
		// The pitch detection is a different family and still belongs there: it streams a whole
		// sample through SampleFrameReader with cluster loading, so `wantPitch` is unchanged.
		if (listingsRun == 0) {
			printf("IM-TIER FAIL leg=%s no folder listing ran at all\n", legName(g_leg));
			g_failures++;
		}
		if (listings != 0 || pitch != wantPitch) {
			printf(
			    "IM-TIER FAIL leg=%s listings on the owner %d (want 0), pitch detections on the owner %d (want %d)\n",
			    legName(g_leg), listings, pitch, wantPitch);
			g_failures++;
		}
		const bool browserClosed = !isUIOpen(&sampleBrowser);
		if (g_leg == Leg::ImportKit) {
			auto* kit = static_cast<Kit*>(g_importKitClip->output);
			int32_t drums = 0;
			int32_t matched = 0;
			for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next, drums++) {
				if (drum->type != DrumType::SOUND) {
					continue;
				}
				auto* soundDrum = static_cast<SoundDrum*>(drum);
				AudioFileHolder* holder = firstHolder(soundDrum->sources[0]);
				printf("SC-PROBE leg=import-kit drum=%d name=%s path=%s bound=%d repeat=%d sideChain=%d\n", drums,
				       soundDrum->drumName.c_str(), holder != nullptr ? holder->filePath.c_str() : "",
				       (holder != nullptr && holder->audioFile != nullptr) ? 1 : 0,
				       static_cast<int>(soundDrum->sources[0].repeatMode), soundDrum->sideChainSendLevel);
				if (drums < kImportKitCount && holder != nullptr && holder->audioFile != nullptr
				    && soundDrum->drumName == kImportKitNames[drums]
				    && holder->filePath == std::string{kImportKitFolder} + "/IMK_" + kImportKitNames[drums] + ".WAV") {
					matched++;
				}
			}
			auto* clip = static_cast<InstrumentClip*>(g_importKitClip);
			int32_t rowsWithDrum = 0;
			for (int32_t i = 0; i < std::ssize(clip->noteRows); i++) {
				if (clip->noteRows[i].drum != nullptr) {
					rowsWithDrum++;
				}
			}
			printf("IM-KIT drums=%d matched=%d rowsWithDrum=%d edited=%d browserClosed=%d ticks=%d\n", drums, matched,
			       rowsWithDrum, kit->editedByUser ? 1 : 0, browserClosed ? 1 : 0, g_waitTicks);
			if (drums != kImportKitCount || matched != kImportKitCount || rowsWithDrum < kImportKitCount
			    || !kit->editedByUser || !browserClosed) {
				printf(
				    "IM-KIT FAIL the kit import did not end with one drum per file in filename order, each named after "
				    "its file without the prefix, bound to it and with a NoteRow, the kit edited and the browser "
				    "closed\n");
				g_failures++;
			}
		}
		else {
			auto* synth = static_cast<SoundInstrument*>(g_importSynthClip->output);
			Source& source = synth->sources[0];
			const int32_t ranges = std::ssize(source.ranges);
			int32_t matched = 0;
			for (int32_t i = 0; i < ranges; i++) {
				auto* range = static_cast<MultisampleRange*>(source.ranges.getElement(i));
				const bool bound = range->sampleHolder.audioFile != nullptr;
				printf("SC-PROBE leg=import-multi range=%d top=%d path=%s bound=%d transpose=%d\n", i, range->topNote,
				       range->sampleHolder.filePath.c_str(), bound ? 1 : 0, range->sampleHolder.transpose);
				if (i < kImportMultiCount && bound && range->sampleHolder.filePath == kImportMultiFiles[i]
				    && range->topNote == kImportMultiTopNotes[i]) {
					matched++;
				}
			}
			printf("IM-MULTI ranges=%d matched=%d repeat=%d osc=%d edited=%d browserClosed=%d ticks=%d\n", ranges,
			       matched, static_cast<int>(source.repeatMode), static_cast<int>(source.oscType),
			       synth->editedByUser ? 1 : 0, browserClosed ? 1 : 0, g_waitTicks);
			if (ranges != kImportMultiCount || matched != kImportMultiCount
			    || source.repeatMode != SampleRepeatMode::ONCE || !browserClosed) {
				printf(
				    "IM-MULTI FAIL the multisample import did not end with one range per file in pitch order, each "
				    "bound to its file with the top note today's importer gives it, one-second files playing once, and "
				    "the browser closed\n");
				g_failures++;
			}
		}
		break;
	}
	case Leg::RandomizeGate: {
		deluge_audio_load_set_latency_ms(0);
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		if (released && g_rdGateWhileRunning) {
			// The same pad, now the gate is open: taken, then taken away again so the clip is as it was.
			g_rdGateEditTakenAfter = pressGateEditPad();
			(void)pressGateEditPad();
		}
		printf("RD-GATE whileRunning=%d secondWalkRefused=%d editPadRefused=%d editPadTakenAfter=%d released=%d "
		       "ticks=%d\n",
		       g_rdGateWhileRunning ? 1 : 0, g_rdGateSecondWalkRefused ? 1 : 0, g_rdGateEditRefused ? 1 : 0,
		       g_rdGateEditTakenAfter ? 1 : 0, released ? 1 : 0, g_waitTicks);
		if (!g_rdGateWhileRunning) {
			printf("RD-GATE FAIL the walk had ended before its held load could land: a randomize must wait on its load "
			       "with the gate closed\n");
			g_failures++;
		}
		else if (!g_rdGateSecondWalkRefused || !g_rdGateEditRefused || !g_rdGateEditTakenAfter || !released) {
			printf(
			    "RD-GATE FAIL a second randomize or the edit pad was taken while the walk was out, or the gate stayed "
			    "closed once it ended\n");
			g_failures++;
		}
		break;
	}
	case Leg::RandomizeStall: {
		deluge_audio_load_set_latency_ms(0);
		auto* first = static_cast<SoundDrum*>(static_cast<Kit*>(g_kitClip->output)->firstDrum);
		auto* second = static_cast<SoundDrum*>(first->next);
		auto* third = static_cast<SoundDrum*>(second->next);
		AudioFileHolder* kept = firstHolder(first->sources[0]);
		AudioFileHolder* held = firstHolder(second->sources[0]);
		const bool keptFirst = kept != nullptr && kept->audioFile != nullptr && kept->filePath == kRandomizeStallKept
		                       && first->drumName == kRandomizeStallKeptName;
		const bool secondUnbound = held != nullptr && held->audioFile == nullptr;
		// Untouched: still named as the preset named it, which a randomize would have replaced with its file's name.
		const bool thirdUntouched = third->drumName == kRandomizeDrums[2].name;
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		// How the walk ended: on the async sample load's stall bound, not on some other failure.
		printf("SC-PROBE leg=randomize-stall giveUps=%d\n", deluge::gui::randomizeDrums.giveUps());
		printf("RD-STALL keptFirst=%d secondUnbound=%d thirdUntouched=%d released=%d\n", keptFirst ? 1 : 0,
		       secondUnbound ? 1 : 0, thirdUntouched ? 1 : 0, released ? 1 : 0);
		if (!keptFirst || !secondUnbound || !thirdUntouched || !released) {
			printf("RD-STALL FAIL a walk whose second row's load never answered did not stop there: the first row must "
			       "keep its file, the second must be left unbound, the third untouched, and the gate released\n");
			g_failures++;
		}
		break;
	}
	case Leg::RandomizeCancelScan: {
		deluge_file_directory_set_latency_ms(0);
		SoundDrum* drum = kitDrum();
		AudioFileHolder* holder = (drum != nullptr) ? firstHolder(drum->sources[0]) : nullptr;
		const int32_t entered = deluge::gui::randomizeDrums.drainsEntered() - g_rdCancelDrainsEnteredBefore;
		const int32_t collected = deluge::gui::randomizeDrums.drainsCollected() - g_rdCancelDrainsCollectedBefore;
		// The claim the rounds are for: a randomize made after them still gets a directory handle, so every handle
		// their cancelled opens produced went back. Nothing else in this leg can see a burned handle -- a drain that
		// collects and drops one looks identical from the outside until the slots run out.
		const bool landed =
		    holder != nullptr && holder->audioFile != nullptr && holder->filePath.rfind(kRandomizeFolder, 0) == 0;
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		printf("RD-CANCELSCAN rounds=%d entered=%d collected=%d drained=%d rowUntouched=%d landed=%d released=%d "
		       "ticks=%d\n",
		       g_rdCancelRound, entered, collected, g_rdCancelDrained, g_rdCancelRowUntouched ? 1 : 0, landed ? 1 : 0,
		       released ? 1 : 0, g_waitTicks);
		if (g_rdCancelRound != kRandomizeCancelRounds || entered != kRandomizeCancelRounds) {
			// Without this the leg proves nothing: every claim below is about what a drain does, and a cancel that
			// found nothing outstanding never drains. This is the leg that went quietly green once the cancel's own
			// test could no longer be true.
			printf("RD-CANCELSCAN FAIL only %d of %d walks were cancelled with the row's open still out, so the "
			       "drain ran %d time(s)\n",
			       g_rdCancelRound, kRandomizeCancelRounds, entered);
			g_failures++;
		}
		else if (collected != kRandomizeCancelRounds) {
			printf("RD-CANCELSCAN FAIL %d of %d drains never collected the handle their cancelled open produced\n",
			       kRandomizeCancelRounds - collected, kRandomizeCancelRounds);
			g_failures++;
		}
		else if (!landed) {
			printf("RD-CANCELSCAN FAIL a randomize made after %d cancelled opens could not open its folder: the "
			       "handles those opens produced were collected but never given back\n",
			       kRandomizeCancelRounds);
			g_failures++;
		}
		else if (g_rdCancelDrained != 0 || !g_rdCancelRowUntouched || !released) {
			printf("RD-CANCELSCAN FAIL a cancelled walk did more than discard: it must read no entries and leave the "
			       "row's holder and name as they were, and the gate must be released at the end\n");
			g_failures++;
		}
		break;
	}
	case Leg::RandomizeOne:
	case Leg::RandomizeAll: {
		const RandomizeMark now = markRandomize();
		const int32_t scans = now.scans - g_randomizeMark.scans;
		const int32_t loads = now.loads - g_randomizeMark.loads;
		const int32_t sampleIo = now.sampleIo - g_randomizeMark.sampleIo;
		const int32_t scansRun = deluge::gui::randomizeDrums.scansRun();
		printf("RD-TIER leg=%s scansRun=%d scansOnOwner=%d loadsOnOwner=%d sampleIo=%d\n", legName(g_leg), scansRun,
		       scans, loads, sampleIo);
		if (loads != 0 || sampleIo != 0) {
			printf("RD-TIER FAIL leg=%s %d randomizer load(s) began on the storage owner and %d sample read(s) went to "
			       "the card on the interaction tier -- a randomized drum's load belongs on the interaction tier\n",
			       legName(g_leg), loads, sampleIo);
			g_failures++;
		}
		// TWO claims, deliberately split. `scansOnOwner` alone used to carry both, so inverting it
		// for the tier move would have quietly dropped the liveness half.
		if (scans != 0) {
			printf("RD-TIER FAIL leg=%s %d randomizer scan(s) ran on the storage owner -- a row's folder is "
			       "opened through the directory channel now\n",
			       legName(g_leg), scans);
			g_failures++;
		}
		if (scansRun == 0) {
			printf("RD-TIER FAIL leg=%s no randomizer scan ran at all\n", legName(g_leg));
			g_failures++;
		}

		int32_t randomized = 0;
		int32_t untouched = 0;
		int32_t index = 0;
		for (Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum;
		     drum != nullptr && index < kRandomizeDrumCount; drum = drum->next, index++) {
			AudioFileHolder* holder = firstHolder(static_cast<SoundDrum*>(drum)->sources[0]);
			const bool eligible = (g_leg == Leg::RandomizeAll) ? (index < kRandomizeEligible) : (index == 0);
			const bool inFolder =
			    holder != nullptr && holder->audioFile != nullptr && holder->filePath.rfind(kRandomizeFolder, 0) == 0;
			const std::string& name = static_cast<SoundDrum*>(drum)->drumName;
			const bool namedAfterFile =
			    inFolder && holder->filePath.rfind(name.c_str()) != std::string::npos && !name.empty();
			printf("SC-PROBE leg=%s drum=%d path=%s name=%s\n", legName(g_leg), index,
			       holder != nullptr ? holder->filePath.c_str() : "", name.c_str());
			if (eligible) {
				randomized += (inFolder && namedAfterFile) ? 1 : 0;
			}
			else {
				untouched += (holder != nullptr && holder->filePath == g_randomizeRowPaths[index]) ? 1 : 0;
			}
		}
		const int32_t wantRandomized = (g_leg == Leg::RandomizeAll) ? kRandomizeEligible : 1;
		const int32_t wantUntouched = kRandomizeDrumCount - wantRandomized;
		if (g_leg == Leg::RandomizeOne) {
			// scansRun, not scansOnOwner: the latter is zero by design now, and printing it here read
			// as "no scan happened" when a scan had in fact run on the interaction tier.
			printf("RD-ONE randomized=%d untouched=%d scans=%d ticks=%d\n", randomized, untouched,
			       (int)deluge::gui::randomizeDrums.scansRun(), g_waitTicks);
			if (randomized != wantRandomized || untouched != wantUntouched) {
				printf("RD-ONE FAIL the selected drum did not end on a file from its own folder, named after it, with "
				       "every other row untouched\n");
				g_failures++;
			}
		}
		else {
			printf("RD-ALL randomized=%d untouched=%d scans=%d ticks=%d\n", randomized, untouched,
			       (int)deluge::gui::randomizeDrums.scansRun(), g_waitTicks);
			if (randomized != wantRandomized || untouched != wantUntouched) {
				printf("RD-ALL FAIL an affect-entire randomize did not end with every unmuted row that has notes on a "
				       "file from its own folder, named after it, leaving the muted and empty rows alone\n");
				g_failures++;
			}
		}
		break;
	}
	case Leg::Done:
		break;
	}
}

/// One tick of a supersede leg: the browser ticked, and the moment the first auto-load claim's pitch detection is out,
/// the cursor moved on to the second file, which cancels that claim.
void tickSupersede() {
	sampleBrowser.graphicsRoutine();
	if (g_supersedeFired || !deluge::gui::sampleClaim.opOutstanding()) {
		return;
	}
	g_supersedeFired = true;
	if (g_leg == Leg::Supersede) {
		// In memory, so its preview lands in this same tick with the detection still out -- as on the device, where
		// detection reads the card for longer than a graphics tick -- and asks for the next claim at once.
		sampleBrowser.previewFile(kSupersedeLanded, 0);
		sampleBrowser.graphicsRoutine();
	}
	else {
		// Its preview held: the claim the cursor left must go now, not when this preview lands.
		deluge_audio_load_set_latency_ms(kSupersedeHoldMs);
		sampleBrowser.previewFile(kSupersedeLoading, 0);
	}
}

/// One tick of the cancel leg.
///
/// A round lets the row's open reach the held directory channel, cancels the walk through the real user action --
/// returning to the clip view, the only caller of cancelWalk() -- and waits for the drain that cancel starts. After
/// the last round the hold comes off and the leg randomizes for real: that walk needs a directory handle, and the
/// rounds have asked for one more than there are.
void tickRandomizeCancel() {
	auto& walk = deluge::gui::randomizeDrums;
	if (g_rdCancelFinalPressed) {
		return; // the real walk is running; legEnded() waits for it
	}
	if (!g_rdCancelCancelled) {
		if (++g_rdCancelPasses < kRandomizeCancelPasses) {
			return; // this round's open has not reached the channel yet
		}
		instrumentClipView.focusRegained();
		g_rdCancelCancelled = true;
		return;
	}
	if (walk.draining()) {
		return; // waiting for the handle the cancelled open still produces
	}
	g_rdCancelRound++;
	g_rdCancelCancelled = false;
	g_rdCancelPasses = 0;
	if (g_rdCancelRound < kRandomizeCancelRounds) {
		pressRandomize(/*affectEntire=*/false);
		return;
	}
	// Read before the real walk runs: it reads a folder and rebinds the row, which is exactly what a cancelled one
	// must not have done.
	g_rdCancelDrained = walk.scansRun() - g_rdCancelScansBefore;
	SoundDrum* drum = kitDrum();
	AudioFileHolder* holder = (drum != nullptr) ? firstHolder(drum->sources[0]) : nullptr;
	g_rdCancelRowUntouched = holder != nullptr && holder->audioFile == nullptr
	                         && holder->filePath == kRandomizeFirstSample && drum->drumName == kRandomizeDrums[0].name;
	deluge_file_directory_set_latency_ms(0);
	pressRandomize(/*affectEntire=*/false);
	g_rdCancelFinalPressed = true;
}

void nextLeg() {
	g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
	g_step = Step::Prepare;
	g_waitTicks = 0;
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
	if (g_leg == Leg::Done) {
		// The question the on_owner() collapse turns on: did anything actually RUN on the storage
		// owner during this gate? Every site that branches on on_owner() is only interesting while
		// the answer is non-zero. Reporting-only for now -- the value is unmeasured.
		printf("SC-ONOWNER trueAfterStart=%d ownerOps=%d\n", (int)deluge::storage::Owner::onOwnerTrue(),
		       (int)deluge::storage::Owner::opsRun());
		printf("SC-RESULT failures=%d\n", g_failures);
		fflush(stdout);
		g_summaryPrinted = true;
		return;
	}
	switch (g_step) {
	case Step::Prepare:
		prepareLeg();
		g_step = Step::Open;
		break;
	case Step::Open:
		// A claim still in flight from the leg before -- one that timed out -- would act on this leg's clip.
		if (deluge::gui::sampleClaim.inFlight()) {
			if (++g_waitTicks > kMaxLegTicks) {
				fail(legName(g_leg), "a claim from the leg before never ended");
				nextLeg();
			}
			break;
		}
		g_waitTicks = 0;
		if (openLeg()) {
			// The randomizer legs press a pad in the clip view: no listing, no file to select, no preview to draw.
			g_step = isRandomize() ? Step::Act : Step::Listing;
		}
		else {
			nextLeg();
		}
		break;
	case Step::Listing:
	case Step::SelectWait:
		if (listingBusy()) {
			if (++g_waitTicks > kMaxListingTicks) {
				fail(legName(g_leg), "a browser listing never finished");
				nextLeg();
			}
			break;
		}
		if (g_step == Step::Listing) {
			g_step = Step::Select;
			break;
		}
		printf("SC-SELECT leg=%s file=%s/%s\n", legName(g_leg), Browser::currentDir.c_str(),
		       sampleBrowser.getCurrentFileItem() != nullptr ? sampleBrowser.getCurrentFileItem()->filename.c_str()
		                                                     : "");
		g_waitTicks = 0;
		g_step = (g_leg == Leg::Slice || g_leg == Leg::SliceManual || isSupersede()) ? Step::PreviewWait : Step::Act;
		break;
	case Step::PreviewWait: {
		// The slicer draws the browser's previewed waveform, so a user opens it only once the preview shows the file.
		const Sample* drawn = waveformBasicNavigator.sample;
		if (drawn != nullptr && drawn->filePath == legFile()) {
			printf("SC-PROBE leg=%s preview drawn ticks=%d\n", legName(g_leg), g_waitTicks);
			g_step = Step::Act;
			break;
		}
		sampleBrowser.graphicsRoutine();
		if (++g_waitTicks > kMaxLegTicks) {
			fail(legName(g_leg), "the browser's preview never drew the file");
			nextLeg();
		}
		break;
	}
	case Step::Select:
		(void)sampleBrowser.setFileByFullPath(OutputType::NONE, legFile());
		g_waitTicks = 0;
		g_step = Step::SelectWait;
		break;
	case Step::Act:
		actLeg();
		g_waitTicks = 0;
		g_step = Step::Wait;
		break;
	case Step::Wait:
		if (isSupersede()) {
			tickSupersede();
		}
		if (g_leg == Leg::ImportGate) {
			tickImportGate();
		}
		if (isRandomize() && getCurrentUI() == &instrumentClipView) {
			// A randomize may walk on the clip view's graphics routine: ticked here, as the slice legs tick the
			// slicer's.
			instrumentClipView.graphicsRoutine();
			if (g_leg == Leg::RandomizeGate) {
				tickRandomizeGate();
			}
			if (g_leg == Leg::RandomizeCancelScan) {
				tickRandomizeCancel();
			}
		}
		if ((g_leg == Leg::Slice || g_leg == Leg::SliceManual) && getCurrentUI() == &slicer) {
			// The slice ends on the slicer's graphics routine: ticked here, as the preview wait ticks the browser's.
			slicer.graphicsRoutine();
		}
		if (legEnded()
		    || ++g_waitTicks > ((g_leg == Leg::Stall || g_leg == Leg::RandomizeStall || g_leg == Leg::ImportStall)
		                            ? kMaxStallTicks
		                            : kMaxLegTicks)) {
			checkLeg();
			nextLeg();
		}
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_sample_claim_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_leg = Leg::Synth;
	g_step = Step::Prepare;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_waitTicks = 0;
	g_synth = nullptr;
	g_kitClip = nullptr;
	g_audioClip = nullptr;
	g_tierMark = {};
	g_gateMoved = false;
	g_supersedeFired = false;
}

bool deluge_scenario_sample_claim_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_sample_claim_dispatch_step(void) {
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

bool deluge_scenario_sample_claim_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_sample_claim_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
