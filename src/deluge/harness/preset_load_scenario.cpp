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

#include "harness/preset_load_scenario.h"

#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "gui/context_menu/clear_song.h"
#include "gui/context_menu/load_instrument_preset.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_midi_device_definition_ui.h"
#include "gui/ui/load/preset_walk.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "harness/preset_fixture_loader.h"
#include "hid/button.h"
#include "io/file.hpp"
#include "libdeluge/audio_load.h" // deluge_audio_load_set_latency_ms
#include "memory/fast_allocator.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/byte_channel.h" // bytes::interactionTierIo
#include "storage/flash_storage.h"
#include "storage/multi_range/multi_range.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases printf to an implementation this harness has no
// `putchar_` for -- every line below would become an undefined symbol at link time.
#ifdef printf
#undef printf
#endif

/// Drives SessionView's and ArrangerView's acquisition gestures for the AQ legs: the private state a pad or encoder
/// handler reads is written as a user's press leaves it, and then the handler itself is called.
class PresetAcquireProbe {
public:
	/// An empty pad pressed in Rows layout: the new-track gesture, making a track of @p type.
	static void pressEmptyRow(int32_t yDisplay, OutputType type = OutputType::SYNTH) {
		currentSong->sessionLayout = SessionLayoutType::SessionLayoutTypeRows;
		currentSong->songViewYScroll = 0;
		currentUIMode = UI_MODE_NONE;
		FlashStorage::defaultUseLastClipType = true;
		sessionView.lastTypeCreated = type;
		(void)sessionView.padAction(0, yDisplay, 1);
	}

	/// Let go of whatever clip press a gesture left held, and go back to Rows layout.
	static void releasePress() {
		sessionView.clipPressEnded();
		sessionView.gridResetPresses(true, true);
		currentUIMode = UI_MODE_NONE;
		currentSong->sessionLayout = SessionLayoutType::SessionLayoutTypeRows;
	}

	/// @p source's grid pad copied into the first empty track column, in its own section.
	static bool cloneIntoEmptyColumn(Clip* source) {
		currentSong->sessionLayout = SessionLayoutType::SessionLayoutTypeGrid;
		currentUIMode = UI_MODE_NONE;
		const Cartesian from = sessionView.gridXYFromClip(*source);
		const auto column = static_cast<int32_t>(sessionView.gridTrackCount());
		if (from.x < 0 || from.y < 0 || column >= kDisplayWidth) {
			return false;
		}
		sessionView.gridClonePad(from.x, from.y, column, from.y);
		return true;
	}

	/// The select encoder turned while @p clip's grid pad is held.
	static bool turnInGrid(Clip* clip, int8_t offset) {
		currentSong->sessionLayout = SessionLayoutType::SessionLayoutTypeGrid;
		const Cartesian xy = sessionView.gridXYFromClip(*clip);
		sessionView.gridFirstPressedX = xy.x;
		sessionView.gridFirstPressedY = xy.y;
		if (sessionView.getClipForLayout() != clip) {
			return false;
		}
		currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
		sessionView.selectEncoderAction(offset);
		return true;
	}

	/// The select encoder turned while @p output's arranger row is auditioned.
	static void turnInArranger(Output* output, int8_t offset) {
		arrangerView.outputsOnScreen[0] = output;
		arrangerView.yPressedEffective = 0;
		currentUIMode = UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION;
		arrangerView.selectEncoderAction(offset);
	}

	/// Let go of the arranger row's audition.
	static void endArrangerAudition() {
		Output* output = arrangerView.outputsOnScreen[0];
		currentUIMode = UI_MODE_NONE;
		if (output != nullptr) {
			arrangerView.endAudition(output);
		}
		arrangerView.outputsOnScreen[0] = nullptr;
	}
};

namespace {

/// Each leg's samples, in folders of their own (see pack_preset_load_image in the harness).
constexpr const char* kSynthSample = "SAMPLES/PL/PLSYN.WAV";
constexpr const char* kKitSamples[] = {"SAMPLES/PL/PLKA.WAV", "SAMPLES/PL/PLKB.WAV", "SAMPLES/PL/PLKC.WAV"};
constexpr const char* kKitDrumNames[] = {"PLKA", "PLKB", "PLKC"};
constexpr int32_t kKitDrumCount = 3;
constexpr const char* kRowSample = "SAMPLES/PL/PLROW.WAV";
constexpr const char* kCloneSample = "SAMPLES/PL/PLCLN.WAV";
constexpr const char* kBaseKitSample = "SAMPLES/PL/PLBASE.WAV";

/// The presets, written through the firmware's own serialisers in the first leg's preparation.
constexpr const char* kSynthPreset = "SYNTHS/PLSYN.XML";
constexpr const char* kSynthName = "PLSYN";
constexpr const char* kKitPreset = "KITS/PLKIT.XML";
constexpr const char* kKitName = "PLKIT";
constexpr const char* kRowPreset = "SYNTHS/PLROW.XML";
constexpr const char* kRowName = "PLROW";
constexpr const char* kClonePreset = "SYNTHS/PLCLN.XML";
constexpr const char* kCloneName = "PLCLN";
/// The kit clip's own one-drum preset, the one the new-kit-track gesture picks up: it sorts before kKitPreset.
constexpr const char* kBaseKitPreset = "KITS/PLBASE.XML";

/// The race leg's synth preset: cloned without waiting for the selection's scroll-load.
constexpr const char* kRacePreset = "SYNTHS/PLRACE.XML";

/// Reachable ONLY by descending into a subfolder. The walk's recursion has no other coverage:
/// measured 2026-09-19, every gate reaches depth 1 and the reference card's SYNTHS is flat, so a
/// conversion that silently stopped recursing would look perfect until a user with subfolders found
/// their presets gone.
/// The walk starts HERE: a folder with no presets of its own, only a subfolder. That is what forces
/// the descent -- with a usable preset at the start folder the walk returns it and never recurses,
/// which is why this leg cannot simply search SYNTHS.
constexpr const char* kWalkStartDir = "SYNTHS/WALK";
constexpr const char* kNestedPreset = "SYNTHS/WALK/ZNEST/PLNEST.XML";
constexpr const char* kNestedSample = "SAMPLES/PL/PLNEST.WAV";
/// What the walk must return for PL-WALK-DEPTH: only a descent can produce it.
constexpr const char* kNestedFilename = "PLNEST.XML";
/// Subfolders the walk's start folder holds: 24 empty fillers plus ZNEST. Must stay above
/// FILE_ITEMS_MAX_NUM_ELEMENTS or the cull never happens and paging goes untested.
constexpr int32_t kWalkFolderCount = 25;
constexpr const char* kRaceSample = "SAMPLES/PL/PLRACE.WAV";
constexpr const char* kRaceName = "PLRACE";
/// The supersession leg's kits: a heavy one whose samples nothing loads before it, and a light one scrolled on to. Both
/// sort after PLBASE, so neither is the kit clip's own.
constexpr const char* kHeavyPreset = "KITS/PLHEAVY.XML";
constexpr const char* kHeavyName = "PLHEAVY";
constexpr const char* kHeavySamples[] = {"SAMPLES/PLH/PLHA.WAV", "SAMPLES/PLH/PLHB.WAV", "SAMPLES/PLH/PLHC.WAV"};
constexpr const char* kHeavyDrumNames[] = {"PLHA", "PLHB", "PLHC"};
constexpr const char* kLitePreset = "KITS/PLLITE.XML";
constexpr const char* kLiteName = "PLLITE";
constexpr const char* kLiteSample = "SAMPLES/PLL/PLLITE.WAV";
/// The gate and stall legs' synth presets, each naming a sample nothing loads before it.
constexpr const char* kGatePreset = "SYNTHS/PLGATE.XML";
constexpr const char* kGateName = "PLGATE";
constexpr const char* kGateSample = "SAMPLES/PLG/PLGATE.WAV";
constexpr const char* kStallPreset = "SYNTHS/PLSTALL.XML";
constexpr const char* kStallName = "PLSTALL";
constexpr const char* kStallSample = "SAMPLES/PLS/PLSTALL.WAV";
/// How long the loader holds each load in the supersession and gate legs, and in the stall leg.
constexpr uint32_t kHoldMs = 400;
constexpr uint32_t kStallHoldMs = 30000;
/// Scenario ticks the stall leg's commit may take to give up before it is called wedged: far past its stall bound.
constexpr int32_t kMaxStallTicks = 20000;

/// Scenario ticks a browser listing may take before it is called wedged.
constexpr int32_t kMaxListingTicks = 20000;
/// Scenario ticks a load may take before it is called wedged.
constexpr int32_t kMaxLegTicks = 2000;

enum class Leg : uint8_t {
	BlankBoot,
	Synth,
	/// The SAME search as checkPresetWalk's, over the same fixture, run through PresetWalk instead of
	/// the recursive function -- and ticked from here, on the interaction tier, which is the half the
	/// PL-WALK legs cannot reach. Placed right after Synth because Synth's prepare writes the fixture.
	WalkMachine,
	Kit,
	Row,
	Clone,
	Race,
	Supersede,
	Gate,
	AqNew,
	AqChange,
	AqNavigate,
	AqSupersede,
	AqRefuse,
	AqClone,
	RevertCard,
	RevertMemory,
	Stall,
	AqStall,
	RevertStall,
	MidiDef,
	BlankClear,
	BlankEdited,
	Done
};
enum class Step : uint8_t { Prepare, Open, Listing, Select, SelectWait, Settle, Act, Wait };

Leg g_leg = Leg::Synth;
Step g_step = Step::Prepare;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_waitTicks = 0;
SoundInstrument* g_synth = nullptr;
Clip* g_synthClip = nullptr;
Clip* g_kitClip = nullptr;
int32_t g_outputsBefore = 0;
/// The kit row leg's NoteRow index, recorded when it opens: loading into a row replaces that row's drum, which the kit
/// then keeps at the end of its list rather than first.
int32_t g_rowIndex = -1;

/// Whether the supersession leg saw the heavy kit swapped into the kit clip on any tick.
bool g_heavySwapped = false;
/// The song's orphaned param managers when the supersession leg selected the heavy kit.
int32_t g_orphansBefore = 0;
/// The sample loads begun when the supersession leg selected the heavy kit: it scrolls on only once the heavy kit's own
/// has begun, so a kit has been parsed and is out loading when the scroll cancels it.
int32_t g_heavyLoadsBefore = 0;
/// The blank-song legs: the built-in synth a song with no preset starts on, and how long they wait for one to land
/// once the song has been swapped in.
constexpr const char* kDefaultSynthName = "0";
constexpr int32_t kBlankSettleTicks = 120;

/// The MIDI device-definition leg: the file it writes through the firmware, and the CC it labels in it.
constexpr const char* kMidiDefinition = "MIDI_DEVICES/DEFINITION/MDTEST.XML";
constexpr int32_t kMidiDefinitionCC = 74;
constexpr const char* kMidiDefinitionLabel = "CUTOFF";
int32_t g_midiDefReadsBefore = 0;
int32_t g_midiDefOwnerReadsBefore = 0;

/// The revert legs: the clip's original, read as the leg opens, and what the exit was seen to do.
std::string g_revertName{};
std::string g_revertSample{};
bool g_revertTried = false;
bool g_revertClosedAtOnce = false;
bool g_revertGateClosed = false;
int32_t g_revertGiveUpsBefore = 0;

/// What the gate leg saw: whether its presses were tried, whether the commit was still out, and whether each was
/// refused.
bool g_gateTried = false;
bool g_gateWhileRunning = false;
bool g_gateTypeRefused = false;
bool g_gateEncoderRefused = false;

/// Where a leg's card work ran, read when the leg opens the browser: the loads' document reads and sample loads
/// begun on the storage owner, all of them, and the interaction tier's blocking sample reads.
struct TierMark {
	int32_t documentReads = 0;
	int32_t documentReadsOnOwner = 0;
	int32_t sampleLoads = 0;
	int32_t sampleLoadsOnOwner = 0;
	int32_t sampleIo = 0;
};
TierMark g_tierMark{};

const char* legName(Leg leg) {
	switch (leg) {
	case Leg::Synth:
		return "synth";
	case Leg::WalkMachine:
		return "walk-machine";
	case Leg::Kit:
		return "kit";
	case Leg::Row:
		return "row";
	case Leg::Clone:
		return "clone";
	case Leg::Race:
		return "race";
	case Leg::Supersede:
		return "supersede";
	case Leg::Gate:
		return "gate";
	case Leg::AqNew:
		return "aq-new";
	case Leg::AqChange:
		return "aq-change";
	case Leg::AqNavigate:
		return "aq-navigate";
	case Leg::AqSupersede:
		return "aq-supersede";
	case Leg::AqRefuse:
		return "aq-refuse";
	case Leg::AqClone:
		return "aq-clone";
	case Leg::RevertCard:
		return "revert-card";
	case Leg::RevertMemory:
		return "revert-memory";
	case Leg::RevertStall:
		return "revert-stall";
	case Leg::MidiDef:
		return "midi-def";
	case Leg::BlankBoot:
		return "blank-boot";
	case Leg::BlankClear:
		return "blank-clear";
	case Leg::BlankEdited:
		return "blank-edited";
	case Leg::Stall:
		return "stall";
	case Leg::AqStall:
		return "aq-stall";
	case Leg::Done:
		return "done";
	}
	return "?";
}

/// Whether the leg backs out of the browser, reverting to the clip's original.
bool isRevertLeg(Leg leg) {
	return leg == Leg::RevertCard || leg == Leg::RevertMemory || leg == Leg::RevertStall;
}

bool fail(const char* leg, const char* what) {
	printf("PL-SETUP FAIL leg=%s %s\n", leg, what);
	g_failures++;
	return false;
}

TierMark markTier() {
	return {deluge::gui::instrumentLoad.documentReads(), deluge::gui::instrumentLoad.documentReadsOnOwner(),
	        deluge::gui::instrumentLoad.sampleLoads(), deluge::gui::instrumentLoad.sampleLoadsOnOwner(),
	        deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoad)};
}

/// Queue @p fn on the storage owner: the fixture builds write the card, which belongs there.
void onOwner(void (*fn)()) {
	if (!deluge::storage::Owner::run([](void* ctx) { reinterpret_cast<void (*)()>(ctx)(); },
	                                 reinterpret_cast<void*>(fn))) {
		fail(legName(g_leg), "the storage owner's queue was full");
	}
}

AudioFileHolder* firstHolder(Source& source) {
	MultiRange* range = source.getOrCreateFirstRange();
	return (range != nullptr) ? range->getAudioFileHolder() : nullptr;
}

int32_t countOutputs() {
	int32_t outputs = 0;
	for (Output* output = currentSong->firstOutput; output != nullptr; output = output->next) {
		outputs++;
	}
	return outputs;
}

// ── Fixture construction ─────────────────────────────────────────────────────

/// Write @p instrument's document to @p path through the firmware serialiser.
bool writeInstrumentDocument(Instrument& instrument, const char* path, const char* closingTag) {
	Error error = StorageManager::createXMLFile(path, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return fail(legName(g_leg), "a preset file could not be created");
	}
	instrument.writeToFile(nullptr, currentSong);
	error = GetSerializer().closeFileAfterWriting(path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", closingTag);
	return error == Error::NONE || fail(legName(g_leg), "a preset file could not be closed");
}

/// A synth preset whose first oscillator plays @p samplePath, written through the firmware's new-synth and save
/// paths (never hand-written: a content-empty preset corrupts the heap on the load that rejects it). The synth is
/// leaked: it is never added to the song.
bool writeSampleSynthPreset(const char* path, const char* samplePath) {
	ParamManagerForTimeline paramManager;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (instrument == nullptr) {
		return fail(legName(g_leg), "no synth for a preset");
	}
	auto* synth = static_cast<SoundInstrument*>(instrument);
	synth->setupAsBlankSynth(&paramManager, false);
	synth->sources[0].setOscType(OscType::SAMPLE);
	AudioFileHolder* holder = firstHolder(synth->sources[0]);
	if (holder == nullptr) {
		return fail(legName(g_leg), "a preset synth has no sample range");
	}
	holder->filePath = samplePath;
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(instrument->toModControllable()), nullptr,
	                                &paramManager, true);
	return writeInstrumentDocument(*instrument, path, "\n</sound>\n");
}

/// Drive `findAnUnlaunchedPresetIncludingWithinSubfolders` once and pin what it does TODAY.
///
/// Runs on the storage owner, which is where the walk is legal until it is converted -- the four TIR
/// guards freeze otherwise, and they are what caught the fiber-inline attempt.
///
/// The start folder holds NO presets of its own, only `NESTED/`. That is the whole fixture: with a
/// usable preset at the start folder the walk returns it immediately and never recurses, so the leg
/// would pass while proving nothing. Measured 2026-09-19: no gate reached depth > 1 before this.
void checkPresetWalk() {
	const int32_t tierBefore = deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Browser);

	const std::string savedDir = Browser::currentDir;
	Browser::currentDir = kWalkStartDir;
	auto found = loadInstrumentPresetUI.findAnUnlaunchedPresetIncludingWithinSubfolders(currentSong, OutputType::SYNTH,
	                                                                                    Availability::ANY);
	const bool descended =
	    found.has_value() && *found != nullptr && std::string((*found)->filename) == std::string(kNestedFilename);
	printf("PL-WALK-FOUND ok=%d name=%s\n", (int)(found.has_value() && *found != nullptr),
	       (found.has_value() && *found != nullptr) ? (*found)->filename.c_str() : "-");
	printf("PL-WALK-DEPTH ok=%d descended=%d\n", (int)descended, (int)descended);

	// PAGING, proved the same indirect way DEPTH is. `SYNTHS/WALK` holds 25 subfolders and the cap is
	// FILE_ITEMS_MAX_NUM_ELEMENTS (20), so the first chunk CANNOT contain ZNEST -- it sorts last and is
	// culled from the end. Only a re-read from a search name (`goto goAgain`) can reach it. Finding
	// PLNEST therefore means the walk paged, and finding it means it also popped back out of 24 empty
	// subfolders on the way, which is the return-from-recursion path.
	printf("PL-WALK-PAGED ok=%d entries=%d cap=%d\n", (int)descended, (int)kWalkFolderCount,
	       (int)FILE_ITEMS_MAX_NUM_ELEMENTS);
	if (kWalkFolderCount <= FILE_ITEMS_MAX_NUM_ELEMENTS) {
		fail("walk", "the walk fixture no longer exceeds the cull cap, so paging is not exercised");
	}
	loadInstrumentPresetUI.emptyFileItems();

	// A folder with nothing usable at any depth must report exhaustion, not a false hit.
	Browser::currentDir = "SYNTHS/WALK/W01";
	auto none = loadInstrumentPresetUI.findAnUnlaunchedPresetIncludingWithinSubfolders(currentSong, OutputType::SYNTH,
	                                                                                   Availability::ANY);
	// The SPECIFIC error, not merely "an error". An earlier draft of this leg searched a folder that
	// does not exist and passed on FOLDER_DOESNT_EXIST (10) -- which tests a missing folder, not an
	// exhausted search. W01 is a real, empty filler folder, so the only honest answer is
	// NO_FURTHER_FILES_THIS_DIRECTION (16).
	const bool exhausted = !none.has_value() && none.error() == Error::NO_FURTHER_FILES_THIS_DIRECTION;
	printf("PL-WALK-EXHAUST ok=%d error=%d\n", (int)exhausted, (int)(none.has_value() ? Error::NONE : none.error()));
	loadInstrumentPresetUI.emptyFileItems();
	Browser::currentDir = savedDir;

	const int32_t tierIo =
	    deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Browser) - tierBefore;
	// Passes TODAY because the walk runs on the owner; must STILL pass after the conversion, for the
	// opposite reason -- on the interaction tier, never blocking. Holding across both is its value.
	printf("PL-WALK-TIER ok=%d tierIo=%d\n", (int)(tierIo == 0), (int)tierIo);

	if (!descended) {
		fail("walk", "the preset search did not descend into a subfolder");
	}
	if (!exhausted) {
		fail("walk", "a search with nothing usable did not report NO_FURTHER_FILES_THIS_DIRECTION");
	}
	if (tierIo != 0) {
		fail("walk", "the preset search did card I/O on the interaction tier");
	}
}

/// A sample drum, the way the slicer builds one, added to @p kit.
SoundDrum* addSampleDrum(Kit& kit, const char* name, const char* samplePath) {
	ParamManagerForTimeline paramManager;
	if (paramManager.setupWithPatching() != Error::NONE) {
		fail(legName(g_leg), "a drum's ParamManager could not be set up");
		return nullptr;
	}
	void* memory = deluge::memory::alloc_fast(sizeof(SoundDrum));
	if (memory == nullptr) {
		fail(legName(g_leg), "no memory for a drum");
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
		fail(legName(g_leg), "a drum has no sample range");
		return nullptr;
	}
	holder->filePath = samplePath;
	currentSong->backUpParamManager(drum, nullptr, &paramManager, true);
	return drum;
}

/// A kit preset of @p count sample drums, written through the firmware serialiser. The kit is leaked.
bool writeKitPreset(const char* path, const char* const* names, const char* const* samples, int32_t count) {
	ParamManagerForTimeline kitParams;
	Instrument* kitBase = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	if (kitBase == nullptr) {
		return fail(legName(g_leg), "no kit for a preset");
	}
	auto* kit = static_cast<Kit*>(kitBase);
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(kit->toModControllable()), nullptr, &kitParams,
	                                true);
	for (int32_t i = 0; i < count; i++) {
		if (addSampleDrum(*kit, names[i], samples[i]) == nullptr) {
			return false;
		}
	}
	return writeInstrumentDocument(*kit, path, "\n</kit>\n");
}

/// Write a MIDI device definition through the firmware's own serialiser: one labelled CC, and nothing else. The
/// Instrument it is written from is leaked, as the preset fixtures' are.
bool writeMidiDefinition(const char* path) {
	Instrument* instrument = StorageManager::createNewNonAudioInstrument(OutputType::MIDI_OUT, 2, -1);
	if (instrument == nullptr) {
		return fail("midi-def", "no MIDI instrument for a definition");
	}
	auto* midiInstrument = static_cast<MIDIInstrument*>(instrument);
	midiInstrument->setNameForCC(kMidiDefinitionCC, kMidiDefinitionLabel);
	Error error = StorageManager::createXMLFile(path, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return fail("midi-def", "a definition file could not be created");
	}
	midiInstrument->writeDeviceDefinitionFile(GetSerializer(), /*writeFileNameToPresetOrSong=*/false);
	error = GetSerializer().closeFileAfterWriting(path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", "\n");
	return error == Error::NONE || fail("midi-def", "a definition file could not be closed");
}

// ── Driving the browser ──────────────────────────────────────────────────────

/// Tick the current UI while a browser listing runs, and say whether it still is.
bool listingBusy() {
	if (Browser::isListingInProgress()) {
		getCurrentUI()->graphicsRoutine();
	}
	return Browser::isListingInProgress();
}

/// The Select encoder's press and release on the current UI, as a user's short press delivers them.
void pressSelect() {
	using namespace deluge::hid::button;
	(void)getCurrentUI()->buttonAction(SELECT_ENC, true, false);
	(void)getCurrentUI()->buttonAction(SELECT_ENC, false, false);
}

/// The kit clip's first drum and its NoteRow, or nulls.
SoundDrum* kitFirstDrum(NoteRow** row, int32_t* rowIndex) {
	auto* clip = static_cast<InstrumentClip*>(g_kitClip);
	auto* kit = static_cast<Kit*>(g_kitClip->output);
	Drum* drum = kit->firstDrum;
	if (drum == nullptr || drum->type != DrumType::SOUND) {
		return nullptr;
	}
	*row = clip->getNoteRowForDrum(drum, rowIndex);
	return (*row != nullptr) ? static_cast<SoundDrum*>(drum) : nullptr;
}

/// Make the leg's clip current under the clip view and open the preset browser on it, as the clip view's LOAD
/// gestures do.
bool openLeg() {
	const char* leg = legName(g_leg);
	if (g_leg == Leg::MidiDef) {
		// The definition browser opens on a clip minder whose Output is the MIDI Instrument.
		currentSong->setCurrentClip(g_synthClip);
		changeRootUI(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		const bool opened = openUI(&loadMidiDeviceDefinitionUI);
		printf("PL-SETUP leg=%s opened=%d browserOnTop=%d outputs=%d\n", leg, opened ? 1 : 0,
		       getCurrentUI() == &loadMidiDeviceDefinitionUI ? 1 : 0, countOutputs());
		return opened || fail(leg, "the device-definition browser could not be opened");
	}
	Clip* clip = (g_leg == Leg::Kit || g_leg == Leg::Row || g_leg == Leg::Supersede) ? g_kitClip : g_synthClip;
	if (clip == nullptr || clip->output == nullptr) {
		return fail(leg, "the leg's clip is missing");
	}
	currentSong->setCurrentClip(clip);
	changeRootUI(&instrumentClipView);
	auto* instrumentClip = static_cast<InstrumentClip*>(clip);
	auto* instrument = static_cast<Instrument*>(clip->output);
	if (g_leg == Leg::Row) {
		NoteRow* row = nullptr;
		int32_t rowIndex = 0;
		SoundDrum* drum = kitFirstDrum(&row, &rowIndex);
		if (drum == nullptr) {
			return fail(leg, "the kit clip has no first drum with a NoteRow");
		}
		g_rowIndex = rowIndex;
		loadInstrumentPresetUI.setupLoadSynthToKit(instrument, instrumentClip, drum, row, rowIndex);
	}
	else {
		loadInstrumentPresetUI.setupLoadInstrument(instrument->type, instrument, instrumentClip);
	}
	g_outputsBefore = countOutputs();
	g_tierMark = markTier();
	const bool opened = openUI(&loadInstrumentPresetUI);
	printf("PL-SETUP leg=%s opened=%d browserOnTop=%d outputs=%d\n", leg, opened ? 1 : 0,
	       getCurrentUI() == &loadInstrumentPresetUI ? 1 : 0, g_outputsBefore);
	return opened || fail(leg, "the preset browser could not be opened");
}

const char* legPreset() {
	switch (g_leg) {
	case Leg::Synth:
		return kSynthPreset;
	case Leg::Kit:
		return kKitPreset;
	case Leg::Row:
		return kRowPreset;
	case Leg::Clone:
		return kClonePreset;
	case Leg::Race:
		return kRacePreset;
	case Leg::Supersede:
		return kHeavyPreset;
	case Leg::Gate:
		return kGatePreset;
	case Leg::Stall:
		return kStallPreset;
	case Leg::AqNew:
	case Leg::AqChange:
	case Leg::AqNavigate:
	case Leg::AqSupersede:
	case Leg::AqRefuse:
	case Leg::AqClone:
	case Leg::AqStall:
	case Leg::WalkMachine:
	case Leg::Done:
		break;
	case Leg::RevertCard:
	case Leg::RevertMemory:
	case Leg::RevertStall:
		// The preset the browser scrolls to before backing out; the revert puts the original back.
		return kSynthPreset;
	case Leg::MidiDef:
		return kMidiDefinition;
	case Leg::BlankBoot:
	case Leg::BlankClear:
	case Leg::BlankEdited:
		// The blank-song legs open no browser: the song asks for its own preset.
		return "";
	}
	return "";
}

// ── The legs ─────────────────────────────────────────────────────────────────

void prepareLeg() {
	if (isRevertLeg(g_leg)) {
		auto* instrument = static_cast<Instrument*>(g_synthClip->output);
		g_revertName = instrument->name;
		AudioFileHolder* holder = (instrument->type == OutputType::SYNTH)
		                              ? firstHolder(static_cast<SoundInstrument*>(instrument)->sources[0])
		                              : nullptr;
		g_revertSample = (holder != nullptr) ? holder->filePath : "";
		if (g_leg == Leg::RevertMemory) {
			// An edited Instrument hibernates rather than leaves when it is replaced, so its revert needs no card.
			instrument->editedByUser = true;
		}
		g_revertTried = false;
		g_revertClosedAtOnce = false;
		g_revertGateClosed = false;
		g_revertGiveUpsBefore = deluge::gui::instrumentLoad.giveUps();
		printf("PL-PROBE leg=%s original=%s sample=%s\n", legName(g_leg), g_revertName.c_str(), g_revertSample.c_str());
		return;
	}
	if (g_leg == Leg::MidiDef) {
		onOwner([] {
			(void)deluge::io::mkdir("MIDI_DEVICES");
			(void)deluge::io::mkdir("MIDI_DEVICES/DEFINITION");
			const bool written = writeMidiDefinition(kMidiDefinition);
			// The clip becomes a MIDI one, which is what the definition browser opens on. MIDI reads no card, so this
			// arm is synchronous.
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(
			    modelStackMemory, currentSong, static_cast<InstrumentClip*>(g_synthClip));
			Instrument* midi =
			    static_cast<InstrumentClip*>(g_synthClip)->changeOutputType(modelStack, OutputType::MIDI_OUT);
			printf("PL-PROBE leg=midi-def written=%d midi=%d\n", written ? 1 : 0, midi != nullptr ? 1 : 0);
		});
		g_midiDefReadsBefore = deluge::gui::instrumentLoad.documentReads();
		g_midiDefOwnerReadsBefore = deluge::gui::instrumentLoad.documentReadsOnOwner();
		return;
	}
	if (g_leg != Leg::Synth) {
		return;
	}
	onOwner([] {
		bool ok = writeSampleSynthPreset(kSynthPreset, kSynthSample) && writeSampleSynthPreset(kRowPreset, kRowSample)
		          && writeSampleSynthPreset(kClonePreset, kCloneSample)
		          && writeKitPreset(kKitPreset, kKitDrumNames, kKitSamples, kKitDrumCount);
		const char* baseName = "PLBASE";
		const char* liteName = kLiteName;
		ok = ok && writeKitPreset(kBaseKitPreset, &baseName, &kBaseKitSample, 1)
		     && writeSampleSynthPreset(kNestedPreset, kNestedSample) && writeSampleSynthPreset(kRacePreset, kRaceSample)
		     && writeSampleSynthPreset(kGatePreset, kGateSample) && writeSampleSynthPreset(kStallPreset, kStallSample)
		     && writeKitPreset(kHeavyPreset, kHeavyDrumNames, kHeavySamples, 3)
		     && writeKitPreset(kLitePreset, &liteName, &kLiteSample, 1);
		if (ok) {
			// The new-kit-track gesture, which takes the first kit preset nobody in the song is using: PLBASE.
			g_kitClip = deluge::harness::createNewInstrumentClip(OutputType::KIT, 1);
		}
		for (Output* output = currentSong->firstOutput; output != nullptr && g_synth == nullptr;
		     output = output->next) {
			if (output->type == OutputType::SYNTH) {
				g_synth = static_cast<SoundInstrument*>(output);
			}
		}
		g_synthClip = (g_synth != nullptr) ? currentSong->getClipWithOutput(g_synth) : nullptr;
		printf("PL-PROBE prepared presets=%d kitClip=%d synthClip=%d\n", ok ? 1 : 0, g_kitClip != nullptr ? 1 : 0,
		       g_synthClip != nullptr ? 1 : 0);
		if (ok) {
			checkPresetWalk();
		}
	});
}

/// Whether the leg holds the loader from its selection on, and so acts without waiting for the scroll-load: the race
/// leg because not waiting is the point, the held legs because the wait is the hold. The supersession leg waits only
/// for the heavy kit's sample load to begin (see g_heavyLoadsBefore).
bool actsWithoutSettling() {
	return g_leg == Leg::Race || g_leg == Leg::Gate || g_leg == Leg::Stall;
}

/// Every instrument in the song, active or hibernating, named @p name.
int32_t instrumentsNamed(const char* name) {
	int32_t found = 0;
	for (Output* output = currentSong->firstOutput; output != nullptr; output = output->next) {
		found += (output->name == name) ? 1 : 0;
	}
	for (Output* output = currentSong->firstHibernatingInstrument; output != nullptr; output = output->next) {
		found += (output->name == name) ? 1 : 0;
	}
	return found;
}

/// Whether @p owner is the song's: one of its outputs, active or hibernating, or a drum of one of its kits.
bool ownedBySong(const ModControllableAudio* owner) {
	for (Output* list : {currentSong->firstOutput, static_cast<Output*>(currentSong->firstHibernatingInstrument)}) {
		for (Output* output = list; output != nullptr; output = output->next) {
			if (static_cast<ModControllableAudio*>(output->toModControllable()) == owner) {
				return true;
			}
			if (output->type == OutputType::KIT) {
				for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
					if (drum->type == DrumType::SOUND
					    && static_cast<ModControllableAudio*>(static_cast<SoundDrum*>(drum)) == owner) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

/// The song's backed-up param managers whose owner is not the song's: what an Instrument parsed and then dropped
/// without being deleted leaves behind.
int32_t orphanedParamManagers() {
	int32_t orphans = 0;
	for (const auto& [key, paramManager] : currentSong->backedUpParamManagers) {
		orphans += ownedBySong(key.first) ? 0 : 1;
	}
	return orphans;
}

/// The revert legs' first wait tick: whether the browser had already closed, and whether the revert holds the gate.
void tickRevert() {
	if (g_revertTried) {
		return;
	}
	g_revertTried = true;
	g_revertClosedAtOnce = !isUIOpen(&loadInstrumentPresetUI);
	g_revertGateClosed = currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
}

/// The gate leg's first wait tick: while the commit is still out, an output-type button and an encoder turn.
void tickGate() {
	if (g_gateTried) {
		return;
	}
	g_gateTried = true;
	g_gateWhileRunning = currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	if (!g_gateWhileRunning) {
		return;
	}
	using namespace deluge::hid::button;
	const OutputType typeBefore = Browser::outputTypeToLoad;
	(void)getCurrentUI()->buttonAction(KIT, true, false);
	(void)getCurrentUI()->buttonAction(KIT, false, false);
	g_gateTypeRefused = Browser::outputTypeToLoad == typeBefore;
	const FileItem* item = loadInstrumentPresetUI.getCurrentFileItem();
	const std::string before = item != nullptr ? item->filename.c_str() : "";
	(void)getCurrentUI()->selectEncoderAction(-1);
	item = loadInstrumentPresetUI.getCurrentFileItem();
	g_gateEncoderRefused = before == (item != nullptr ? item->filename.c_str() : "");
}

void actLeg() {
	using namespace deluge::hid::button;
	if (isRevertLeg(g_leg)) {
		// The mute pad, which is what backs out of the browser (padAction()'s potentiallyExit).
		(void)getCurrentUI()->padAction(kDisplayWidth, 0, 1);
		return;
	}
	if (g_leg == Leg::Supersede) {
		// On to the light kit before the heavy kit's samples land.
		(void)loadInstrumentPresetUI.setFileByFullPath(OutputType::NONE, kLitePreset);
		return;
	}
	if (g_leg == Leg::Clone || g_leg == Leg::Race) {
		// A long press on Select: the press, then what the UI timer's callback does once it has been held.
		(void)getCurrentUI()->buttonAction(SELECT_ENC, true, false);
		(void)static_cast<UI&>(loadInstrumentPresetUI).timerCallback();
		const bool menuOnTop = getCurrentUI() == &deluge::gui::context_menu::loadInstrumentPreset;
		printf("PL-SETUP leg=%s menuOnTop=%d\n", legName(g_leg), menuOnTop ? 1 : 0);
		if (menuOnTop) {
			pressSelect();
		}
		return;
	}
	pressSelect();
}

bool legEnded() {
	if (g_leg == Leg::MidiDef) {
		return !isUIOpen(&loadMidiDeviceDefinitionUI) && !deluge::gui::instrumentLoad.inFlight();
	}
	if (isRevertLeg(g_leg)) {
		return !isUIOpen(&loadInstrumentPresetUI) && !deluge::gui::instrumentLoad.inFlight();
	}
	if (g_leg == Leg::Supersede) {
		// The light kit's load settled, with the browser left open on it.
		return !Browser::isListingInProgress() && !loadInstrumentPresetUI.scrollLoadInFlight()
		       && static_cast<Instrument*>(g_kitClip->output)->name == kLiteName;
	}
	if (g_leg == Leg::Gate) {
		return g_gateTried && !isUIOpen(&loadInstrumentPresetUI);
	}
	return !isUIOpen(&loadInstrumentPresetUI);
}

/// PL-TIER: since the leg opened the browser, loads read documents and loaded samples, none of it begun on the storage
/// owner, and no sample was read from the card on the interaction tier.
void checkTier() {
	const TierMark now = markTier();
	const int32_t reads = now.documentReads - g_tierMark.documentReads;
	const int32_t readsOnOwner = now.documentReadsOnOwner - g_tierMark.documentReadsOnOwner;
	const int32_t loads = now.sampleLoads - g_tierMark.sampleLoads;
	const int32_t loadsOnOwner = now.sampleLoadsOnOwner - g_tierMark.sampleLoadsOnOwner;
	const int32_t sampleIo = now.sampleIo - g_tierMark.sampleIo;
	printf("PL-TIER leg=%s documentReads=%d readsOnOwner=%d sampleLoads=%d loadsOnOwner=%d sampleIo=%d\n",
	       legName(g_leg), reads, readsOnOwner, loads, loadsOnOwner, sampleIo);
	if (reads == 0 || loads == 0) {
		printf("PL-TIER FAIL leg=%s the leg read no document or loaded no samples\n", legName(g_leg));
		g_failures++;
	}
	if (readsOnOwner != 0 || loadsOnOwner != 0 || sampleIo != 0) {
		printf(
		    "PL-TIER FAIL leg=%s %d document read(s) and %d sample load(s) began on the storage owner, and %d sample "
		    "read(s) went to the card on the interaction tier -- a preset load belongs on the interaction tier\n",
		    legName(g_leg), readsOnOwner, loadsOnOwner, sampleIo);
		g_failures++;
	}
}

/// Whether @p holder is bound to @p path.
bool boundTo(AudioFileHolder* holder, const char* path) {
	return holder != nullptr && holder->audioFile != nullptr && holder->filePath == path;
}

/// The revert legs: what the exit put back, and who read it.
void checkRevert() {
	auto* instrument = static_cast<Instrument*>(g_synthClip->output);
	AudioFileHolder* holder = (instrument->type == OutputType::SYNTH)
	                              ? firstHolder(static_cast<SoundInstrument*>(instrument)->sources[0])
	                              : nullptr;
	const bool reverted = instrument->name == g_revertName;
	const bool bound = boundTo(holder, g_revertSample.c_str());
	const bool holderEmpty = holder != nullptr && holder->audioFile == nullptr;
	const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	const int32_t giveUps = deluge::gui::instrumentLoad.giveUps() - g_revertGiveUpsBefore;
	if (g_leg == Leg::RevertCard) {
		printf("RV-CARD reverted=%d bound=%d closedAtOnce=%d gateClosed=%d released=%d name=%s ticks=%d\n",
		       reverted ? 1 : 0, bound ? 1 : 0, g_revertClosedAtOnce ? 1 : 0, g_revertGateClosed ? 1 : 0,
		       released ? 1 : 0, instrument->name.c_str(), g_waitTicks);
		if (!reverted || !bound || !g_revertClosedAtOnce || !g_revertGateClosed || !released) {
			printf("RV-CARD FAIL backing out did not close the browser at once and put the original back off the "
			       "storage owner with its gate closed while it loaded\n");
			g_failures++;
		}
		return;
	}
	if (g_leg == Leg::RevertMemory) {
		printf("RV-MEMORY reverted=%d bound=%d closedAtOnce=%d name=%s ticks=%d\n", reverted ? 1 : 0, bound ? 1 : 0,
		       g_revertClosedAtOnce ? 1 : 0, instrument->name.c_str(), g_waitTicks);
		if (!reverted || !bound || !g_revertClosedAtOnce) {
			printf("RV-MEMORY FAIL backing out did not put the hibernating original back, with no document read at "
			       "all\n");
			g_failures++;
		}
		return;
	}
	deluge_audio_load_set_latency_ms(0);
	printf("RV-STALL reverted=%d holderEmpty=%d released=%d giveUps=%d name=%s\n", reverted ? 1 : 0,
	       holderEmpty ? 1 : 0, released ? 1 : 0, giveUps, instrument->name.c_str());
	if (!reverted || !holderEmpty || !released || giveUps != 1) {
		printf("RV-STALL FAIL a revert whose sample load never answered did not put the original back with that "
		       "holder empty on the stall bound\n");
		g_failures++;
	}
}

/// MD-BROWSER: the definition the browser loaded, and who staged it.
void checkMidiDefinition() {
	auto* output = g_synthClip->output;
	const bool isMidi = output->type == OutputType::MIDI_OUT;
	const bool labelled =
	    isMidi && static_cast<MIDIInstrument*>(output)->getNameFromCC(kMidiDefinitionCC) == kMidiDefinitionLabel;
	const bool closed = !isUIOpen(&loadMidiDeviceDefinitionUI);
	const int32_t reads = deluge::gui::instrumentLoad.documentReads() - g_midiDefReadsBefore;
	const int32_t onOwner = deluge::gui::instrumentLoad.documentReadsOnOwner() - g_midiDefOwnerReadsBefore;
	printf("MD-BROWSER midi=%d labelled=%d closed=%d reads=%d onOwner=%d ticks=%d\n", isMidi ? 1 : 0, labelled ? 1 : 0,
	       closed ? 1 : 0, reads, onOwner, g_waitTicks);
	if (!isMidi || !labelled || !closed || reads != 1 || onOwner != 0) {
		printf("MD-BROWSER FAIL the definition browser did not stage its file through InstrumentLoad and label the "
		       "instrument from it\n");
		g_failures++;
	}
}

void checkLeg() {
	if (g_leg == Leg::MidiDef) {
		// Nothing of the preset kind is read: a definition carries no samples.
		checkMidiDefinition();
		return;
	}
	if (g_leg == Leg::RevertMemory) {
		// Nothing is read: its original hibernates in memory.
		checkRevert();
		return;
	}
	checkTier();
	if (isRevertLeg(g_leg)) {
		checkRevert();
		return;
	}
	const bool browserClosed = !isUIOpen(&loadInstrumentPresetUI);
	switch (g_leg) {
	case Leg::Race: {
		auto* instrument = static_cast<Instrument*>(g_synthClip->output);
		const bool isSynth = instrument->type == OutputType::SYNTH;
		AudioFileHolder* holder =
		    isSynth ? firstHolder(static_cast<SoundInstrument*>(instrument)->sources[0]) : nullptr;
		const bool renamed = instrument->name != kRaceName && instrument->name.starts_with(kRaceName);
		const bool bound = boundTo(holder, kRaceSample);
		printf("PL-PROBE leg=race name=%s edited=%d\n", instrument->name.c_str(), instrument->editedByUser ? 1 : 0);
		printf("PL-RACE renamed=%d bound=%d edited=%d browserClosed=%d ticks=%d\n", renamed ? 1 : 0, bound ? 1 : 0,
		       instrument->editedByUser ? 1 : 0, browserClosed ? 1 : 0, g_waitTicks);
		if (!isSynth || !renamed || !bound || !instrument->editedByUser || !browserClosed) {
			printf("PL-RACE FAIL a clone made while the selection's scroll-load was still out did not survive it\n");
			g_failures++;
		}
		break;
	}
	case Leg::Supersede: {
		deluge_audio_load_set_latency_ms(0);
		auto* kit = static_cast<Instrument*>(g_kitClip->output);
		const bool lite = kit->name == kLiteName;
		const int32_t heavyLeft = instrumentsNamed(kHeavyName);
		const bool outputsKept = countOutputs() == g_outputsBefore;
		const int32_t orphansLeft = orphanedParamManagers() - g_orphansBefore;
		printf("PL-SUPERSEDE lite=%d heavySwapped=%d heavyLeft=%d orphansLeft=%d outputsKept=%d ticks=%d\n",
		       lite ? 1 : 0, g_heavySwapped ? 1 : 0, heavyLeft, orphansLeft, outputsKept ? 1 : 0, g_waitTicks);
		if (!lite || g_heavySwapped || heavyLeft != 0 || orphansLeft != 0 || !outputsKept) {
			printf("PL-SUPERSEDE FAIL a scroll that moved on before the heavy kit's samples landed still swapped the "
			       "heavy "
			       "kit in, or left it in the song\n");
			g_failures++;
		}
		loadInstrumentPresetUI.close();
		break;
	}
	case Leg::Gate: {
		deluge_audio_load_set_latency_ms(0);
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		auto* instrument = static_cast<Instrument*>(g_synthClip->output);
		const bool loaded = instrument->name == kGateName;
		printf("PL-GATE whileRunning=%d typeRefused=%d encoderRefused=%d released=%d loaded=%d browserClosed=%d "
		       "ticks=%d\n",
		       g_gateWhileRunning ? 1 : 0, g_gateTypeRefused ? 1 : 0, g_gateEncoderRefused ? 1 : 0, released ? 1 : 0,
		       loaded ? 1 : 0, browserClosed ? 1 : 0, g_waitTicks);
		if (!g_gateWhileRunning) {
			printf(
			    "PL-GATE FAIL the commit had ended before its held sample could land: a commit must wait on its load "
			    "with the gate closed\n");
			g_failures++;
		}
		else if (!g_gateTypeRefused || !g_gateEncoderRefused || !released || !loaded || !browserClosed) {
			printf(
			    "PL-GATE FAIL an output-type button or an encoder turn was taken while the commit was out, or the gate "
			    "stayed closed, or the commit did not finish\n");
			g_failures++;
		}
		break;
	}
	case Leg::Stall: {
		deluge_audio_load_set_latency_ms(0);
		auto* instrument = static_cast<Instrument*>(g_synthClip->output);
		const bool swapped = instrument->name == kStallName;
		AudioFileHolder* holder = (instrument->type == OutputType::SYNTH)
		                              ? firstHolder(static_cast<SoundInstrument*>(instrument)->sources[0])
		                              : nullptr;
		const bool holderEmpty = holder != nullptr && holder->audioFile == nullptr;
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		// How the load ended: on the async sample load's stall bound, not on some other failure.
		printf("PL-PROBE leg=stall giveUps=%d supersessions=%d\n", deluge::gui::instrumentLoad.giveUps(),
		       deluge::gui::instrumentLoad.supersessions());
		printf("PL-STALL swapped=%d holderEmpty=%d released=%d browserClosed=%d\n", swapped ? 1 : 0,
		       holderEmpty ? 1 : 0, released ? 1 : 0, browserClosed ? 1 : 0);
		if (!swapped || !holderEmpty || !released || !browserClosed) {
			printf("PL-STALL FAIL a commit whose sample load never answered did not keep its instrument: it must be "
			       "swapped in with that holder empty, the gate released and the browser closed\n");
			g_failures++;
		}
		break;
	}
	case Leg::Synth:
	case Leg::Clone: {
		auto* instrument = static_cast<Instrument*>(g_synthClip->output);
		const bool isSynth = instrument->type == OutputType::SYNTH;
		AudioFileHolder* holder =
		    isSynth ? firstHolder(static_cast<SoundInstrument*>(instrument)->sources[0]) : nullptr;
		const char* wantSample = (g_leg == Leg::Synth) ? kSynthSample : kCloneSample;
		const bool bound = boundTo(holder, wantSample);
		printf("PL-PROBE leg=%s name=%s path=%s outputs=%d\n", legName(g_leg), instrument->name.c_str(),
		       holder != nullptr ? holder->filePath.c_str() : "", countOutputs());
		if (g_leg == Leg::Synth) {
			const bool named = instrument->name == kSynthName;
			printf("PL-SYNTH synth=%d named=%d bound=%d browserClosed=%d ticks=%d\n", isSynth ? 1 : 0, named ? 1 : 0,
			       bound ? 1 : 0, browserClosed ? 1 : 0, g_waitTicks);
			if (!isSynth || !named || !bound || !browserClosed) {
				printf(
				    "PL-SYNTH FAIL the clip's instrument is not the loaded synth with its sample bound, or the browser "
				    "is still open\n");
				g_failures++;
			}
		}
		else {
			// A clone takes an unused variation of the preset's name, never the name itself.
			const bool renamed = instrument->name != kCloneName && instrument->name.starts_with(kCloneName);
			printf("PL-CLONE synth=%d renamed=%d bound=%d edited=%d browserClosed=%d ticks=%d\n", isSynth ? 1 : 0,
			       renamed ? 1 : 0, bound ? 1 : 0, instrument->editedByUser ? 1 : 0, browserClosed ? 1 : 0,
			       g_waitTicks);
			if (!isSynth || !renamed || !bound || !instrument->editedByUser || !browserClosed) {
				printf("PL-CLONE FAIL the clone is not a new synth under a variation of the preset's name with its "
				       "sample bound, or the browser is still open\n");
				g_failures++;
			}
		}
		break;
	}
	case Leg::Kit: {
		auto* kit = static_cast<Kit*>(g_kitClip->output);
		const bool isKit = kit->type == OutputType::KIT;
		int32_t drums = 0;
		int32_t matched = 0;
		for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next, drums++) {
			if (drum->type != DrumType::SOUND || drums >= kKitDrumCount) {
				continue;
			}
			auto* soundDrum = static_cast<SoundDrum*>(drum);
			AudioFileHolder* holder = firstHolder(soundDrum->sources[0]);
			printf("PL-PROBE leg=kit drum=%d name=%s path=%s bound=%d\n", drums, soundDrum->drumName.c_str(),
			       holder != nullptr ? holder->filePath.c_str() : "",
			       (holder != nullptr && holder->audioFile != nullptr) ? 1 : 0);
			if (soundDrum->drumName == kKitDrumNames[drums] && boundTo(holder, kKitSamples[drums])) {
				matched++;
			}
		}
		auto* clip = static_cast<InstrumentClip*>(g_kitClip);
		int32_t rowsWithDrum = 0;
		for (int32_t i = 0; i < std::ssize(clip->noteRows); i++) {
			if (clip->noteRows[i].drum != nullptr) {
				rowsWithDrum++;
			}
		}
		const bool named = kit->name == kKitName;
		printf("PL-KIT kit=%d named=%d drums=%d matched=%d rowsWithDrum=%d browserClosed=%d ticks=%d\n", isKit ? 1 : 0,
		       named ? 1 : 0, drums, matched, rowsWithDrum, browserClosed ? 1 : 0, g_waitTicks);
		if (!isKit || !named || drums != kKitDrumCount || matched != kKitDrumCount || rowsWithDrum < kKitDrumCount
		    || !browserClosed) {
			printf("PL-KIT FAIL the clip's instrument is not the loaded kit with each drum bound to its sample and a "
			       "NoteRow, or the browser is still open\n");
			g_failures++;
		}
		break;
	}
	case Leg::Row: {
		auto* clip = static_cast<InstrumentClip*>(g_kitClip);
		Drum* rowDrum =
		    (g_rowIndex >= 0 && g_rowIndex < std::ssize(clip->noteRows)) ? clip->noteRows[g_rowIndex].drum : nullptr;
		SoundDrum* drum =
		    (rowDrum != nullptr && rowDrum->type == DrumType::SOUND) ? static_cast<SoundDrum*>(rowDrum) : nullptr;
		AudioFileHolder* holder = (drum != nullptr) ? firstHolder(drum->sources[0]) : nullptr;
		const bool named = drum != nullptr && drum->drumName == kRowName;
		const bool bound = boundTo(holder, kRowSample);
		printf("PL-PROBE leg=row name=%s path=%s\n", drum != nullptr ? drum->drumName.c_str() : "",
		       holder != nullptr ? holder->filePath.c_str() : "");
		printf("PL-ROW named=%d bound=%d browserClosed=%d ticks=%d\n", named ? 1 : 0, bound ? 1 : 0,
		       browserClosed ? 1 : 0, g_waitTicks);
		if (!named || !bound || !browserClosed) {
			printf("PL-ROW FAIL the kit row's drum is not the loaded synth, named after it with its sample bound, or "
			       "the browser is still open\n");
			g_failures++;
		}
		break;
	}
	case Leg::AqNew:
	case Leg::AqChange:
	case Leg::AqNavigate:
	case Leg::AqSupersede:
	case Leg::AqRefuse:
	case Leg::AqClone:
	case Leg::AqStall:
	case Leg::Done:
		break;
	}
}

// ── The acquisition legs ─────────────────────────────────────────────────────

/// The acquisition legs' presets, written when AQ-NEW prepares (after the browser legs, whose walks they would
/// otherwise join): a synth the new-track gesture picks up, a kit the change-type lands on and three navigated through,
/// a heavy kit and the light one after it, and the stall leg's synth, which sorts straight after PLSTALL so a turn from
/// the synth clip lands on it. Every other name sorts before the PL presets of its type.
constexpr const char* kAqSynthPreset = "SYNTHS/AQSA.XML";
constexpr const char* kAqSynthName = "AQSA";
constexpr const char* kAqSynthSample = "SAMPLES/AQ/AQSA.WAV";
constexpr const char* kAqKitPresets[] = {"KITS/AQKA.XML", "KITS/AQKB.XML", "KITS/AQKC.XML", "KITS/AQKD.XML"};
constexpr const char* kAqKitNames[] = {"AQKA", "AQKB", "AQKC", "AQKD"};
constexpr const char* kAqKitSamples[] = {"SAMPLES/AQ/AQKA.WAV", "SAMPLES/AQ/AQKB.WAV", "SAMPLES/AQ/AQKC.WAV",
                                         "SAMPLES/AQ/AQKD.WAV"};
constexpr int32_t kAqKitCount = 4;
constexpr const char* kAqHeavyPreset = "KITS/AQKH.XML";
constexpr const char* kAqHeavyName = "AQKH";
constexpr const char* kAqHeavySamples[] = {"SAMPLES/AQH/AQHA.WAV", "SAMPLES/AQH/AQHB.WAV", "SAMPLES/AQH/AQHC.WAV"};
constexpr const char* kAqHeavyDrumNames[] = {"AQHA", "AQHB", "AQHC"};
constexpr const char* kAqLitePreset = "KITS/AQKL.XML";
constexpr const char* kAqLiteName = "AQKL";
constexpr const char* kAqLiteSample = "SAMPLES/AQ/AQKL.WAV";
constexpr const char* kAqStallPreset = "SYNTHS/PLSTB.XML";
constexpr const char* kAqStallName = "PLSTB";
constexpr const char* kAqStallSample = "SAMPLES/AQS/PLSTB.WAV";

enum class AqStep : uint8_t { Prepare, Act, Wait };
AqStep g_aqStep = AqStep::Prepare;
/// Which of the leg's gestures is out.
int32_t g_aqAct = 0;
/// The clip AQ-NEW's new-track gesture makes, which AQ-CHANGE, AQ-NAVIGATE, AQ-SUPERSEDE and AQ-CLONE act on.
Clip* g_aqClip = nullptr;
/// The session's clips, the clip's instrument name, the acquisition runs, supersessions, give-ups and orphaned param
/// managers, read as the gesture is made.
std::vector<Clip*> g_aqClipsBefore{};
std::string g_aqNameBefore{};
int32_t g_aqRunsBefore = 0;
int32_t g_aqSupersessionsBefore = 0;
int32_t g_aqGiveUpsBefore = 0;
int32_t g_aqOrphansBefore = 0;
/// AQ-SUPERSEDE: the sample loads begun before its first turn, whether its second turn has been made, and whether the
/// heavy kit was ever the clip's.
int32_t g_aqLoadsBefore = 0;
bool g_aqTurned = false;
bool g_aqHeavySwapped = false;
TierMark g_aqTierMark{};

bool isAcquireLeg(Leg leg) {
	switch (leg) {
	case Leg::AqNew:
	case Leg::AqChange:
	case Leg::AqNavigate:
	case Leg::AqSupersede:
	case Leg::AqRefuse:
	case Leg::AqClone:
	case Leg::AqStall:
		return true;
	default:
		return false;
	}
}

/// How many gestures the leg makes, one after another.
int32_t acquireActs(Leg leg) {
	return leg == Leg::AqNavigate ? 3 : leg == Leg::AqNew ? 2 : 1;
}

/// What the acquisition legs count: InstrumentLoad's own reads and loads, which is all there is to count now.
TierMark markAcquireTier() {
	return markTier();
}

std::vector<Clip*> sessionClipList() {
	std::vector<Clip*> clips;
	for (int32_t i = 0; i < std::ssize(currentSong->sessionClips); i++) {
		clips.push_back(currentSong->sessionClips[i]);
	}
	return clips;
}

/// The session clips made since the gesture, in session order.
std::vector<Clip*> clipsAdded() {
	std::vector<Clip*> added;
	for (Clip* clip : sessionClipList()) {
		if (std::find(g_aqClipsBefore.begin(), g_aqClipsBefore.end(), clip) == g_aqClipsBefore.end()) {
			added.push_back(clip);
		}
	}
	return added;
}

/// Make @p clip current under the clip view, with no mode held, as a user who has opened it does.
bool focusClip(Clip* clip) {
	if (clip == nullptr || clip->output == nullptr) {
		return false;
	}
	currentSong->setCurrentClip(clip);
	changeRootUI(&instrumentClipView);
	currentUIMode = UI_MODE_NONE;
	return true;
}

void prepareAcquire() {
	g_aqAct = 0;
	g_aqTierMark = markAcquireTier();
	if (g_leg != Leg::AqNew) {
		return;
	}
	onOwner([] {
		bool ok = writeSampleSynthPreset(kAqSynthPreset, kAqSynthSample)
		          && writeSampleSynthPreset(kAqStallPreset, kAqStallSample);
		for (int32_t i = 0; ok && i < kAqKitCount; i++) {
			ok = writeKitPreset(kAqKitPresets[i], &kAqKitNames[i], &kAqKitSamples[i], 1);
		}
		const char* liteName = kAqLiteName;
		ok = ok && writeKitPreset(kAqHeavyPreset, kAqHeavyDrumNames, kAqHeavySamples, 3)
		     && writeKitPreset(kAqLitePreset, &liteName, &kAqLiteSample, 1);
		printf("AQ-PROBE prepared presets=%d\n", ok ? 1 : 0);
	});
}

/// The first Rows-layout pad with no clip on it.
int32_t emptyRow() {
	return static_cast<int32_t>(std::ssize(currentSong->sessionClips));
}

/// Make the leg's gesture. False when it could not be made, recorded as a failure.
bool actAcquire() {
	using namespace deluge::hid::button;
	const char* leg = legName(g_leg);
	g_aqClipsBefore = sessionClipList();
	g_aqRunsBefore = LoadInstrumentPresetUI::presetAcquisitionsRun();
	g_aqSupersessionsBefore = deluge::gui::instrumentLoad.supersessions();
	g_aqGiveUpsBefore = deluge::gui::instrumentLoad.giveUps();
	g_aqNameBefore = (g_aqClip != nullptr && g_aqClip->output != nullptr) ? g_aqClip->output->name : "";
	switch (g_leg) {
	case Leg::AqNew:
		if (g_aqAct == 0) {
			PresetAcquireProbe::pressEmptyRow(emptyRow());
			return true;
		}
		if (!focusClip(g_aqClip)) {
			return fail(leg, "the new-track gesture made no clip");
		}
		// shift+SYNTH on the clip view.
		(void)instrumentClipView.createNewInstrument(OutputType::SYNTH, false);
		return true;
	case Leg::AqChange:
		if (!focusClip(g_aqClip)) {
			return fail(leg, "the new-track gesture made no clip");
		}
		(void)instrumentClipView.buttonAction(KIT, true, false);
		(void)instrumentClipView.buttonAction(KIT, false, false);
		return true;
	case Leg::AqNavigate:
		if (g_aqAct == 0) {
			if (!focusClip(g_aqClip)) {
				return fail(leg, "the new-track gesture made no clip");
			}
			instrumentClipView.selectEncoderAction(1);
			return true;
		}
		if (g_aqAct == 1) {
			return PresetAcquireProbe::turnInGrid(g_aqClip, 1) || fail(leg, "the clip's grid pad could not be held");
		}
		PresetAcquireProbe::turnInArranger(g_aqClip->output, 1);
		return true;
	case Leg::AqSupersede:
		if (!focusClip(g_aqClip)) {
			return fail(leg, "the new-track gesture made no clip");
		}
		deluge_audio_load_set_latency_ms(kHoldMs);
		g_aqOrphansBefore = orphanedParamManagers();
		g_aqLoadsBefore = markAcquireTier().sampleLoads;
		g_aqTurned = false;
		g_aqHeavySwapped = false;
		instrumentClipView.selectEncoderAction(1);
		return true;
	case Leg::AqRefuse:
		// A synth track, a kit track and a synth track: a third press that replaced the queued second would leave no
		// kit.
		PresetAcquireProbe::pressEmptyRow(emptyRow(), OutputType::SYNTH);
		PresetAcquireProbe::pressEmptyRow(emptyRow() + 1, OutputType::KIT);
		PresetAcquireProbe::pressEmptyRow(emptyRow() + 2, OutputType::SYNTH);
		return true;
	case Leg::AqClone:
		if (g_aqClip == nullptr) {
			return fail(leg, "the new-track gesture made no clip");
		}
		PresetAcquireProbe::pressEmptyRow(emptyRow());
		return PresetAcquireProbe::cloneIntoEmptyColumn(g_aqClip) || fail(leg, "the clip has no grid pad to copy");
	case Leg::AqStall:
		if (!focusClip(g_synthClip)) {
			return fail(leg, "the synth clip is missing");
		}
		deluge_audio_load_set_latency_ms(kStallHoldMs);
		g_aqNameBefore = g_synthClip->output->name;
		instrumentClipView.selectEncoderAction(1);
		return true;
	default:
		return false;
	}
}

/// How many of @p output's first @p drums drums are sound drums bound to their sample in @p samples.
int32_t kitDrumsBound(Output* output, const char* const* samples, int32_t drums) {
	if (output == nullptr || output->type != OutputType::KIT) {
		return 0;
	}
	int32_t bound = 0;
	int32_t index = 0;
	for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr && index < drums;
	     drum = drum->next, index++) {
		if (drum->type == DrumType::SOUND
		    && boundTo(firstHolder(static_cast<SoundDrum*>(drum)->sources[0]), samples[index])) {
			bound++;
		}
	}
	return bound;
}

bool synthBoundTo(Output* output, const char* sample) {
	return output != nullptr && output->type == OutputType::SYNTH
	       && boundTo(firstHolder(static_cast<SoundInstrument*>(output)->sources[0]), sample);
}

void aqFail(const char* marker, const char* what) {
	printf("%s FAIL %s\n", marker, what);
	g_failures++;
}

/// Check the gesture's outcome, once its acquisition has ended.
void checkAcquireAct() {
	const std::vector<Clip*> added = clipsAdded();
	const int32_t runs = LoadInstrumentPresetUI::presetAcquisitionsRun() - g_aqRunsBefore;
	Output* output = (g_aqClip != nullptr) ? g_aqClip->output : nullptr;
	const std::string name = (output != nullptr) ? output->name : "";
	switch (g_leg) {
	case Leg::AqNew: {
		if (g_aqAct == 0) {
			g_aqClip = (added.size() == 1) ? added[0] : nullptr;
			Output* created = (g_aqClip != nullptr) ? g_aqClip->output : nullptr;
			const bool named = created != nullptr && created->name == kAqSynthName;
			const bool bound = synthBoundTo(created, kAqSynthSample);
			printf("AQ-NEW gesture=track clipsAdded=%d named=%d bound=%d name=%s runs=%d\n",
			       static_cast<int32_t>(added.size()), named ? 1 : 0, bound ? 1 : 0,
			       created != nullptr ? created->name.c_str() : "", runs);
			PresetAcquireProbe::releasePress();
			if (added.size() != 1 || !named || !bound) {
				aqFail("AQ-NEW", "the new-track gesture did not make one synth track on the first unused synth preset "
				                 "with its sample bound");
			}
			break;
		}
		const bool fresh = output != nullptr && output->type == OutputType::SYNTH && name != g_aqNameBefore
		                   && name.starts_with("SYNT");
		printf("AQ-NEW gesture=instrument fresh=%d before=%s name=%s runs=%d\n", fresh ? 1 : 0, g_aqNameBefore.c_str(),
		       name.c_str(), runs);
		if (!fresh) {
			aqFail("AQ-NEW", "shift+SYNTH did not give the clip a new synth under an unused slot name");
		}
		break;
	}
	case Leg::AqChange: {
		const bool isKit = output != nullptr && output->type == OutputType::KIT;
		const bool named = name == kAqKitNames[0];
		const int32_t bound = kitDrumsBound(output, &kAqKitSamples[0], 1);
		printf("AQ-CHANGE kit=%d named=%d bound=%d name=%s runs=%d\n", isKit ? 1 : 0, named ? 1 : 0, bound,
		       name.c_str(), runs);
		if (!isKit || !named || bound != 1) {
			aqFail("AQ-CHANGE",
			       "the KIT button did not land the clip on the first unused kit preset with its drum bound");
		}
		break;
	}
	case Leg::AqNavigate: {
		static constexpr const char* kGestures[] = {"clip", "grid", "arranger"};
		const bool landed = name == kAqKitNames[g_aqAct + 1];
		const int32_t bound = kitDrumsBound(output, &kAqKitSamples[g_aqAct + 1], 1);
		printf("AQ-NAVIGATE gesture=%s before=%s name=%s landed=%d bound=%d runs=%d\n", kGestures[g_aqAct],
		       g_aqNameBefore.c_str(), name.c_str(), landed ? 1 : 0, bound, runs);
		if (g_aqAct == 1) {
			PresetAcquireProbe::releasePress();
		}
		if (g_aqAct == 2) {
			PresetAcquireProbe::endArrangerAudition();
		}
		if (!landed || bound != 1) {
			aqFail("AQ-NAVIGATE", "a turn did not land the clip on the next unused kit preset with its drum bound");
		}
		break;
	}
	case Leg::AqSupersede: {
		deluge_audio_load_set_latency_ms(0);
		const bool lite = name == kAqLiteName;
		const int32_t heavyLeft = instrumentsNamed(kAqHeavyName);
		const int32_t orphansLeft = orphanedParamManagers() - g_aqOrphansBefore;
		const int32_t supersessions = deluge::gui::instrumentLoad.supersessions() - g_aqSupersessionsBefore;
		printf("AQ-SUPERSEDE lite=%d heavySwapped=%d heavyLeft=%d orphansLeft=%d supersessions=%d name=%s runs=%d\n",
		       lite ? 1 : 0, g_aqHeavySwapped ? 1 : 0, heavyLeft, orphansLeft, supersessions, name.c_str(), runs);
		if (!lite || g_aqHeavySwapped || heavyLeft != 0 || orphansLeft != 0 || supersessions < 1) {
			aqFail("AQ-SUPERSEDE",
			       "a turn made while the heavy kit's samples were loading did not cancel it: the heavy "
			       "kit swapped in, or something of it was left, or the clip missed the light kit");
		}
		break;
	}
	case Leg::AqRefuse: {
		int32_t synths = 0;
		int32_t kits = 0;
		for (Clip* clip : added) {
			synths += (clip->output != nullptr && clip->output->type == OutputType::SYNTH) ? 1 : 0;
			kits += (clip->output != nullptr && clip->output->type == OutputType::KIT) ? 1 : 0;
		}
		printf("AQ-REFUSE clipsAdded=%d synths=%d kits=%d runs=%d\n", static_cast<int32_t>(added.size()), synths, kits,
		       runs);
		PresetAcquireProbe::releasePress();
		if (added.size() != 2 || synths != 1 || kits != 1 || runs != 2) {
			aqFail("AQ-REFUSE", "three new-track presses in one tick did not keep the in-flight and the queued one and "
			                    "refuse the third");
		}
		break;
	}
	case Leg::AqClone: {
		int32_t synths = 0;
		int32_t kits = 0;
		for (Clip* clip : added) {
			synths += (clip->output != nullptr && clip->output->type == OutputType::SYNTH) ? 1 : 0;
			kits += (clip->output != nullptr && clip->output->type == OutputType::KIT) ? 1 : 0;
		}
		printf("AQ-CLONE clipsAdded=%d synths=%d kits=%d runs=%d\n", static_cast<int32_t>(added.size()), synths, kits,
		       runs);
		PresetAcquireProbe::releasePress();
		if (added.size() != 2 || synths != 1 || kits != 1 || runs != 2) {
			aqFail("AQ-CLONE", "a new-track press and a grid clone made in one tick did not both make their track");
		}
		break;
	}
	case Leg::AqStall: {
		deluge_audio_load_set_latency_ms(0);
		Output* synth = g_synthClip->output;
		const bool named = synth->name == kAqStallName;
		AudioFileHolder* holder = (synth->type == OutputType::SYNTH)
		                              ? firstHolder(static_cast<SoundInstrument*>(synth)->sources[0])
		                              : nullptr;
		const bool holderEmpty = holder != nullptr && holder->audioFile == nullptr;
		const int32_t giveUps = deluge::gui::instrumentLoad.giveUps() - g_aqGiveUpsBefore;
		printf("AQ-STALL named=%d holderEmpty=%d giveUps=%d before=%s name=%s runs=%d\n", named ? 1 : 0,
		       holderEmpty ? 1 : 0, giveUps, g_aqNameBefore.c_str(), synth->name.c_str(), runs);
		if (!named || !holderEmpty || giveUps != 1) {
			aqFail("AQ-STALL", "a turn whose sample load never answered did not deliver the synth with that holder "
			                   "empty on the stall bound");
		}
		break;
	}
	default:
		break;
	}
}

/// AQ-TIER: since the leg began, its acquisitions read documents and loaded samples, none of it begun on the storage
/// owner, and no sample was read from the card on the interaction tier.
void checkAcquireTier() {
	const TierMark now = markAcquireTier();
	const int32_t reads = now.documentReads - g_aqTierMark.documentReads;
	const int32_t readsOnOwner = now.documentReadsOnOwner - g_aqTierMark.documentReadsOnOwner;
	const int32_t loads = now.sampleLoads - g_aqTierMark.sampleLoads;
	const int32_t loadsOnOwner = now.sampleLoadsOnOwner - g_aqTierMark.sampleLoadsOnOwner;
	const int32_t sampleIo = now.sampleIo - g_aqTierMark.sampleIo;
	printf("AQ-TIER leg=%s documentReads=%d readsOnOwner=%d sampleLoads=%d loadsOnOwner=%d sampleIo=%d\n",
	       legName(g_leg), reads, readsOnOwner, loads, loadsOnOwner, sampleIo);
	if (reads == 0 || loads == 0) {
		printf("AQ-TIER FAIL leg=%s the leg read no document or loaded no samples\n", legName(g_leg));
		g_failures++;
	}
	if (readsOnOwner != 0 || loadsOnOwner != 0 || sampleIo != 0) {
		printf("AQ-TIER FAIL leg=%s %d document read(s) and %d sample load(s) began on the storage owner, and %d "
		       "sample read(s) went to the card on the interaction tier -- an acquisition's load belongs on the "
		       "interaction tier\n",
		       legName(g_leg), readsOnOwner, loadsOnOwner, sampleIo);
		g_failures++;
	}
}

void nextLeg();

void runAcquireTick() {
	switch (g_aqStep) {
	case AqStep::Prepare:
		prepareAcquire();
		g_aqStep = AqStep::Act;
		break;
	case AqStep::Act:
		g_waitTicks = 0;
		if (!actAcquire()) {
			nextLeg();
			break;
		}
		g_aqStep = AqStep::Wait;
		break;
	case AqStep::Wait: {
		if (g_leg == Leg::AqSupersede) {
			if (g_aqClip->output->name == kAqHeavyName) {
				g_aqHeavySwapped = true;
			}
			if (!g_aqTurned && markAcquireTier().sampleLoads > g_aqLoadsBefore) {
				// On past the heavy kit, once its sample load has begun.
				(void)focusClip(g_aqClip);
				instrumentClipView.selectEncoderAction(1);
				g_aqTurned = true;
				break;
			}
		}
		const int32_t maxTicks = (g_leg == Leg::AqStall) ? kMaxStallTicks : kMaxLegTicks;
		const bool ended = g_waitTicks >= 1 && !loadInstrumentPresetUI.presetAcquisitionInFlight()
		                   && (g_leg != Leg::AqSupersede || g_aqTurned);
		if (!ended && ++g_waitTicks <= maxTicks) {
			break;
		}
		if (!ended) {
			fail(legName(g_leg), "an acquisition never ended");
		}
		checkAcquireAct();
		if (++g_aqAct < acquireActs(g_leg)) {
			g_aqStep = AqStep::Act;
			break;
		}
		checkAcquireTier();
		nextLeg();
		break;
	}
	}
}

/// Where a blank-song leg has got to: the boot leg only ever waits.
enum class BlankStep : uint8_t { Prepare, Act, Wait };
BlankStep g_blankStep{BlankStep::Prepare};
int32_t g_blankRunsBefore = 0;
int32_t g_blankReadsBefore = 0;
int32_t g_blankOwnerReadsBefore = 0;
bool g_blankEdited = false;

/// Whether @p leg is one of the blank-song legs, which drive no browser.
bool isBlankLeg(Leg leg) {
	return leg == Leg::BlankBoot || leg == Leg::BlankClear || leg == Leg::BlankEdited;
}

/// @name The preset search, run through PresetWalk
///
/// The PL-WALK legs call `findAnUnlaunchedPresetIncludingWithinSubfolders` directly from inside an
/// `onOwner` block, so they pin the recursive walk and CANNOT see the machine that replaces it. They
/// are not repointed: the recursive walk survives this pilot -- `confirmPresetOrNextUnlaunchedOne`
/// still calls it -- so it still needs its coverage. These five legs sit beside them instead, over
/// the same fixture with the same assertions, so one run carries both drivers and the pair is a
/// differential rather than a replacement.
///
/// ⚠️ PW-WALK-TIER is the one that reads differently from its PL sibling. PL-WALK-TIER passes
/// because the walk runs ON the owner; this one passes because the machine runs on the INTERACTION
/// tier and defers instead of blocking. Same number, opposite reason -- which is the whole point.
/// @{
deluge::gui::PresetWalk g_presetWalk;

enum class WalkMachineStep : uint8_t { ArmFound, RunFound, ArmExhaust, RunExhaust, Report };
WalkMachineStep g_walkStep = WalkMachineStep::ArmFound;
std::string g_walkSavedDir;
std::string g_walkFoundName;
bool g_walkFound = false;
Error g_walkExhaustError = Error::NONE;
bool g_walkExhausted = false;
int32_t g_walkTierBefore = 0;
/// Ticks the search spanned. Zero would mean it never suspended, which would make PW-WALK-TIER
/// vacuous -- a walk that finishes inside one tick has not shown it can be interrupted.
int32_t g_walkTicks = 0;
int32_t g_walkMaxDepth = 0;
int32_t g_walkPagedReads = 0;
int32_t g_walkGiveUps = 0;

/// A machine that never finished is a failure, not a hang. The recursive walk over this fixture
/// makes 26 folder reads and each one is at most a few ticks on the directory channel.
constexpr int32_t kMaxWalkMachineTicks = 4000;

void runWalkMachineTick() {
	switch (g_walkStep) {
	case WalkMachineStep::ArmFound:
		g_walkSavedDir = Browser::currentDir;
		g_walkTierBefore = deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Browser);
		Browser::currentDir = kWalkStartDir;
		g_presetWalk.arm(loadInstrumentPresetUI, currentSong, OutputType::SYNTH, Availability::ANY);
		g_waitTicks = 0;
		g_walkStep = WalkMachineStep::RunFound;
		break;

	case WalkMachineStep::RunFound:
		if (g_presetWalk.tick()) {
			if (++g_waitTicks > kMaxWalkMachineTicks) {
				fail(legName(g_leg), "the preset search machine never finished");
				g_walkStep = WalkMachineStep::Report;
			}
			break;
		}
		{
			// Copied out NOW: the FileItem* points into Browser::fileItems, which emptyFileItems()
			// is about to drop.
			const auto found = g_presetWalk.result();
			g_walkFound = found.has_value() && *found != nullptr;
			g_walkFoundName = g_walkFound ? (*found)->filename : "";
		}
		g_walkTicks = g_waitTicks;
		g_walkMaxDepth = g_presetWalk.maxDepthReached();
		g_walkPagedReads = g_presetWalk.pagedReads();
		g_walkGiveUps = g_presetWalk.depthGiveUps();
		g_walkStep = WalkMachineStep::ArmExhaust;
		break;

	case WalkMachineStep::ArmExhaust:
		loadInstrumentPresetUI.emptyFileItems();
		// A real, empty filler folder -- NOT a missing one. The PL sibling's first draft searched a
		// folder that never existed and passed on FOLDER_DOESNT_EXIST (10), which tests a missing
		// folder rather than an exhausted search.
		Browser::currentDir = "SYNTHS/WALK/W01";
		g_presetWalk.arm(loadInstrumentPresetUI, currentSong, OutputType::SYNTH, Availability::ANY);
		g_waitTicks = 0;
		g_walkStep = WalkMachineStep::RunExhaust;
		break;

	case WalkMachineStep::RunExhaust:
		if (g_presetWalk.tick()) {
			if (++g_waitTicks > kMaxWalkMachineTicks) {
				fail(legName(g_leg), "the exhausting preset search machine never finished");
				g_walkStep = WalkMachineStep::Report;
			}
			break;
		}
		{
			const auto none = g_presetWalk.result();
			g_walkExhausted = !none.has_value() && none.error() == Error::NO_FURTHER_FILES_THIS_DIRECTION;
			g_walkExhaustError = none.has_value() ? Error::NONE : none.error();
		}
		g_walkStep = WalkMachineStep::Report;
		break;

	case WalkMachineStep::Report: {
		loadInstrumentPresetUI.emptyFileItems();
		Browser::currentDir = g_walkSavedDir;

		const bool descended = g_walkFound && g_walkFoundName == std::string(kNestedFilename);
		printf("PW-WALK-FOUND ok=%d name=%s\n", (int)g_walkFound, g_walkFound ? g_walkFoundName.c_str() : "-");
		// Measured directly here, unlike the PL sibling: the machine counts how deep its frame stack
		// got, so "it descended" is a fact rather than an inference from which preset came back.
		printf("PW-WALK-DEPTH ok=%d descended=%d depth=%d\n", (int)(descended && g_walkMaxDepth >= 2), (int)descended,
		       (int)g_walkMaxDepth);
		printf("PW-WALK-PAGED ok=%d reads=%d giveUps=%d\n", (int)(g_walkPagedReads > 0), (int)g_walkPagedReads,
		       (int)g_walkGiveUps);
		printf("PW-WALK-EXHAUST ok=%d error=%d\n", (int)g_walkExhausted, (int)g_walkExhaustError);

		const int32_t tierIo =
		    deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Browser) - g_walkTierBefore;
		// tierIo == 0 alone cannot tell "deferred properly" from "never suspended". The tick count is
		// what separates them: a search spanning ticks did no blocking card I/O BECAUSE it gave the
		// tier back between folders, which is the claim being made.
		printf("PW-WALK-TIER ok=%d tierIo=%d ticks=%d\n", (int)(tierIo == 0 && g_walkTicks > 0), (int)tierIo,
		       (int)g_walkTicks);

		if (!descended) {
			fail(legName(g_leg), "the preset search machine did not reach the nested preset");
		}
		if (g_walkMaxDepth < 2) {
			fail(legName(g_leg), "the preset search machine never pushed a frame");
		}
		if (g_walkPagedReads == 0) {
			fail(legName(g_leg), "the preset search machine never paged a folder");
		}
		if (g_walkGiveUps != 0) {
			fail(legName(g_leg), "the preset search machine hit its depth bound on a one-deep fixture");
		}
		if (!g_walkExhausted) {
			fail(legName(g_leg), "an exhausted search machine did not report NO_FURTHER_FILES_THIS_DIRECTION");
		}
		if (tierIo != 0) {
			fail(legName(g_leg), "the preset search machine did card I/O on the interaction tier");
		}
		if (g_walkTicks == 0) {
			fail(legName(g_leg), "the preset search machine never suspended, so its tier leg proves nothing");
		}
		nextLeg();
		break;
	}
	}
}
/// @}

/// The clip a blank song starts with: its first session clip. Null while a clear is between songs.
InstrumentClip* blankClip() {
	if (currentSong == nullptr || currentSong->sessionClips.empty()) {
		return nullptr;
	}
	return static_cast<InstrumentClip*>(currentSong->sessionClips[0]);
}

/// The Instrument that clip plays.
Instrument* blankInstrument() {
	InstrumentClip* clip = blankClip();
	return (clip != nullptr && clip->output != nullptr) ? static_cast<Instrument*>(clip->output) : nullptr;
}

/// Whether that Instrument is a synth with its first source bound to a file.
bool blankBound() {
	Instrument* instrument = blankInstrument();
	if (instrument == nullptr || instrument->type != OutputType::SYNTH) {
		return false;
	}
	AudioFileHolder* holder = firstHolder(static_cast<SoundInstrument*>(instrument)->sources[0]);
	return holder != nullptr && !holder->filePath.empty();
}

/// BS-BOOT: the boot song starts on the default synth and asks for a preset once -- finding none, SYNTHS being empty
/// until the first leg writes into it.
void checkBlankBoot() {
	Instrument* instrument = blankInstrument();
	const bool isDefault =
	    instrument != nullptr && instrument->type == OutputType::SYNTH && instrument->name == kDefaultSynthName;
	const int32_t walks = LoadInstrumentPresetUI::presetAcquisitionsRun();
	printf("BS-BOOT default=%d walks=%d outputs=%d ticks=%d\n", isDefault ? 1 : 0, walks, countOutputs(), g_waitTicks);
	if (!isDefault || walks != 1) {
		printf("BS-BOOT FAIL the boot song did not start on the default synth and ask for a preset exactly once\n");
		g_failures++;
	}
}

/// BS-CLEAR and BS-EDITED: what the cleared song's clip ended up holding, and at what cost.
void checkBlankClear() {
	Instrument* instrument = blankInstrument();
	const std::string name = (instrument != nullptr) ? instrument->name : "";
	const int32_t reads = deluge::gui::instrumentLoad.documentReads() - g_blankReadsBefore;
	const int32_t onOwner = deluge::gui::instrumentLoad.documentReadsOnOwner() - g_blankOwnerReadsBefore;
	if (g_leg == Leg::BlankClear) {
		printf("BS-CLEAR name=%s bound=%d reads=%d onOwner=%d outputs=%d ticks=%d\n", name.c_str(),
		       blankBound() ? 1 : 0, reads, onOwner, countOutputs(), g_waitTicks);
		if (name.empty() || name == kDefaultSynthName || !blankBound() || reads < 1 || onOwner != 0) {
			printf("BS-CLEAR FAIL a cleared song did not land on a SYNTHS preset staged by InstrumentLoad off the "
			       "storage owner\n");
			g_failures++;
		}
		return;
	}
	const int32_t orphans = orphanedParamManagers();
	const int32_t outputs = countOutputs();
	printf("BS-EDITED name=%s edited=%d orphans=%d outputs=%d ticks=%d\n", name.c_str(), g_blankEdited ? 1 : 0, orphans,
	       outputs, g_waitTicks);
	if (name != kDefaultSynthName || !g_blankEdited || orphans != 0 || outputs != 1) {
		printf("BS-EDITED FAIL a preset was swapped over an edited default synth, or something of it was left "
		       "behind\n");
		g_failures++;
	}
}

/// The blank-song legs: BS-BOOT waits for what boot asked for, and the two clear legs clear the song and wait for what
/// the new one asks for.
void runBlankTick() {
	switch (g_blankStep) {
	case BlankStep::Prepare:
		if (g_leg == Leg::BlankBoot) {
			g_waitTicks = 0;
			g_blankStep = BlankStep::Wait;
			break;
		}
		// The stall legs leave the loader holding, and a clear needs it moving again with nothing of theirs in flight.
		deluge_audio_load_set_latency_ms(0);
		if (deluge::gui::instrumentLoad.inFlight()) {
			if (++g_waitTicks > kMaxStallTicks) {
				fail(legName(g_leg), "a load from an earlier leg never ended");
				nextLeg();
			}
			break;
		}
		g_waitTicks = 0;
		g_blankStep = BlankStep::Act;
		break;
	case BlankStep::Act:
		g_blankRunsBefore = LoadInstrumentPresetUI::presetAcquisitionsRun();
		g_blankReadsBefore = deluge::gui::instrumentLoad.documentReads();
		g_blankOwnerReadsBefore = deluge::gui::instrumentLoad.documentReadsOnOwner();
		g_blankEdited = false;
		if (g_leg == Leg::BlankEdited) {
			// The preset's sample is held, so the default synth can be edited while the song's request is still out.
			deluge_audio_load_set_latency_ms(kHoldMs);
		}
		(void)deluge::gui::context_menu::clearSong.acceptCurrentOption();
		printf("PL-PROBE leg=%s cleared=1\n", legName(g_leg));
		g_waitTicks = 0;
		g_blankStep = BlankStep::Wait;
		break;
	case BlankStep::Wait: {
		// The clear runs as one operation behind the gate it closes: the gate reopening is the new song arriving.
		const bool cleared = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		if (g_leg == Leg::BlankEdited && !g_blankEdited && cleared) {
			// The user edits the new song's default synth while the preset it asked for is still being read.
			Instrument* instrument = blankInstrument();
			if (instrument != nullptr) {
				instrument->editedByUser = true;
				g_blankEdited = true;
				deluge_audio_load_set_latency_ms(0);
			}
		}
		const bool acquired = LoadInstrumentPresetUI::presetAcquisitionsRun() > g_blankRunsBefore;
		const bool settled = acquired || g_waitTicks > kBlankSettleTicks;
		const bool ready = (g_leg == Leg::BlankBoot) || cleared;
		const bool ended = ready && settled && !deluge::gui::instrumentLoad.inFlight();
		if (ended || ++g_waitTicks > kMaxLegTicks) {
			if (g_leg == Leg::BlankBoot) {
				checkBlankBoot();
			}
			else {
				checkBlankClear();
			}
			nextLeg();
		}
		break;
	}
	}
}

void nextLeg() {
	g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
	g_walkStep = WalkMachineStep::ArmFound;
	g_step = Step::Prepare;
	g_aqStep = AqStep::Prepare;
	g_blankStep = BlankStep::Prepare;
	g_waitTicks = 0;
}

void runTick() {
	if (g_leg == Leg::Done) {
		printf("PL-RESULT failures=%d\n", g_failures);
		fflush(stdout);
		g_summaryPrinted = true;
		return;
	}
	if (isAcquireLeg(g_leg)) {
		runAcquireTick();
		return;
	}
	if (isBlankLeg(g_leg)) {
		runBlankTick();
		return;
	}
	if (g_leg == Leg::WalkMachine) {
		runWalkMachineTick();
		return;
	}
	switch (g_step) {
	case Step::Prepare:
		prepareLeg();
		g_step = Step::Open;
		break;
	case Step::Open:
		g_waitTicks = 0;
		g_step = openLeg() ? Step::Listing : Step::Prepare;
		if (g_step == Step::Prepare) {
			nextLeg();
		}
		break;
	case Step::Listing:
	case Step::SelectWait:
		if (g_leg == Leg::Supersede && static_cast<Instrument*>(g_kitClip->output)->name == kHeavyName) {
			g_heavySwapped = true;
		}
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
		printf("PL-SELECT leg=%s file=%s/%s\n", legName(g_leg), Browser::currentDir.c_str(),
		       loadInstrumentPresetUI.getCurrentFileItem() != nullptr
		           ? loadInstrumentPresetUI.getCurrentFileItem()->filename.c_str()
		           : "");
		g_waitTicks = 0;
		g_step = actsWithoutSettling() ? Step::Act : Step::Settle;
		break;
	case Step::Settle:
		// The load the selection started, left to finish as a user waits for it before pressing anything: a
		// scroll-load still queued on the owner would otherwise land after the leg's own load.
		if (g_leg == Leg::Supersede ? markTier().sampleLoads == g_heavyLoadsBefore
		                            : loadInstrumentPresetUI.scrollLoadInFlight()) {
			if (++g_waitTicks > kMaxLegTicks) {
				fail(legName(g_leg), "the selection's scroll-load never finished");
				nextLeg();
			}
			break;
		}
		printf("PL-PROBE leg=%s settled ticks=%d\n", legName(g_leg), g_waitTicks);
		g_waitTicks = 0;
		g_step = Step::Act;
		break;
	case Step::Select:
		// The held legs hold the loader from the selection on, so the scroll-load it starts is held too.
		if (g_leg == Leg::Supersede || g_leg == Leg::Gate) {
			deluge_audio_load_set_latency_ms(kHoldMs);
		}
		else if (g_leg == Leg::Stall || g_leg == Leg::RevertStall) {
			deluge_audio_load_set_latency_ms(kStallHoldMs);
		}
		g_heavySwapped = false;
		g_orphansBefore = orphanedParamManagers();
		g_heavyLoadsBefore = markTier().sampleLoads;
		g_gateTried = false;
		g_gateWhileRunning = false;
		g_gateTypeRefused = false;
		g_gateEncoderRefused = false;
		if (g_leg == Leg::MidiDef) {
			(void)loadMidiDeviceDefinitionUI.setFileByFullPath(OutputType::NONE, legPreset());
		}
		else {
			(void)loadInstrumentPresetUI.setFileByFullPath(OutputType::NONE, legPreset());
		}
		g_waitTicks = 0;
		g_step = Step::SelectWait;
		break;
	case Step::Act:
		actLeg();
		g_waitTicks = 0;
		g_step = Step::Wait;
		break;
	case Step::Wait:
		if (g_leg == Leg::Supersede) {
			listingBusy();
			if (static_cast<Instrument*>(g_kitClip->output)->name == kHeavyName) {
				g_heavySwapped = true;
			}
		}
		if (g_leg == Leg::Gate) {
			tickGate();
		}
		if (isRevertLeg(g_leg)) {
			tickRevert();
		}
		if (legEnded()
		    || ++g_waitTicks > ((g_leg == Leg::Stall || g_leg == Leg::RevertStall) ? kMaxStallTicks : kMaxLegTicks)) {
			checkLeg();
			nextLeg();
		}
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_preset_load_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_leg = Leg::BlankBoot;
	g_step = Step::Prepare;
	g_blankStep = BlankStep::Prepare;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_waitTicks = 0;
	g_synth = nullptr;
	g_synthClip = nullptr;
	g_kitClip = nullptr;
	g_tierMark = {};
	g_aqStep = AqStep::Prepare;
	g_aqClip = nullptr;
}

bool deluge_scenario_preset_load_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_preset_load_dispatch_step(void) {
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

bool deluge_scenario_preset_load_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_preset_load_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
