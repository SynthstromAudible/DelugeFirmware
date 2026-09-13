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

#include "harness/document_save_channel_scenario.h"

#ifdef DELUGE_HOST

#include "gui/context_menu/overwrite_file.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/save/save_kit_row_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/save/save_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "hid/button.h"
#include "io/file.hpp"
#include "libdeluge/file_bytes.h"
#include "libdeluge/file_write.h"
#include "memory/heaps.h"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "storage/byte_channel.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded
// `printf_`. That implementation emits through `putchar_`, which only the C host BSP defines -- this
// harness links the Rust BSP and has no provider, so the alias turns every line this file writes into
// an undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

/// Reaches `SaveUI::enterKeyPress()`, which is protected: the gate drives the REAL gesture rather
/// than the save machinery underneath it. Mirrors SaveRoundtripProbe and SongLoadChannelProbe.
class DocumentSaveChannelProbe {
public:
	static bool openOn() {
		Browser::enteredText.clear();
		if (!openUI(&saveInstrumentPresetUI)) {
			return false;
		}
		for (int32_t t = 0; Browser::isListingInProgress() && t < 200; t++) {
			saveInstrumentPresetUI.graphicsRoutine();
		}
		return !Browser::isListingInProgress();
	}
	/// Tick the browser just opened while its listing runs, and say whether it still is. Nothing else in the
	/// headless harness ticks a browser, and the listing runs on the browser's tick surface.
	static bool listingInProgress() {
		if (Browser::isListingInProgress()) {
			getCurrentUI()->graphicsRoutine();
		}
		return Browser::isListingInProgress();
	}
	static void typeName(const char* name) { Browser::enteredText = name; }
	static void pressEnter() { saveInstrumentPresetUI.enterKeyPress(); }
	static void pressBack() {
		(void)saveInstrumentPresetUI.buttonAction(deluge::hid::button::BACK, true, false);
		(void)saveInstrumentPresetUI.buttonAction(deluge::hid::button::BACK, false, false);
	}
	static std::string targetPath() { return Browser::currentDir + "/" + Browser::enteredText + ".XML"; }
	static bool overwritePromptOpen() { return getCurrentUI() == &deluge::gui::context_menu::overwriteFile; }
	/// Confirm the open overwrite prompt, as its OK does. Mirrors SongSaveChannelProbe::confirmOverwrite().
	static void confirmOverwrite() { (void)deluge::gui::context_menu::overwriteFile.acceptCurrentOption(); }
	/// @return Whether the save screen OPENED -- not whether its listing has finished. The listing is
	///         asynchronous and settles on a later tick, which is KitOpenWait's job; treating an
	///         in-flight listing as a failure here is what made the first run report that a screen
	///         which had opened perfectly well "did not open".
	static bool openKitRow() { return openUI(&saveKitRowUI); }
	static void pressEnterKitRow() { saveKitRowUI.enterKeyPress(); }
	/// Open the song save screen on SONGS, the way SongSaveChannelProbe does.
	static bool openSongSave() {
		Browser::currentDir = "SONGS";
		Browser::enteredText.clear();
		return openUI(&saveSongUI);
	}
	static void pressEnterSong() { saveSongUI.enterKeyPress(); }
	/// Dispatch a preset save the way the overwrite prompt and the save-song-or-instrument menu do: straight
	/// into dispatchSave(), which is the guard under test. The save screen's own buttons are already refused
	/// while any save runs.
	static bool dispatchPresetSave(const char* name) {
		return saveInstrumentPresetUI.dispatchSave({
		    .consumer = deluge::storage::SaveConsumer::EnterKey,
		    .mayOverwrite = false,
		    .collectingSamples = false,
		    .filePath = std::string("SYNTHS/") + name + ".XML",
		    .enteredText = name,
		    .dirPath = "SYNTHS",
		});
	}
};

namespace {

constexpr const char* kSaveName = "DSTEST01";
constexpr const char* kKitRowName = "DSKITROW";
constexpr const char* kSongGuardName = "DSSONG01";
constexpr const char* kSongGuardPath = "SONGS/DSSONG01.XML";
constexpr const char* kSongGuardPresetName = "DSGUARD1";
constexpr const char* kSongGuardPresetPath = "SYNTHS/DSGUARD1.XML";
/// Byte- and write-channel latency while the song save runs, so its collection and document machines span
/// many ticks and the preset save can be dispatched while they do.
constexpr uint32_t kSongGuardLatencyMs = 20;
constexpr const char* kAccountName = "DSACCT01";
/// Byte-channel latency while the accounting save verifies: long enough to remove the file between the write
/// landing and the read-back's first answer.
constexpr uint32_t kAccountLatencyMs = 50;
/// DS-OVERWRITE-FAIL: the byte channel's hold while the confirmed overwrite verifies. DS-ACCOUNTED's, for its reason:
/// long enough to truncate the temp file between its write landing and the read-back's first answer.
constexpr uint32_t kOverwriteLatencyMs = kAccountLatencyMs;

enum class Stage : uint8_t {
	Fixture,
	FixtureWait,
	Save,
	SaveBack,
	SaveWait,
	Verify,
	OccupiedOpen,
	OccupiedOpenWait,
	Occupied,
	OccupiedWait,
	OccupiedCheck,
	KitOpen,
	KitOpenWait,
	KitGuard,
	KitGuardWait,
	KitGuardCheck,
	KitReopenWait,
	KitSave,
	KitSaveWait,
	KitCheck,
	SongGuardOpen,
	SongGuardOpenWait,
	SongGuardSave,
	SongGuardDispatch,
	SongGuardWait,
	SongGuardCheck,
	AccountOpen,
	AccountOpenWait,
	AccountSave,
	AccountTamper,
	AccountWait,
	AccountCheck,
	OverwriteOpen,
	OverwriteOpenWait,
	OverwriteSave,
	OverwritePrompt,
	OverwriteWait,
	OverwriteCheck,
	OverwriteFailOpen,
	OverwriteFailOpenWait,
	OverwriteFailSave,
	OverwriteFailPrompt,
	OverwriteFailTamper,
	OverwriteFailWait,
	OverwriteFailDrain,
	OverwriteFailCheck,
	Report,
	Done,
};

Stage g_stage = Stage::Fixture;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_waitTicks = 0;
uint32_t g_crc = 0;
size_t g_bytes = 0;
std::string g_path;
std::string g_kitPath;
/// The instrument's name before the preset save. If it already equals the name being saved, the
/// rename check could pass without the save renaming anything, so the check has to know.
std::string g_nameBefore;
SoundDrum* g_kitDrum = nullptr;
/// DS-GUARD-SONG: whether the preset dispatch happened inside the window, and what dispatchSave() said.
bool g_songGuardTried = false;
bool g_songGuardRefused = false;
/// Staged saves completed before the song-guard stage, so a preset save that slipped through is seen.
int32_t g_completedBeforeSongGuard = 0;
/// DS-ACCOUNTED: counters at the save, whether the file was removed under the read-back, and its path.
int32_t g_writeServedBefore = 0;
int32_t g_verifyServedBefore = 0;
int32_t g_verifiedBefore = 0;
bool g_tampered = false;
std::string g_accountPath;
/// DS-OVERWRITE / DS-OVERWRITE-FAIL: the counters before each confirmed overwrite, and what the failing one found.
int32_t g_overwriteCompletedBefore = 0;
int32_t g_overwriteVerifiedBefore = 0;
int32_t g_swapServedBefore = 0;
int32_t g_overwriteWriteServedBefore = 0;
bool g_overwriteTampered = false;
bool g_overwriteTargetBefore = false;
uint32_t g_overwriteCrcBefore = 0;

/// A save spans ticks by construction; this bound exists so a save that never lands ends the run with
/// a REPORT rather than a silent timeout.
constexpr int32_t kMaxWaitTicks = 200000;

uint32_t crc32(const void* data, size_t len) {
	const auto* p = static_cast<const uint8_t*>(data);
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++) {
		crc ^= p[i];
		for (int32_t k = 0; k < 8; k++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
		}
	}
	return ~crc;
}

bool readWholeFile(const char* path, std::vector<uint8_t>& out) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened.has_value()) {
		return false;
	}
	auto size = opened->size();
	if (!size || *size == 0) {
		return false;
	}
	out.assign(*size, 0);
	auto read = opened->read(std::span<std::byte>(reinterpret_cast<std::byte*>(out.data()), *size));
	(void)opened->close();
	return read.has_value() && read->size() == *size;
}

/// Whether `path` names a file that is there, empty or not. `readWholeFile` reads an empty file as absent.
bool fileOnCard(const char* path) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened.has_value()) {
		return false;
	}
	(void)opened->close();
	return true;
}

bool endsWith(const std::string& s, const char* suffix) {
	const size_t n = strlen(suffix);
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

/// Build a sound drum to save, the way shipping code builds kit drums (slicer.cpp,
/// sample_browser.cpp) -- never from hand-written XML, which the preview gate found corrupts the heap
/// once the loader meets a preset with no parameter collections.
///
/// No kit: SaveKitRowUI reads only the drum and its ParamManager. setupAsBlankSynth rather than the
/// slicer's setupAsSample, because this is a synth drum, not a slice. The drum is deliberately leaked,
/// as writeBlankSynthPresetFile's Instrument is -- nothing else ever scans it.
bool buildKitRowFixture() {
	// Outlives the save: the document is serialised on a later tick than this runs.
	static ParamManagerForTimeline paramManager;
	if (paramManager.setupWithPatching() != Error::NONE) {
		printf("DS-KITROW FAIL could not set up the drum's ParamManager\n");
		return false;
	}
	void* memory = deluge::memory::alloc_fast(sizeof(SoundDrum));
	if (memory == nullptr) {
		printf("DS-KITROW FAIL no memory for the drum\n");
		return false;
	}
	auto* drum = new (memory) SoundDrum();
	// Deliberately NOT the name it is saved under: the save must give the drum its new name, and a
	// fixture that already carried it would pass that check without the save doing anything.
	drum->drumName = "KITFIXTR";
	g_kitDrum = drum;
	Sound::initParams(&paramManager);
	drum->setupAsBlankSynth(&paramManager);
	saveKitRowUI.setup(drum, &paramManager);
	return true;
}

void runTick() {
	switch (g_stage) {
	case Stage::Fixture: {
		// A root UI beneath, because the overwrite prompt this gate provokes is pushed with openUI().
		changeRootUI(&arrangerView);
		(void)DocumentSaveChannelProbe::openOn();
		g_stage = Stage::FixtureWait;
		break;
	}
	case Stage::FixtureWait: {
		if (!DocumentSaveChannelProbe::listingInProgress()) {
			g_stage = Stage::Save;
		}
		break;
	}
	case Stage::Save: {
		DocumentSaveChannelProbe::typeName(kSaveName);
		g_path = DocumentSaveChannelProbe::targetPath();
		// Start from a known-absent target so this is genuinely the "free name" case.
		(void)deluge::io::unlink(g_path.c_str());
		if (Instrument* instrument = getCurrentInstrument(); instrument != nullptr) {
			g_nameBefore = instrument->name;
		}
		DocumentSaveChannelProbe::pressEnter();
		g_stage = Stage::SaveBack;
		break;
	}
	case Stage::SaveBack: {
		// DS-BACKMIDSAVE / DS-EXITMIDSAVE. The staged preset save spans ticks too, and ends in close() just
		// as the song save does, so leaving its screen mid-save carries the same hazard.
		const bool inFlight = SaveUI::saveInFlight();
		DocumentSaveChannelProbe::pressBack();
		const bool backBlocked = getCurrentUI() == &saveInstrumentPresetUI;
		printf("DS-BACKMIDSAVE inFlight=%d back=%d\n", (int)inFlight, (int)backBlocked);
		if (!inFlight) {
			printf("DS-BACKMIDSAVE FAIL the save had already finished; nothing was pressed mid-save, so this proves "
			       "nothing\n");
			g_failures++;
		}
		else if (!backBlocked) {
			printf("DS-BACKMIDSAVE FAIL BACK closed the preset save screen while the save was still running\n");
			g_failures++;
		}
		if (backBlocked) {
			(void)saveInstrumentPresetUI.exitUI();
			const bool exitBlocked = getCurrentUI() == &saveInstrumentPresetUI;
			printf("DS-EXITMIDSAVE exit=%d\n", (int)exitBlocked);
			if (!exitBlocked) {
				printf(
				    "DS-EXITMIDSAVE FAIL a long-press exit closed the preset save screen while the save was running\n");
				g_failures++;
			}
		}
		else {
			printf("DS-EXITMIDSAVE exit=skipped\n");
		}
		g_stage = Stage::SaveWait;
		break;
	}
	case Stage::SaveWait: {
		// saveInFlight(), NOT the UI being open: a staged save parks between ticks, and the save
		// screen stays up either way.
		if (!SaveUI::saveInFlight()) {
			g_stage = Stage::Verify;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("DS-SAVED FAIL the save never finished (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::Verify: {
		std::vector<uint8_t> bytes;
		if (!readWholeFile(g_path.c_str(), bytes)) {
			printf("DS-SAVED FAIL nothing was written to %s\n", g_path.c_str());
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		g_bytes = bytes.size();
		g_crc = crc32(bytes.data(), bytes.size());
		// Shallow on purpose: the cross-leg CRC comparison in the script is what carries the weight.
		// These two catch the shapes that would make that comparison vacuous -- a document with no
		// prolog (which is what a staged save that forgot it produces) or one too short to be real.
		if (bytes.size() < 100 || memcmp(bytes.data(), "<?xml", 5) != 0) {
			printf("DS-SAVED FAIL the written document is short or has no prolog (%zu bytes)\n", bytes.size());
			g_failures++;
		}
		// The file being here says nothing about whether the save SUCCEEDED: a save rejected by its
		// read-back has already written it. Only the counters taken where a save genuinely completes
		// can say that.
		if (SaveUI::stagedSavesCompleted() != 1 || SaveUI::stagedSavesVerified() != 1) {
			printf("DS-VERIFIED FAIL the preset save left a file but did not complete a verified save "
			       "(completed=%d verified=%d)\n",
			       (int)SaveUI::stagedSavesCompleted(), (int)SaveUI::stagedSavesVerified());
			g_failures++;
		}
		printf("DS-SAVED path=%s bytes=%zu crc=%08X\n", g_path.c_str(), g_bytes, g_crc);
		{
			// DS-FINISHED. A save is not over when the document lands: it closes the screen and gives the
			// instrument its new name. Staging the save once skipped every step of that without any
			// gate noticing -- this one re-saved on a screen a finished save would have closed.
			const bool closed = getCurrentUI() != &saveInstrumentPresetUI;
			Instrument* instrument = getCurrentInstrument();
			// named is only EVIDENCE if the name was not already this before the save. A card image left
			// behind by an earlier failed run can boot an instrument already carrying it -- which once
			// reported named=1 on a build where nothing renamed anything.
			const bool alreadyNamed = g_nameBefore == kSaveName;
			const bool named = !alreadyNamed && instrument != nullptr && instrument->name == kSaveName;
			if (alreadyNamed) {
				printf("DS-FINISHED FAIL the instrument was already named %s before saving; the rename check "
				       "cannot tell anything and the fixture is contaminated\n",
				       kSaveName);
			}
			printf("DS-FINISHED closed=%d named=%d\n", (int)closed, (int)named);
			if (!closed || !named) {
				printf("DS-FINISHED FAIL the preset was written but the save never finished\n");
				g_failures++;
			}
		}
		g_stage = Stage::OccupiedOpen;
		break;
	}
	case Stage::OccupiedOpen: {
		// A finished save closes its screen, so saving again means opening it again, as a user would.
		(void)DocumentSaveChannelProbe::openOn();
		g_stage = Stage::OccupiedOpenWait;
		break;
	}
	case Stage::OccupiedOpenWait: {
		// Type on a LATER tick than the open: the listing settles asynchronously and picks a fresh
		// name when it does, which would overwrite a name typed any earlier.
		if (!DocumentSaveChannelProbe::listingInProgress()) {
			g_stage = Stage::Occupied;
		}
		break;
	}
	case Stage::Occupied: {
		// The SAME name again. The user did not ask to replace it, so this must PROMPT and must not
		// write -- silently replacing somebody's preset is the failure this stage exists to catch.
		DocumentSaveChannelProbe::typeName(kSaveName);
		DocumentSaveChannelProbe::pressEnter();
		g_waitTicks = 0;
		g_stage = Stage::OccupiedWait;
		break;
	}
	case Stage::OccupiedWait: {
		if (!SaveUI::saveInFlight() || ++g_waitTicks > kMaxWaitTicks) {
			g_stage = Stage::OccupiedCheck;
		}
		break;
	}
	case Stage::OccupiedCheck: {
		const bool prompted = DocumentSaveChannelProbe::overwritePromptOpen();
		std::vector<uint8_t> bytes;
		const bool stillThere = readWholeFile(g_path.c_str(), bytes);
		const uint32_t crcNow = stillThere ? crc32(bytes.data(), bytes.size()) : 0;
		printf("DS-OCCUPIED prompted=%d unchanged=%d\n", (int)prompted, (int)(stillThere && crcNow == g_crc));
		if (!prompted) {
			printf("DS-OCCUPIED FAIL a taken name did not prompt\n");
			g_failures++;
		}
		if (!stillThere || crcNow != g_crc) {
			printf("DS-OCCUPIED FAIL the existing document was modified without the user agreeing\n");
			g_failures++;
		}
		g_stage = Stage::KitOpen;
		break;
	}
	case Stage::KitOpen: {
		// The occupied stage leaves the overwrite prompt up, unanswered. Start from a clean UI stack
		// rather than stacking a second save screen on top of it.
		changeRootUI(&arrangerView);
		if (!buildKitRowFixture() || !DocumentSaveChannelProbe::openKitRow()) {
			printf("DS-KITROW FAIL the kit-row save screen did not open\n");
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		g_stage = Stage::KitOpenWait;
		break;
	}
	case Stage::KitOpenWait: {
		// Type on a LATER tick than the open: the listing settles asynchronously and picks a fresh
		// name when it does, which would overwrite a name typed any earlier.
		if (!DocumentSaveChannelProbe::listingInProgress()) {
			g_stage = Stage::KitGuard;
		}
		break;
	}
	case Stage::KitGuard: {
		// DS-GUARD. Save the kit row under the name the preset was JUST saved as. Once the preset save
		// finishes, that instrument holds the slot SYNTHS/DSTEST01 in the song, and a kit row may not
		// take a slot another instrument already holds -- so this must be refused before anything is
		// written. It depends on the preset rename actually happening, which is the point: the check
		// and the bookkeeping it relies on were both lost together.
		DocumentSaveChannelProbe::typeName(kSaveName);
		DocumentSaveChannelProbe::pressEnterKitRow();
		g_waitTicks = 0;
		g_stage = Stage::KitGuardWait;
		break;
	}
	case Stage::KitGuardWait: {
		if (!SaveUI::saveInFlight()) {
			g_stage = Stage::KitGuardCheck;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("DS-GUARD FAIL the refused save never ended (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::KitGuardCheck: {
		// Refused means: still on the kit-row screen (not closed by a finished save, not replaced by the
		// overwrite prompt a write attempt would raise), and no save counted.
		const bool refused = getCurrentUI() == &saveKitRowUI && SaveUI::stagedSavesCompleted() == 1;
		std::vector<uint8_t> bytes;
		const bool stillThere = readWholeFile(g_path.c_str(), bytes);
		const bool unchanged = stillThere && crc32(bytes.data(), bytes.size()) == g_crc;
		printf("DS-GUARD refused=%d unchanged=%d\n", (int)refused, (int)unchanged);
		if (!refused) {
			printf("DS-GUARD FAIL a kit row was allowed to take a slot another instrument in the song holds\n");
			g_failures++;
		}
		if (!unchanged) {
			printf("DS-GUARD FAIL the preset occupying that slot was modified\n");
			g_failures++;
		}
		if (getCurrentUI() != &saveKitRowUI) {
			// Whatever took the screen (the overwrite prompt, on a build without the guard), put the
			// kit-row screen back ONCE and go straight on to the free-name save. Routing back through
			// KitOpenWait re-entered KitGuard, which retried a guard that was not there until the run
			// hit its wedge deadline -- a failure that should have been reported once, and moved past.
			changeRootUI(&arrangerView);
			(void)DocumentSaveChannelProbe::openKitRow();
			g_stage = Stage::KitReopenWait;
			break;
		}
		g_stage = Stage::KitSave;
		break;
	}
	case Stage::KitReopenWait: {
		if (!DocumentSaveChannelProbe::listingInProgress()) {
			g_stage = Stage::KitSave;
		}
		break;
	}
	case Stage::KitSave: {
		DocumentSaveChannelProbe::typeName(kKitRowName);
		g_kitPath = DocumentSaveChannelProbe::targetPath();
		(void)deluge::io::unlink(g_kitPath.c_str());
		DocumentSaveChannelProbe::pressEnterKitRow();
		g_waitTicks = 0;
		g_stage = Stage::KitSaveWait;
		break;
	}
	case Stage::KitSaveWait: {
		if (!SaveUI::saveInFlight()) {
			g_stage = Stage::KitCheck;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("DS-KITROW FAIL the kit-row save never finished (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::KitCheck: {
		std::vector<uint8_t> bytes;
		if (!readWholeFile(g_kitPath.c_str(), bytes)) {
			printf("DS-KITROW FAIL nothing was written to %s\n", g_kitPath.c_str());
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		printf("DS-KITROW path=%s bytes=%zu crc=%08X\n", g_kitPath.c_str(), bytes.size(),
		       crc32(bytes.data(), bytes.size()));
		// Two completed, two verified: the preset and this. The occupied save between them opened a
		// prompt and wrote nothing, so a third would itself be the counting defect.
		if (SaveUI::stagedSavesCompleted() != 2 || SaveUI::stagedSavesVerified() != 2) {
			printf("DS-KITROW FAIL the kit-row save did not complete a verified save (completed=%d verified=%d)\n",
			       (int)SaveUI::stagedSavesCompleted(), (int)SaveUI::stagedSavesVerified());
			g_failures++;
		}
		{
			const bool closed = getCurrentUI() != &saveKitRowUI;
			const bool named = g_kitDrum != nullptr && g_kitDrum->drumName == kKitRowName;
			const char* path = g_kitDrum != nullptr ? g_kitDrum->path.c_str() : "";
			printf("DS-KITFINISHED closed=%d named=%d path=%s\n", (int)closed, (int)named, path);
			if (!closed || !named) {
				printf("DS-KITFINISHED FAIL the kit row was written but the save never finished\n");
				g_failures++;
			}
		}
		g_stage = Stage::SongGuardOpen;
		break;
	}
	case Stage::SongGuardOpen: {
		// DS-GUARD-SONG. A song save releases the save guard as soon as its name is known to be free, and its
		// collection and document machines then run for many more ticks. A preset save started in that window
		// would serialise into the shared serialiser under the song's staged document.
		(void)deluge::io::unlink(kSongGuardPath);
		(void)deluge::io::unlink(kSongGuardPresetPath);
		changeRootUI(&arrangerView);
		if (!DocumentSaveChannelProbe::openSongSave()) {
			printf("DS-GUARD-SONG FAIL the song save screen did not open\n");
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		g_stage = Stage::SongGuardOpenWait;
		break;
	}
	case Stage::SongGuardOpenWait: {
		if (!DocumentSaveChannelProbe::listingInProgress()) {
			g_stage = Stage::SongGuardSave;
		}
		break;
	}
	case Stage::SongGuardSave: {
		g_completedBeforeSongGuard = SaveUI::stagedSavesCompleted();
		g_songGuardTried = false;
		g_songGuardRefused = false;
		deluge_file_bytes_set_latency_ms(kSongGuardLatencyMs);
		deluge_file_write_set_latency_ms(kSongGuardLatencyMs);
		DocumentSaveChannelProbe::typeName(kSongGuardName);
		DocumentSaveChannelProbe::pressEnterSong();
		g_waitTicks = 0;
		g_stage = Stage::SongGuardDispatch;
		break;
	}
	case Stage::SongGuardDispatch: {
		const bool songRunning = saveSongUI.collectInFlight() || saveSongUI.docInFlight();
		if (songRunning && !SaveUI::saveInFlight()) {
			// The window: the song save is still running, and the guard dispatchSave() used to check is down.
			g_songGuardTried = true;
			g_songGuardRefused = !DocumentSaveChannelProbe::dispatchPresetSave(kSongGuardPresetName);
			g_stage = Stage::SongGuardWait;
			break;
		}
		if (!songRunning && !SaveUI::saveInFlight()) {
			// The song save ended without the window ever opening; SongGuardCheck reports that nothing was tested.
			g_stage = Stage::SongGuardWait;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("DS-GUARD-SONG FAIL the song save never reached its collection or document machine (%d ticks)\n",
			       g_waitTicks);
			g_failures++;
			g_stage = Stage::SongGuardWait;
		}
		break;
	}
	case Stage::SongGuardWait: {
		if (!saveSongUI.collectInFlight() && !saveSongUI.docInFlight() && !SaveUI::saveInFlight()) {
			deluge_file_bytes_set_latency_ms(0);
			deluge_file_write_set_latency_ms(0);
			g_stage = Stage::SongGuardCheck;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("DS-GUARD-SONG FAIL the song save never finished (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::SongGuardCheck: {
		std::vector<uint8_t> song;
		const bool songSaved =
		    readWholeFile(kSongGuardPath, song) && song.size() > 64 && std::memcmp(song.data(), "<?xml", 5) == 0;
		std::vector<uint8_t> preset;
		const bool presetWritten =
		    readWholeFile(kSongGuardPresetPath, preset) || SaveUI::stagedSavesCompleted() != g_completedBeforeSongGuard;
		printf("DS-GUARD-SONG tried=%d refused=%d songSaved=%d presetWritten=%d\n", (int)g_songGuardTried,
		       (int)g_songGuardRefused, (int)songSaved, (int)presetWritten);
		if (!g_songGuardTried) {
			printf("DS-GUARD-SONG FAIL no preset save was dispatched while the song save ran with the guard down; "
			       "this proves nothing\n");
			g_failures++;
		}
		else if (!g_songGuardRefused) {
			printf("DS-GUARD-SONG FAIL a preset save was accepted while a song save was still running\n");
			g_failures++;
		}
		if (!songSaved) {
			printf("DS-GUARD-SONG FAIL the song save did not leave a whole document at %s\n", kSongGuardPath);
			g_failures++;
		}
		if (presetWritten) {
			printf("DS-GUARD-SONG FAIL the refused preset save wrote a file or counted a save\n");
			g_failures++;
		}
		g_stage = Stage::AccountOpen;
		break;
	}
	case Stage::AccountOpen: {
		// DS-ACCOUNTED. A read-back that ends Failed after a real channel answer still used the channel, and the
		// served count has to say so. The file is removed between the write landing and the read-back's first
		// answer, so the length stage collects NOT_FOUND -- one served operation -- and fails the save.
		changeRootUI(&arrangerView);
		(void)DocumentSaveChannelProbe::openOn();
		g_stage = Stage::AccountOpenWait;
		break;
	}
	case Stage::AccountOpenWait: {
		if (!DocumentSaveChannelProbe::listingInProgress()) {
			g_stage = Stage::AccountSave;
		}
		break;
	}
	case Stage::AccountSave: {
		DocumentSaveChannelProbe::typeName(kAccountName);
		g_accountPath = DocumentSaveChannelProbe::targetPath();
		(void)deluge::io::unlink(g_accountPath.c_str());
		g_writeServedBefore = SaveUI::writeChannelServedOps();
		g_verifyServedBefore = SaveUI::verifyChannelServedOps();
		g_verifiedBefore = SaveUI::stagedSavesVerified();
		g_tampered = false;
		deluge_file_bytes_set_latency_ms(kAccountLatencyMs);
		DocumentSaveChannelProbe::pressEnter();
		g_waitTicks = 0;
		g_stage = Stage::AccountTamper;
		break;
	}
	case Stage::AccountTamper: {
		if (SaveUI::writeChannelServedOps() > g_writeServedBefore) {
			// The write has landed and the read-back has begun; its stat is held by the latency.
			(void)deluge::io::unlink(g_accountPath.c_str());
			g_tampered = true;
			g_stage = Stage::AccountWait;
			break;
		}
		if (!SaveUI::saveInFlight() || ++g_waitTicks > kMaxWaitTicks) {
			// Ended before its write landed: nothing to remove. AccountCheck reports that nothing was tested.
			g_stage = Stage::AccountWait;
		}
		break;
	}
	case Stage::AccountWait: {
		if (!SaveUI::saveInFlight()) {
			deluge_file_bytes_set_latency_ms(0);
			g_stage = Stage::AccountCheck;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("DS-ACCOUNTED FAIL the save never finished (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::AccountCheck: {
		const bool failed = SaveUI::stagedSavesVerified() == g_verifiedBefore;
		const int32_t verifyServed = SaveUI::verifyChannelServedOps() - g_verifyServedBefore;
		printf("DS-ACCOUNTED tampered=%d failed=%d verifyServed=%d\n", (int)g_tampered, (int)failed, (int)verifyServed);
		if (!g_tampered || !failed) {
			printf("DS-ACCOUNTED FAIL the read-back was not made to fail, so the count proves nothing\n");
			g_failures++;
		}
		else if (verifyServed != 1) {
			printf("DS-ACCOUNTED FAIL the read-back's length stat was answered and failed the save, but the served "
			       "count moved by %d, not 1\n",
			       (int)verifyServed);
			g_failures++;
		}
		g_stage = Stage::OverwriteOpen;
		break;
	}
	case Stage::OverwriteOpen: {
		// DS-OVERWRITE. Last, after every stage that counts staged saves absolutely: DS-GUARD and DS-KITROW read
		// `stagedSavesCompleted()` as 1 and 2. Save onto the preset's own name again, and confirm the prompt that
		// raises.
		changeRootUI(&arrangerView);
		(void)DocumentSaveChannelProbe::openOn();
		g_stage = Stage::OverwriteOpenWait;
		break;
	}
	case Stage::OverwriteOpenWait: {
		// Type on a later tick than the open, for DS-OCCUPIED's reason: the listing picks a fresh name when it settles.
		if (!DocumentSaveChannelProbe::listingInProgress()) {
			g_stage = Stage::OverwriteSave;
		}
		break;
	}
	case Stage::OverwriteSave: {
		DocumentSaveChannelProbe::typeName(kSaveName);
		DocumentSaveChannelProbe::pressEnter();
		g_waitTicks = 0;
		g_stage = Stage::OverwritePrompt;
		break;
	}
	case Stage::OverwritePrompt: {
		if (SaveUI::saveInFlight() && ++g_waitTicks <= kMaxWaitTicks) {
			break;
		}
		if (!DocumentSaveChannelProbe::overwritePromptOpen()) {
			printf("DS-OVERWRITE FAIL fixture: saving onto %s again did not prompt\n", kSaveName);
			g_failures++;
			g_stage = Stage::OverwriteFailOpen;
			break;
		}
		g_overwriteCompletedBefore = SaveUI::stagedSavesCompleted();
		g_overwriteVerifiedBefore = SaveUI::stagedSavesVerified();
		g_swapServedBefore = SaveUI::swapChannelServedOps();
		DocumentSaveChannelProbe::confirmOverwrite();
		g_waitTicks = 0;
		g_stage = Stage::OverwriteWait;
		break;
	}
	case Stage::OverwriteWait: {
		if (SaveUI::saveInFlight() && ++g_waitTicks <= kMaxWaitTicks) {
			break;
		}
		g_stage = Stage::OverwriteCheck;
		break;
	}
	case Stage::OverwriteCheck: {
		std::vector<uint8_t> bytes;
		const bool present = readWholeFile(g_path.c_str(), bytes);
		const bool whole = present && bytes.size() >= 100 && memcmp(bytes.data(), "<?xml", 5) == 0;
		const bool tmpLeft = fileOnCard((g_path + ".tmp").c_str());
		const bool usedTemp = endsWith(SaveUI::stagedWritePath(), ".tmp");
		const int32_t completed = SaveUI::stagedSavesCompleted() - g_overwriteCompletedBefore;
		const int32_t verified = SaveUI::stagedSavesVerified() - g_overwriteVerifiedBefore;
		const int32_t swapServed = SaveUI::swapChannelServedOps() - g_swapServedBefore;
		const bool promptClosed = !isUIOpen(&deluge::gui::context_menu::overwriteFile);
		printf("DS-OVERWRITE completed=%d verified=%d usedTemp=%d swapServed=%d whole=%d tmpLeft=%d promptClosed=%d\n",
		       (int)completed, (int)verified, (int)usedTemp, (int)swapServed, (int)whole, (int)tmpLeft,
		       (int)promptClosed);
		if (SaveUI::saveInFlight()) {
			printf("DS-OVERWRITE FAIL the confirmed overwrite never finished (%d ticks)\n", g_waitTicks);
			g_failures++;
		}
		if (completed != 1 || verified != 1) {
			printf("DS-OVERWRITE FAIL the confirmed overwrite did not complete a verified save\n");
			g_failures++;
		}
		if (!usedTemp) {
			printf("DS-OVERWRITE FAIL the confirmed overwrite wrote straight onto the document it replaces, not a temp "
			       "file\n");
			g_failures++;
		}
		if (swapServed < 2) {
			printf("DS-OVERWRITE FAIL the temp file was not swapped into place through the byte channel\n");
			g_failures++;
		}
		if (!whole) {
			printf("DS-OVERWRITE FAIL %s holds no whole document after the overwrite\n", g_path.c_str());
			g_failures++;
		}
		if (tmpLeft) {
			printf("DS-OVERWRITE FAIL the temp file is still on the card after a successful overwrite\n");
			g_failures++;
		}
		if (!promptClosed) {
			printf("DS-OVERWRITE FAIL the overwrite prompt is still open after the save finished\n");
			g_failures++;
		}
		g_stage = Stage::OverwriteFailOpen;
		break;
	}
	case Stage::OverwriteFailOpen: {
		// DS-OVERWRITE-FAIL. Start from a clean UI stack: a failed DS-OVERWRITE may have left its screens up, and a
		// finished one closed them.
		changeRootUI(&arrangerView);
		(void)DocumentSaveChannelProbe::openOn();
		g_stage = Stage::OverwriteFailOpenWait;
		break;
	}
	case Stage::OverwriteFailOpenWait: {
		if (!DocumentSaveChannelProbe::listingInProgress()) {
			g_stage = Stage::OverwriteFailSave;
		}
		break;
	}
	case Stage::OverwriteFailSave: {
		DocumentSaveChannelProbe::typeName(kSaveName);
		DocumentSaveChannelProbe::pressEnter();
		g_waitTicks = 0;
		g_stage = Stage::OverwriteFailPrompt;
		break;
	}
	case Stage::OverwriteFailPrompt: {
		if (SaveUI::saveInFlight() && ++g_waitTicks <= kMaxWaitTicks) {
			break;
		}
		if (!DocumentSaveChannelProbe::overwritePromptOpen()) {
			printf("DS-OVERWRITE-FAIL FAIL fixture: saving onto %s again did not prompt\n", kSaveName);
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		std::vector<uint8_t> bytes;
		g_overwriteTargetBefore = readWholeFile(g_path.c_str(), bytes);
		g_overwriteCrcBefore = g_overwriteTargetBefore ? crc32(bytes.data(), bytes.size()) : 0;
		g_overwriteVerifiedBefore = SaveUI::stagedSavesVerified();
		g_overwriteWriteServedBefore = SaveUI::writeChannelServedOps();
		g_overwriteTampered = false;
		// Hold the read-back's first answer, so the temp file can be truncated after its write lands and before it
		// is checked.
		deluge_file_bytes_set_latency_ms(kOverwriteLatencyMs);
		DocumentSaveChannelProbe::confirmOverwrite();
		g_waitTicks = 0;
		g_stage = Stage::OverwriteFailTamper;
		break;
	}
	case Stage::OverwriteFailTamper: {
		if (SaveUI::writeChannelServedOps() > g_overwriteWriteServedBefore) {
			// The write has landed and the read-back has begun; its first answer is held by the latency.
			const std::string temp = g_path + ".tmp";
			if (fileOnCard(temp.c_str())) {
				auto truncated = deluge::io::File::open(temp.c_str(), DELUGE_FILE_WRITE_CREATE);
				if (truncated.has_value()) {
					(void)truncated->close();
					g_overwriteTampered = true;
				}
			}
			g_stage = Stage::OverwriteFailWait;
			break;
		}
		if (!SaveUI::saveInFlight() || ++g_waitTicks > kMaxWaitTicks) {
			// Ended before its write landed: nothing to truncate. OverwriteFailCheck reports that nothing was tested.
			g_stage = Stage::OverwriteFailWait;
		}
		break;
	}
	case Stage::OverwriteFailWait: {
		if (SaveUI::saveInFlight()) {
			if (++g_waitTicks > kMaxWaitTicks) {
				printf("DS-OVERWRITE-FAIL FAIL the save never finished (%d ticks)\n", g_waitTicks);
				g_failures++;
				g_stage = Stage::Report;
			}
			break;
		}
		deluge_file_bytes_set_latency_ms(0);
		g_waitTicks = 0;
		g_stage = Stage::OverwriteFailDrain;
		break;
	}
	case Stage::OverwriteFailDrain: {
		// A failed save queues the temp file's removal on the byte channel; let it run before looking for the file.
		if (!deluge::storage::bytes::idle() && ++g_waitTicks <= kMaxWaitTicks) {
			break;
		}
		g_stage = Stage::OverwriteFailCheck;
		break;
	}
	case Stage::OverwriteFailCheck: {
		std::vector<uint8_t> bytes;
		const bool present = readWholeFile(g_path.c_str(), bytes);
		const bool unchanged =
		    g_overwriteTargetBefore && present && crc32(bytes.data(), bytes.size()) == g_overwriteCrcBefore;
		const bool failed = SaveUI::stagedSavesVerified() == g_overwriteVerifiedBefore;
		const bool tmpLeft = fileOnCard((g_path + ".tmp").c_str());
		printf("DS-OVERWRITE-FAIL tampered=%d failed=%d targetUnchanged=%d tmpLeft=%d\n", (int)g_overwriteTampered,
		       (int)failed, (int)unchanged, (int)tmpLeft);
		if (!g_overwriteTampered) {
			printf("DS-OVERWRITE-FAIL FAIL the read-back was not made to fail: there was no temp file to truncate, so "
			       "nothing was tested\n");
			g_failures++;
		}
		if (!failed) {
			printf("DS-OVERWRITE-FAIL FAIL a confirmed overwrite whose read-back was made to fail still counted as "
			       "verified\n");
			g_failures++;
		}
		if (!unchanged) {
			printf("DS-OVERWRITE-FAIL FAIL the document a failed overwrite was replacing is gone or changed\n");
			g_failures++;
		}
		if (tmpLeft) {
			printf("DS-OVERWRITE-FAIL FAIL the temp file of a failed overwrite is still on the card\n");
			g_failures++;
		}
		g_stage = Stage::Report;
		break;
	}
	case Stage::Report: {
		printf("DS-VERIFIED completedSaves=%d verified=%d\n", (int)SaveUI::stagedSavesCompleted(),
		       (int)SaveUI::stagedSavesVerified());
		printf("DS-SUMMARY served=%d offOwnerTicks=%d verifyServed=%d verifyOwner=%d\n",
		       (int)SaveUI::writeChannelServedOps(), (int)SaveUI::offOwnerSaveTicks(),
		       (int)SaveUI::verifyChannelServedOps(), (int)SaveUI::verifyOwnerServedOps());
		printf("DS-RESULT failures=%d\n", g_failures);
		// Leave the card as we found it: these images are fingerprint-keyed and SHARED between gates,
		// and a file left behind here changes what another gate's browser listing sees.
		if (!g_path.empty()) {
			(void)deluge::io::unlink(g_path.c_str());
			(void)deluge::io::unlink((g_path + ".tmp").c_str());
		}
		if (!g_kitPath.empty()) {
			(void)deluge::io::unlink(g_kitPath.c_str());
		}
		(void)deluge::io::unlink(kSongGuardPath);
		(void)deluge::io::unlink(kSongGuardPresetPath);
		if (!g_accountPath.empty()) {
			(void)deluge::io::unlink(g_accountPath.c_str());
		}
		deluge_file_bytes_set_latency_ms(0);
		deluge_file_write_set_latency_ms(0);
		fflush(stdout);
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;
	}
	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_document_save_channel_begin(void) {
	// Line-buffer stdout: piped into a script it is block-buffered by default, and the harness exits
	// without flushing C stdio, so every DS- line -- which IS this gate's output -- would be lost
	// exactly when the gate fails.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Fixture;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_waitTicks = 0;
	g_crc = 0;
	g_bytes = 0;
	g_path.clear();
	g_kitPath.clear();
	g_kitDrum = nullptr;
	g_nameBefore.clear();
	g_songGuardTried = false;
	g_songGuardRefused = false;
	g_completedBeforeSongGuard = 0;
	g_writeServedBefore = 0;
	g_verifyServedBefore = 0;
	g_verifiedBefore = 0;
	g_tampered = false;
	g_accountPath.clear();
	g_overwriteCompletedBefore = 0;
	g_overwriteVerifiedBefore = 0;
	g_swapServedBefore = 0;
	g_overwriteWriteServedBefore = 0;
	g_overwriteTampered = false;
	g_overwriteTargetBefore = false;
	g_overwriteCrcBefore = 0;
}

bool deluge_scenario_document_save_channel_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_document_save_channel_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// A marker op behind whatever the tick queued: the owner queue is FIFO, so this runs after any
	// work the tick dispatched. Setting g_stepDone directly races the owner, and the scenario sails
	// past work it only just queued.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_document_save_channel_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_document_save_channel_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
