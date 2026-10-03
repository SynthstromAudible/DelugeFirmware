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

#include "scenarios/load_cancel_scenario.h"

#include "definitions_cxx.hpp"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "libdeluge/audio_load.h" // deluge_audio_load_set_latency_ms
#include "libdeluge/file_bytes.h" // deluge_file_bytes_set_latency_ms
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/output.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "scenarios/preset_fixture_loader.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "util/functions.h" // getInstrumentFolder

#include <cstdint>
#include <cstdio>
#include <string>

// See staging_release_scenario.cpp: the app's printf alias has no provider in this harness.
#ifdef printf
#undef printf
#endif

namespace {

using deluge::gui::CancelResult;
using deluge::gui::instrumentLoad;
using deluge::gui::LoadTicket;

/// The presets, written through the firmware's serialisers once the boot song has its synth; each names a sample of
/// its own (see fill_load_cancel_card in the harness). Each leg that holds a sample load holds a sample nothing has
/// asked for before, so no earlier leg's request for it is still out when the hold is set.
constexpr int32_t kPresets = 6;
constexpr const char* kPresetPaths[kPresets] = {"SYNTHS/LCA.XML", "SYNTHS/LCB.XML", "SYNTHS/LCC.XML",
                                                "SYNTHS/LCD.XML", "SYNTHS/LCE.XML", "SYNTHS/LCF.XML"};
constexpr const char* kSamplePaths[kPresets] = {"SAMPLES/LC/LCA.WAV", "SAMPLES/LC/LCB.WAV", "SAMPLES/LC/LCC.WAV",
                                                "SAMPLES/LC/LCD.WAV", "SAMPLES/LC/LCE.WAV", "SAMPLES/LC/LCF.WAV"};
constexpr const char* kPresetA = kPresetPaths[0];
constexpr const char* kPresetB = kPresetPaths[1];
constexpr const char* kPresetC = kPresetPaths[2];
constexpr const char* kPresetD = kPresetPaths[3];
constexpr const char* kPresetE = kPresetPaths[4];
constexpr const char* kPresetF = kPresetPaths[5];
constexpr const char* kNameA = "LCA";
constexpr const char* kNameB = "LCB";
constexpr const char* kNameC = "LCC";
constexpr const char* kNameD = "LCD";
constexpr const char* kNameE = "LCE";
constexpr const char* kNameF = "LCF";
constexpr const char* kSampleA = kSamplePaths[0];
constexpr const char* kSampleB = kSamplePaths[1];
constexpr const char* kSampleC = kSamplePaths[2];

/// How long the byte channel holds the cancel-at-Stage leg's document read, and the loader each held sample load.
constexpr uint32_t kHoldMs = 300;
/// Steps a load, a listing or a settle is given. A step is about a virtual millisecond.
constexpr int32_t kMaxSteps = 20000;
/// Steps a leg runs on after its cancel, past every hold, to see that nothing of the dropped load comes back.
constexpr int32_t kAfterSteps = 600;

enum class Stage : uint8_t {
	Loading,       ///< The boot song's preset request settles.
	Fixture,       ///< The presets are written, and the browser opened on the synth clip.
	Opening,       ///< ...its listing finishes.
	StageSelect,   ///< cancel_at_stage: the byte channel held, LCA selected.
	StageWait,     ///< ...its scroll-load is staging: BACK.
	StageAfter,    ///< ...nothing of it comes back.
	SamplesSelect, ///< cancel_at_samples: the loader held, LCB selected.
	SamplesWait,   ///< ...its scroll-load is loading samples: BACK.
	SamplesAfter,  ///< ...nothing of it comes back, nothing leaks and no error shows.
	QueuedSelect,  ///< cancel_queued: the loader held, LCC selected.
	QueuedFirst,   ///< ...its samples are loading: LCD selected.
	QueuedSecond,  ///< ...LCD's load queued behind LCC's: BACK.
	QueuedAfter,   ///< ...neither comes back.
	StaleSelect,   ///< stale_ticket: LCA selected and left to load.
	StaleWait,     ///< ...it finishes: its ticket is Stale.
	NewerSelect,   ///< ...the loader held, LCE selected.
	NewerFirst,    ///< ...its samples are loading: LCF selected.
	NewerSecond,   ///< ...LCF's load replaces LCE's: LCE's ticket is Stale.
	NewerSettle,   ///< ...LCF lands.
	RefusedStart,  ///< refused_for_a_creation: the browser closed, a new synth asked for and its cancel refused.
	RefusedSettle, ///< ...the creation lands.
	Report,
	Done,
};

Stage g_stage = Stage::Loading;
bool g_stepDone = false;
int32_t g_failures = 0;
int32_t g_steps = 0;

InstrumentClip* g_clip = nullptr;
Output* g_outputBefore = nullptr;
std::string g_nameBefore;
int32_t g_outputsBefore = 0;
int32_t g_orphansBefore = 0;
int32_t g_readsBefore = 0;
int32_t g_loadsBefore = 0;
int32_t g_cancelsBefore = 0;
int32_t g_giveUpsBefore = 0;
/// InstrumentLoad's document reads and sample loads just after BACK: a dropped load that ran on would add to them.
int32_t g_readsAtCancel = 0;
int32_t g_loadsAtCancel = 0;
LoadTicket g_ticket{};
LoadTicket g_newerTicket{};
bool g_popupSeen = false;
/// stale_ticket: whether LCE's ticket was cancelled while LCF's load was queued behind it.
bool g_queuedStaleSeen = false;

void fail(const char* marker, const char* what) {
	printf("%s FAIL %s\n", marker, what);
	g_failures++;
}

/// Gives a wait another step; false, with @p marker's failure reported and the run ended, once it has had too many.
bool waitMore(const char* marker, const char* what) {
	if (++g_steps <= kMaxSteps) {
		return true;
	}
	fail(marker, what);
	g_stage = Stage::Report;
	return false;
}

void pressBack() {
	(void)getCurrentUI()->buttonAction(deluge::hid::button::BACK, true);
	(void)getCurrentUI()->buttonAction(deluge::hid::button::BACK, false);
}

/// Ticks the browser while its listing runs, and says whether it still is.
bool listingBusy() {
	if (Browser::isListingInProgress()) {
		getCurrentUI()->graphicsRoutine();
	}
	return Browser::isListingInProgress();
}

int32_t countOutputs() {
	int32_t outputs = 0;
	for (Output* output = currentSong->project.media.first_output; output != nullptr; output = output->next) {
		outputs++;
	}
	return outputs;
}

/// Every instrument in the song, active or hibernating, named @p name.
int32_t instrumentsNamed(const char* name) {
	int32_t found = 0;
	for (Output* list : {currentSong->project.media.first_output,
	                     static_cast<Output*>(currentSong->project.media.first_hibernating_instrument)}) {
		for (Output* output = list; output != nullptr; output = output->next) {
			found += (output->name == name) ? 1 : 0;
		}
	}
	return found;
}

/// Whether @p owner is the song's: one of its outputs, active or hibernating, or a drum of one of its kits.
bool ownedBySong(const ModControllableAudio* owner) {
	for (Output* list : {currentSong->project.media.first_output,
	                     static_cast<Output*>(currentSong->project.media.first_hibernating_instrument)}) {
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
	for (const auto& [key, paramManager] : currentSong->live.param_backup.map) {
		orphans += ownedBySong(key.first) ? 0 : 1;
	}
	return orphans;
}

/// How many holders keep the sample at @p path loaded, or -1 when it is not resident.
int64_t leasesOn(const char* path) {
	for (AudioFile* file : audioFileManager.audioFiles) {
		if (file != nullptr && file->filePath == path) {
			return file->leaseCount();
		}
	}
	return -1;
}

bool loadingAnimationShown() {
	return deluge::hid::display::OLED::isWorkingAnimationPresent();
}

std::string currentFileName() {
	const FileItem* item = loadInstrumentPresetUI.getCurrentFileItem();
	return item != nullptr ? std::string{item->filename.c_str()} : std::string{};
}

/// What a leg compares against: the clip's preset, the song's outputs and orphans, and InstrumentLoad's counts.
void mark() {
	g_outputBefore = g_clip->output;
	g_nameBefore = g_clip->output->name;
	g_outputsBefore = countOutputs();
	g_orphansBefore = orphanedParamManagers();
	g_readsBefore = instrumentLoad.documentReads();
	g_loadsBefore = instrumentLoad.sampleLoads();
	g_cancelsBefore = instrumentLoad.cancellations();
	g_giveUpsBefore = instrumentLoad.giveUps();
	g_ticket = loadInstrumentPresetUI.scrollLoadTicket();
	g_popupSeen = false;
	g_steps = 0;
}

/// Select @p path in the browser, as the user's scroll onto it does: the listing lands, then a scroll-load is asked
/// for.
void select(const char* path) {
	(void)loadInstrumentPresetUI.setFileByFullPath(OutputType::NONE, path);
}

/// Whether the browser asked for a scroll-load since mark(), its listing over.
bool scrollLoadAsked() {
	return !listingBusy() && loadInstrumentPresetUI.scrollLoadTicket().generation != g_ticket.generation;
}

/// Whether the browser's latest scroll-load is the load in flight, with nothing queued behind it to replace it.
bool scrollLoadCurrent() {
	return scrollLoadAsked() && instrumentLoad.inFlight()
	       && instrumentLoad.currentTicket().generation == loadInstrumentPresetUI.scrollLoadTicket().generation;
}

/// The clip still on the preset it had at mark(), with nothing of @p name in the song and no outputs added.
bool previousKept(const char* name) {
	return g_clip->output == g_outputBefore && g_clip->output->name == g_nameBefore && instrumentsNamed(name) == 0
	       && countOutputs() == g_outputsBefore;
}

/// BACK while the browser's scroll-load is out: the load must be dropped at once, with the browser left where it is.
void backAndCheck(const char* marker, const char* name, const char* file) {
	const bool wasCurrent = scrollLoadCurrent();
	pressBack();
	g_readsAtCancel = instrumentLoad.documentReads();
	g_loadsAtCancel = instrumentLoad.sampleLoads();
	const int32_t cancels = instrumentLoad.cancellations() - g_cancelsBefore;
	const bool browserStayed =
	    getCurrentUI() == &loadInstrumentPresetUI && currentFileName() == file && !Browser::isListingInProgress();
	const bool dropped = !instrumentLoad.inFlight();
	const bool animation = loadingAnimationShown();
	g_popupSeen = display->hasPopup();
	printf("%s-BACK wasCurrent=%d cancels=%d dropped=%d browserStayed=%d file=%s kept=%d animation=%d popup=%d "
	       "steps=%d\n",
	       marker, wasCurrent ? 1 : 0, cancels, dropped ? 1 : 0, browserStayed ? 1 : 0, currentFileName().c_str(),
	       previousKept(name) ? 1 : 0, animation ? 1 : 0, g_popupSeen ? 1 : 0, g_steps);
	if (!wasCurrent) {
		fail(marker, "the scroll-load was not the load in flight when BACK was pressed, so nothing was tested");
	}
	if (cancels != 1 || !dropped) {
		fail(marker, "BACK did not cancel the browser's scroll-load");
	}
	if (!browserStayed) {
		fail(marker, "BACK during a scroll-load moved or closed the browser instead of only cancelling the load");
	}
	if (animation) {
		fail(marker, "the loading animation was left up after the cancel");
	}
	g_steps = 0;
}

/// After the cancel, past every hold: the clip on its preset, nothing of the dropped load in the song, InstrumentLoad
/// idle with no reads or sample loads since the cancel, and no error shown.
void checkAfter(const char* marker, const char* name, const char* sample) {
	const int32_t reads = instrumentLoad.documentReads() - g_readsAtCancel;
	const int32_t loads = instrumentLoad.sampleLoads() - g_loadsAtCancel;
	const int32_t giveUps = instrumentLoad.giveUps() - g_giveUpsBefore;
	const int32_t orphans = orphanedParamManagers() - g_orphansBefore;
	const int64_t leases = leasesOn(sample);
	const bool kept = previousKept(name);
	printf("%s kept=%d named=%s instances=%d outputsKept=%d orphans=%d reads=%d sampleLoads=%d giveUps=%d leases=%lld "
	       "inFlight=%d popup=%d\n",
	       marker, kept ? 1 : 0, g_clip->output->name.c_str(), instrumentsNamed(name),
	       countOutputs() == g_outputsBefore ? 1 : 0, orphans, reads, loads, giveUps, static_cast<long long>(leases),
	       instrumentLoad.inFlight() ? 1 : 0, g_popupSeen ? 1 : 0);
	if (!kept) {
		fail(marker, "the cancelled preset was swapped in or left in the song, or the clip lost its preset");
	}
	if (orphans != 0 || leases > 0) {
		fail(marker, "the cancelled load left a param manager or a sample lease behind");
	}
	if (reads != 0 || loads != 0) {
		fail(marker, "the cancelled load ran on after its cancel");
	}
	if (giveUps != 0 || g_popupSeen) {
		fail(marker, "the cancelled load reported a failure or showed an error");
	}
	if (instrumentLoad.inFlight()) {
		fail(marker, "a load was still in flight after the cancel");
	}
}

void stepLoading() {
	if (currentSong->blankPresetPending || instrumentLoad.inFlight()) {
		(void)waitMore("LC-SETUP", "the boot song's preset request never settled");
		return;
	}
	g_stage = Stage::Fixture;
}

void stepFixture() {
	g_clip = currentSong->project.clips.session_clips.empty()
	             ? nullptr
	             : static_cast<InstrumentClip*>(currentSong->project.clips.session_clips[0]);
	bool written = true;
	for (int32_t i = 0; written && i < kPresets; i++) {
		written = deluge::harness::writeSampleSynthPreset(kPresetPaths[i], kSamplePaths[i]);
	}
	if (!written || g_clip == nullptr || g_clip->output == nullptr || g_clip->output->type != OutputType::SYNTH) {
		fail("LC-SETUP", "the presets could not be written, or the song has no synth clip");
		g_stage = Stage::Report;
		return;
	}
	currentSong->setCurrentClip(g_clip);
	changeRootUI(&instrumentClipView);
	currentUIMode = UI_MODE_NONE;
	auto* instrument = static_cast<Instrument*>(g_clip->output);
	loadInstrumentPresetUI.setupLoadInstrument(instrument->type, instrument, g_clip);
	const bool opened = openUI(&loadInstrumentPresetUI);
	printf("LC-SETUP opened=%d synth=%s\n", opened ? 1 : 0, instrument->name.c_str());
	if (!opened) {
		fail("LC-SETUP", "the preset browser could not be opened");
		g_stage = Stage::Report;
		return;
	}
	g_steps = 0;
	g_stage = Stage::Opening;
}

void stepOpening() {
	// Opening lists the folder and asks for a scroll-load of whatever it lands on; that settles first.
	if (listingBusy() || instrumentLoad.inFlight()) {
		(void)waitMore("LC-SETUP", "the browser's opening listing never settled");
		return;
	}
	g_stage = Stage::StageSelect;
}

/// cancel_at_stage: with the byte channel holding every operation, the scroll-load is still staging its document.
void stepStageSelect() {
	mark();
	deluge_file_bytes_set_latency_ms(kHoldMs);
	select(kPresetA);
	g_stage = Stage::StageWait;
}

void stepStageWait() {
	const bool staging = scrollLoadCurrent() && instrumentLoad.documentReads() > g_readsBefore
	                     && instrumentLoad.sampleLoads() == g_loadsBefore;
	if (!staging) {
		(void)waitMore("LC-STAGE", "the scroll-load never began staging");
		return;
	}
	backAndCheck("LC-STAGE", kNameA, "LCA.XML");
	deluge_file_bytes_set_latency_ms(0);
	g_stage = Stage::StageAfter;
}

void stepStageAfter() {
	g_popupSeen = g_popupSeen || display->hasPopup();
	if (++g_steps < kAfterSteps) {
		return;
	}
	checkAfter("LC-STAGE", kNameA, kSampleA);
	g_stage = Stage::SamplesSelect;
}

/// cancel_at_samples: with the loader holding every sample load, the scroll-load has parsed its synth and is loading
/// its sample.
void stepSamplesSelect() {
	mark();
	deluge_audio_load_set_latency_ms(kHoldMs);
	select(kPresetB);
	g_stage = Stage::SamplesWait;
}

void stepSamplesWait() {
	const bool loading = scrollLoadCurrent() && instrumentLoad.sampleLoads() > g_loadsBefore;
	if (!loading) {
		(void)waitMore("LC-SAMPLES", "the scroll-load never began loading its samples");
		return;
	}
	backAndCheck("LC-SAMPLES", kNameB, "LCB.XML");
	deluge_audio_load_set_latency_ms(0);
	g_stage = Stage::SamplesAfter;
}

void stepSamplesAfter() {
	g_popupSeen = g_popupSeen || display->hasPopup();
	if (++g_steps < kAfterSteps) {
		return;
	}
	checkAfter("LC-SAMPLES", kNameB, kSampleB);
	g_stage = Stage::QueuedSelect;
}

/// cancel_queued: BACK straight after a fast scroll, with the newest scroll-load queued behind the one it is to
/// replace.
void stepQueuedSelect() {
	mark();
	deluge_audio_load_set_latency_ms(kHoldMs);
	select(kPresetC);
	g_stage = Stage::QueuedFirst;
}

void stepQueuedFirst() {
	if (!scrollLoadCurrent() || instrumentLoad.sampleLoads() == g_loadsBefore) {
		(void)waitMore("LC-QUEUED", "the scroll-load of LCC never began loading its samples");
		return;
	}
	g_ticket = loadInstrumentPresetUI.scrollLoadTicket();
	select(kPresetD);
	g_stage = Stage::QueuedSecond;
}

void stepQueuedSecond() {
	if (!scrollLoadAsked()) {
		(void)waitMore("LC-QUEUED", "the scroll onto LCD never asked for its load");
		return;
	}
	const bool queued =
	    instrumentLoad.inFlight()
	    && instrumentLoad.currentTicket().generation != loadInstrumentPresetUI.scrollLoadTicket().generation;
	pressBack();
	g_readsAtCancel = instrumentLoad.documentReads();
	g_loadsAtCancel = instrumentLoad.sampleLoads();
	const bool dropped = !instrumentLoad.inFlight();
	const int32_t cancels = instrumentLoad.cancellations() - g_cancelsBefore;
	const bool browserStayed = getCurrentUI() == &loadInstrumentPresetUI && currentFileName() == "LCD.XML";
	printf("LC-QUEUED-BACK queued=%d cancels=%d dropped=%d browserStayed=%d\n", queued ? 1 : 0, cancels,
	       dropped ? 1 : 0, browserStayed ? 1 : 0);
	if (!queued) {
		fail("LC-QUEUED", "LCD's load was not queued behind LCC's when BACK was pressed, so nothing was tested");
	}
	if (cancels != 1 || !dropped || !browserStayed) {
		fail("LC-QUEUED", "BACK after a fast scroll left a scroll-load loading, or moved the browser");
	}
	deluge_audio_load_set_latency_ms(0);
	g_popupSeen = display->hasPopup();
	g_steps = 0;
	g_stage = Stage::QueuedAfter;
}

void stepQueuedAfter() {
	g_popupSeen = g_popupSeen || display->hasPopup();
	if (++g_steps < kAfterSteps) {
		return;
	}
	checkAfter("LC-QUEUED", kNameC, kSampleC);
	if (instrumentsNamed(kNameD) != 0) {
		fail("LC-QUEUED", "the queued preset was loaded into the song after BACK");
	}
	g_stage = Stage::StaleSelect;
}

/// stale_ticket, first half: a load that has finished.
void stepStaleSelect() {
	mark();
	select(kPresetA);
	g_stage = Stage::StaleWait;
}

void stepStaleWait() {
	if (!scrollLoadAsked() || instrumentLoad.inFlight()) {
		(void)waitMore("LC-STALE", "the scroll-load of LCA never finished");
		return;
	}
	const LoadTicket finished = loadInstrumentPresetUI.scrollLoadTicket();
	Output* landed = g_clip->output;
	const CancelResult result = instrumentLoad.cancel(finished);
	const bool unchanged = g_clip->output == landed && landed->name == kNameA && !instrumentLoad.inFlight()
	                       && instrumentLoad.cancellations() == g_cancelsBefore;
	printf("LC-STALE-FINISHED result=%d landed=%s unchanged=%d\n", static_cast<int>(result), landed->name.c_str(),
	       unchanged ? 1 : 0);
	if (landed->name != kNameA) {
		fail("LC-STALE", "the scroll-load of LCA did not land, so nothing was tested");
	}
	if (result != CancelResult::Stale || !unchanged) {
		fail("LC-STALE", "a cancel naming a load that had finished was not Stale, or changed something");
	}
	g_stage = Stage::NewerSelect;
}

/// stale_ticket, second half: a load a newer request replaced, cancelled while the newer one is in flight.
void stepNewerSelect() {
	mark();
	deluge_audio_load_set_latency_ms(kHoldMs);
	select(kPresetE);
	g_stage = Stage::NewerFirst;
}

void stepNewerFirst() {
	if (!scrollLoadCurrent() || instrumentLoad.sampleLoads() == g_loadsBefore) {
		(void)waitMore("LC-STALE", "the scroll-load of LCE never began loading its samples");
		return;
	}
	g_newerTicket = loadInstrumentPresetUI.scrollLoadTicket();
	g_ticket = g_newerTicket;
	select(kPresetF);
	g_steps = 0;
	g_stage = Stage::NewerSecond;
}

void stepNewerSecond() {
	// LCF's load has replaced LCE's once it is the one in flight. Asked first: it runs the listing that asks for it.
	const bool asked = scrollLoadAsked();
	const LoadTicket newer = loadInstrumentPresetUI.scrollLoadTicket();
	if (asked && !g_queuedStaleSeen && instrumentLoad.currentTicket().generation == g_newerTicket.generation) {
		// LCF's load is queued behind LCE's, which is still in flight but is dropped for it at its next phase
		// boundary: LCE's ticket is already Stale, and its cancel must not drop either.
		g_queuedStaleSeen = true;
		const CancelResult result = instrumentLoad.cancel(g_newerTicket);
		const bool bothKept = instrumentLoad.inFlight()
		                      && instrumentLoad.currentTicket().generation == g_newerTicket.generation
		                      && instrumentLoad.cancellations() == g_cancelsBefore;
		printf("LC-STALE-QUEUED result=%d bothKept=%d\n", static_cast<int>(result), bothKept ? 1 : 0);
		if (result != CancelResult::Stale || !bothKept) {
			fail("LC-STALE", "a cancel naming a load a queued request replaces was not Stale, or dropped a load");
		}
	}
	if (!asked || !instrumentLoad.inFlight() || instrumentLoad.currentTicket().generation != newer.generation) {
		(void)waitMore("LC-STALE", "the scroll-load of LCF never replaced LCE's");
		return;
	}
	if (!g_queuedStaleSeen) {
		fail("LC-STALE",
		     "LCF's load was never seen queued behind LCE's, so a replaced load still in flight was not tested");
	}
	const CancelResult result = instrumentLoad.cancel(g_newerTicket);
	const bool newerKept = instrumentLoad.inFlight() && instrumentLoad.currentTicket().generation == newer.generation;
	printf("LC-STALE-REPLACED result=%d newerKept=%d\n", static_cast<int>(result), newerKept ? 1 : 0);
	if (result != CancelResult::Stale || !newerKept) {
		fail("LC-STALE", "a cancel naming a replaced load was not Stale, or cancelled the load that replaced it");
	}
	deluge_audio_load_set_latency_ms(0);
	g_steps = 0;
	g_stage = Stage::NewerSettle;
}

void stepNewerSettle() {
	if (instrumentLoad.inFlight()) {
		(void)waitMore("LC-STALE", "the newer load never finished");
		return;
	}
	const bool landed = g_clip->output->name == kNameF;
	const int32_t replaced = instrumentsNamed(kNameE);
	const int32_t cancels = instrumentLoad.cancellations() - g_cancelsBefore;
	printf("LC-STALE landed=%s replacedLeft=%d cancels=%d steps=%d\n", g_clip->output->name.c_str(), replaced, cancels,
	       g_steps);
	if (!landed || replaced != 0 || cancels != 0) {
		fail("LC-STALE", "the newer load did not complete after a stale cancel, or the replaced load was kept");
	}
	g_stage = Stage::RefusedStart;
}

/// refused_for_a_creation: the clip view's new-synth gesture, which always completes.
void stepRefusedStart() {
	loadInstrumentPresetUI.close();
	currentSong->setCurrentClip(g_clip);
	changeRootUI(&instrumentClipView);
	currentUIMode = UI_MODE_NONE;
	mark();
	(void)instrumentClipView.createNewInstrument(OutputType::SYNTH, false);
	const LoadTicket ticket = instrumentLoad.currentTicket();
	const CancelResult result = instrumentLoad.cancel(ticket);
	const bool stillInFlight =
	    instrumentLoad.inFlight() && instrumentLoad.currentTicket().generation == ticket.generation;
	printf("LC-REFUSED-CANCEL accepted=%d result=%d stillInFlight=%d\n", ticket.accepted() ? 1 : 0,
	       static_cast<int>(result), stillInFlight ? 1 : 0);
	if (!ticket.accepted()) {
		fail("LC-REFUSED", "the new-synth gesture had no load in flight, so nothing was tested");
	}
	if (result != CancelResult::Refused || !stillInFlight) {
		fail("LC-REFUSED", "a creation's load was cancelled");
	}
	g_stage = Stage::RefusedSettle;
}

void stepRefusedSettle() {
	if (instrumentLoad.inFlight()) {
		(void)waitMore("LC-REFUSED", "the creation never finished");
		return;
	}
	const bool created = g_clip->output != g_outputBefore && g_clip->output->type == OutputType::SYNTH;
	const int32_t cancels = instrumentLoad.cancellations() - g_cancelsBefore;
	printf("LC-REFUSED created=%d synth=%s cancels=%d error=%d steps=%d\n", created ? 1 : 0,
	       g_clip->output->name.c_str(), cancels, static_cast<int>(instrumentLoad.lastError()), g_steps);
	if (!created || cancels != 0 || instrumentLoad.lastError() != Error::NONE) {
		fail("LC-REFUSED", "the creation whose cancel was refused did not complete");
	}
	g_stage = Stage::Report;
}

void report() {
	deluge_file_bytes_set_latency_ms(0);
	deluge_audio_load_set_latency_ms(0);
	printf("LC-RESULT failures=%d\n", g_failures);
	g_stage = Stage::Done;
}

} // namespace

extern "C" {

void deluge_scenario_load_cancel_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Loading;
	g_stepDone = false;
	g_failures = 0;
	g_steps = 0;
	g_clip = nullptr;
	g_outputBefore = nullptr;
	g_ticket = {};
	g_newerTicket = {};
	g_queuedStaleSeen = false;
}

bool deluge_scenario_load_cancel_complete(void) {
	return g_stage == Stage::Done;
}

void deluge_scenario_load_cancel_dispatch_step(void) {
	g_stepDone = false;
	switch (g_stage) {
	case Stage::Loading:
		stepLoading();
		break;
	case Stage::Fixture:
		stepFixture();
		break;
	case Stage::Opening:
		stepOpening();
		break;
	case Stage::StageSelect:
		stepStageSelect();
		break;
	case Stage::StageWait:
		stepStageWait();
		break;
	case Stage::StageAfter:
		stepStageAfter();
		break;
	case Stage::SamplesSelect:
		stepSamplesSelect();
		break;
	case Stage::SamplesWait:
		stepSamplesWait();
		break;
	case Stage::SamplesAfter:
		stepSamplesAfter();
		break;
	case Stage::QueuedSelect:
		stepQueuedSelect();
		break;
	case Stage::QueuedFirst:
		stepQueuedFirst();
		break;
	case Stage::QueuedSecond:
		stepQueuedSecond();
		break;
	case Stage::QueuedAfter:
		stepQueuedAfter();
		break;
	case Stage::StaleSelect:
		stepStaleSelect();
		break;
	case Stage::StaleWait:
		stepStaleWait();
		break;
	case Stage::NewerSelect:
		stepNewerSelect();
		break;
	case Stage::NewerFirst:
		stepNewerFirst();
		break;
	case Stage::NewerSecond:
		stepNewerSecond();
		break;
	case Stage::NewerSettle:
		stepNewerSettle();
		break;
	case Stage::RefusedStart:
		stepRefusedStart();
		break;
	case Stage::RefusedSettle:
		stepRefusedSettle();
		break;
	case Stage::Report:
		report();
		break;
	case Stage::Done:
		break;
	}
	g_stepDone = true;
}

bool deluge_scenario_load_cancel_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_load_cancel_failures(void) {
	return g_failures;
}

} // extern "C"
