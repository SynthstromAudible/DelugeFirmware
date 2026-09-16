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
#include "gui/context_menu/load_instrument_preset.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "hid/button.h"
#include "libdeluge/audio_load.h" // deluge_audio_load_set_latency_ms
#include "memory/fast_allocator.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
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
#include "storage/multi_range/multi_range.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"

#include <cstdio>
#include <iterator>
#include <string>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases printf to an implementation this harness has no
// `putchar_` for -- every line below would become an undefined symbol at link time.
#ifdef printf
#undef printf
#endif

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

enum class Leg : uint8_t { Synth, Kit, Row, Clone, Race, Supersede, Gate, Stall, Done };
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
	case Leg::Stall:
		return "stall";
	case Leg::Done:
		return "done";
	}
	return "?";
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
	case Leg::Done:
		break;
	}
	return "";
}

// ── The legs ─────────────────────────────────────────────────────────────────

void prepareLeg() {
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
		     && writeSampleSynthPreset(kRacePreset, kRaceSample) && writeSampleSynthPreset(kGatePreset, kGateSample)
		     && writeSampleSynthPreset(kStallPreset, kStallSample)
		     && writeKitPreset(kHeavyPreset, kHeavyDrumNames, kHeavySamples, 3)
		     && writeKitPreset(kLitePreset, &liteName, &kLiteSample, 1);
		if (ok) {
			// The new-kit-track gesture, which takes the first kit preset nobody in the song is using: PLBASE.
			g_kitClip = sessionView.reproCreateNewInstrumentClip(OutputType::KIT, 1);
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

void checkLeg() {
	checkTier();
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
	case Leg::Done:
		break;
	}
}

void nextLeg() {
	g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
	g_step = Step::Prepare;
	g_waitTicks = 0;
}

void runTick() {
	if (g_leg == Leg::Done) {
		printf("PL-RESULT failures=%d\n", g_failures);
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
		else if (g_leg == Leg::Stall) {
			deluge_audio_load_set_latency_ms(kStallHoldMs);
		}
		g_heavySwapped = false;
		g_orphansBefore = orphanedParamManagers();
		g_heavyLoadsBefore = markTier().sampleLoads;
		g_gateTried = false;
		g_gateWhileRunning = false;
		g_gateTypeRefused = false;
		g_gateEncoderRefused = false;
		(void)loadInstrumentPresetUI.setFileByFullPath(OutputType::NONE, legPreset());
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
		if (legEnded() || ++g_waitTicks > (g_leg == Leg::Stall ? kMaxStallTicks : kMaxLegTicks)) {
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
	g_leg = Leg::Synth;
	g_step = Step::Prepare;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_waitTicks = 0;
	g_synth = nullptr;
	g_synthClip = nullptr;
	g_kitClip = nullptr;
	g_tierMark = {};
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
