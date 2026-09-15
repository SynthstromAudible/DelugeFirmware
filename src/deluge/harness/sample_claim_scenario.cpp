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
#include "gui/context_menu/sample_browser/synth.h"
#include "gui/menu_item/file_selector.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/slicer.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "hid/button.h"
#include "hid/led/pad_leds.h"
#include "libdeluge/audio_load.h" // deluge_audio_load_set_latency_ms
#include "libdeluge/control_surface.h"
#include "memory/fast_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/audio/audio_file.h"
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
	Stall,
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
/// Write a one-drum kit preset to @p path through the firmware's own serialiser.
bool writeKitPreset(const char* path) {
	ParamManagerForTimeline kitParams;
	Instrument* kitBase = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	if (kitBase == nullptr) {
		return fail("kit", "no kit for the preset");
	}
	auto* kit = static_cast<Kit*>(kitBase);
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(kit->toModControllable()), nullptr, &kitParams,
	                                true);
	if (addSampleDrum(*kit, kKitDrumName, kKitPresetSample) == nullptr) {
		return false;
	}
	Error error = StorageManager::createXMLFile(path, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return fail("kit", "the kit preset file could not be created");
	}
	kit->writeToFile(nullptr, currentSong);
	error = GetSerializer().closeFileAfterWriting(path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", "\n</kit>\n");
	return error == Error::NONE || fail("kit", "the kit preset file could not be closed");
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
	const bool opened = setup && openUI(&sampleBrowser);
	printf("SC-SETUP leg=%s setup=%d opened=%d browserOnTop=%d\n", leg, setup ? 1 : 0, opened ? 1 : 0,
	       getCurrentUI() == &sampleBrowser ? 1 : 0);
	return opened || fail(leg, "the sample browser could not be opened");
}

SoundDrum* kitDrum() {
	if (g_kitClip == nullptr || g_kitClip->output == nullptr || g_kitClip->output->type != OutputType::KIT) {
		return nullptr;
	}
	Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum;
	return (drum != nullptr && drum->type == DrumType::SOUND) ? static_cast<SoundDrum*>(drum) : nullptr;
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
			if (writeKitPreset(kKitPresetPath)) {
				// The new-kit-track gesture, which takes the first kit preset nobody in the song is using:
				// kKitPresetPath.
				g_kitClip = sessionView.reproCreateNewInstrumentClip(OutputType::KIT, 1);
			}
			printf("SC-PROBE leg=kit prepared kitClip=%d\n", g_kitClip != nullptr ? 1 : 0);
		});
		break;
	case Leg::AudioClip:
		onOwner([] { g_audioClip = sessionView.createNewClip(OutputType::AUDIO, 2); });
		break;
	case Leg::SliceManual:
		onOwner([] {
			// A kit of its own: the region slice left the first with four drums.
			if (writeKitPreset(kManualKitPresetPath)) {
				g_kitClip = sessionView.reproCreateNewInstrumentClip(OutputType::KIT, 3);
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
	case Leg::AudioClip:
		return kClipSample;
	case Leg::Slice:
	case Leg::SliceManual:
		return kSliceSample;
	case Leg::Done:
		break;
	}
	return "";
}

/// The claim's Select press, or for the slice leg the slicer opened on the selected file and its commit.
/// Whether the current leg is one of the two supersede legs.
bool isSupersede() {
	return g_leg == Leg::Supersede || g_leg == Leg::SupersedeLoading;
}

/// The file a supersede leg's cursor moves on to.
const char* supersedeSecond() {
	return (g_leg == Leg::Supersede) ? kSupersedeLanded : kSupersedeLoading;
}

void actLeg() {
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

void nextLeg() {
	g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
	g_step = Step::Prepare;
	g_waitTicks = 0;
}

void runTick() {
	if (g_leg == Leg::Done) {
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
			g_step = Step::Listing;
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
		if ((g_leg == Leg::Slice || g_leg == Leg::SliceManual) && getCurrentUI() == &slicer) {
			// The slice ends on the slicer's graphics routine: ticked here, as the preview wait ticks the browser's.
			slicer.graphicsRoutine();
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
