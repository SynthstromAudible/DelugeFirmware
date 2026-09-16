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
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "hid/button.h"
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

/// Scenario ticks a browser listing may take before it is called wedged.
constexpr int32_t kMaxListingTicks = 20000;
/// Scenario ticks a load may take before it is called wedged.
constexpr int32_t kMaxLegTicks = 2000;

enum class Leg : uint8_t { Synth, Kit, Row, Clone, Done };
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
	return {loadInstrumentPresetUI.documentReads(), loadInstrumentPresetUI.documentReadsOnOwner(),
	        loadInstrumentPresetUI.sampleLoads(), loadInstrumentPresetUI.sampleLoadsOnOwner(),
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
	Clip* clip = (g_leg == Leg::Kit || g_leg == Leg::Row) ? g_kitClip : g_synthClip;
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
		ok = ok && writeKitPreset(kBaseKitPreset, &baseName, &kBaseKitSample, 1);
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

void actLeg() {
	using namespace deluge::hid::button;
	if (g_leg == Leg::Clone) {
		// A long press on Select: the press, then what the UI timer's callback does once it has been held.
		(void)getCurrentUI()->buttonAction(SELECT_ENC, true, false);
		(void)static_cast<UI&>(loadInstrumentPresetUI).timerCallback();
		const bool menuOnTop = getCurrentUI() == &deluge::gui::context_menu::loadInstrumentPreset;
		printf("PL-SETUP leg=clone menuOnTop=%d\n", menuOnTop ? 1 : 0);
		if (menuOnTop) {
			pressSelect();
		}
		return;
	}
	pressSelect();
}

bool legEnded() {
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
		g_step = Step::Settle;
		break;
	case Step::Settle:
		// The load the selection started, left to finish as a user waits for it before pressing anything: a
		// scroll-load still queued on the owner would otherwise land after the leg's own load.
		if (loadInstrumentPresetUI.scrollLoadInFlight()) {
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
		if (legEnded() || ++g_waitTicks > kMaxLegTicks) {
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
