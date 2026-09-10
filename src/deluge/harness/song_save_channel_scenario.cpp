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

#include "harness/song_save_channel_scenario.h"

#ifdef DELUGE_HOST

#include "gui/context_menu/overwrite_file.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/save/save_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/instrument_clip_view.h"
#include "io/file.hpp"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"
#include "util/string.h"
#include <cstdio>
#include <string>
#include <vector>

// The GUI headers reach `src/lib/printf.h`, which soft-aliases printf to an implementation this
// harness has no `putchar_` for -- every line below would become an undefined symbol at link time.
#ifdef printf
#undef printf
#endif

/// Reaches the protected ENTER gesture. A save made by calling `performSave()` directly would not be
/// the save a user makes, and this gate exists to test the one they make.
class SongSaveChannelProbe {
public:
	static bool openOn(const char* dir) {
		Browser::currentDir = dir;
		Browser::enteredText.clear();
		if (!openUI(&saveSongUI)) {
			return false;
		}
		for (int32_t t = 0; Browser::isListingInProgress() && t < 200; t++) {
			saveSongUI.graphicsRoutine();
		}
		return !Browser::isListingInProgress();
	}
	static bool listingInProgress() { return Browser::isListingInProgress(); }
	static void typeName(const char* name) { Browser::enteredText = name; }
	static void pressEnter() { saveSongUI.enterKeyPress(); }
	static bool overwritePromptOpen() { return getCurrentUI() == &deluge::gui::context_menu::overwriteFile; }
	static void confirmOverwrite() { (void)deluge::gui::context_menu::overwriteFile.acceptCurrentOption(); }
};

namespace {

constexpr const char* kSaveDir = "SONGS";
constexpr const char* kSaveName = "SSTEST01";
constexpr const char* kSavePath = "SONGS/SSTEST01.XML";

/// Notes per row. Sized so the document clears 65536 bytes -- two doublings of the 32768-byte
/// staging buffer -- with the default song's own ~11.5 KB underneath it.
constexpr int32_t kNotesPerRow = 220;

enum class Stage : uint8_t {
	Inflate,
	Fixture,
	FixtureWait,
	SaveFirst,
	SaveFirstWait,
	SaveFirstCheck,
	ParseWait,
	ParseCheck,
	Reopen,
	SaveSecond,
	SaveSecondCheck,
	Confirm,
	ConfirmWait,
	ConfirmCheck,
	IntactWait,
	IntactCheck,
	Report,
	Done,
};

Stage g_stage = Stage::Inflate;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_waitTicks = 0;
int32_t g_notesAdded = 0;
uint32_t g_crcFirst = 0;
size_t g_bytesFirst = 0;

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
		(void)opened->close();
		return false;
	}
	out.assign(*size, 0);
	auto read = opened->read(std::span<std::byte>(reinterpret_cast<std::byte*>(out.data()), *size));
	(void)opened->close();
	return read.has_value() && read->size() == *size;
}

/// Grow the song until its document outgrows the staging buffer at least twice over.
///
/// Notes are what make a song document big, so this adds them through the model rather than by
/// writing XML by hand. The target is expressed in DOUBLINGS rather than bytes because the buffer's
/// growth is what the fixture exists to exercise; the byte figure that satisfies it will drift as
/// the song format changes and the doubling count will not.
void inflateSong() {
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip == nullptr) {
		printf("SS-SETUP FAIL there is no clip to add notes to\n");
		g_failures++;
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	const int32_t clipLength = clip->loopLength;
	if (clipLength <= 0) {
		printf("SS-SETUP FAIL the clip has no length to place notes in\n");
		g_failures++;
		return;
	}

	// Notes that overlap are MERGED rather than added, which would silently produce a smaller
	// document than intended -- so they are spaced, and the count is reported either way.
	const int32_t noteLength = 1;
	const int32_t step = clipLength / kNotesPerRow > 0 ? clipLength / kNotesPerRow : 1;

	for (int32_t y = 0; y < 8; y++) {
		ModelStackWithNoteRow* rowStack = clip->getOrCreateNoteRowForYNote(60 + y, modelStack, nullptr);
		if (rowStack == nullptr) {
			continue;
		}
		NoteRow* row = rowStack->getNoteRowAllowNull();
		if (row == nullptr) {
			continue;
		}
		for (int32_t n = 0; n < kNotesPerRow; n++) {
			const int32_t pos = (n * step) % clipLength;
			if (row->attemptNoteAdd(pos, noteLength, 64, row->getDefaultProbability(), row->getDefaultIterance(),
			                        row->getDefaultFill(rowStack), rowStack, nullptr)
			    != 0) {
				g_notesAdded++;
			}
		}
	}
	printf("SS-SETUP notes=%d\n", g_notesAdded);
}

/// Whether every machine a song save runs on has gone quiet.
///
/// Three of them: the sample collection, the staged document write, and the staged-save machine the
/// base class drives. Reading the card while any is still running asks a question the save has not
/// answered yet.
bool saveMachineryIdle() {
	return !saveSongUI.collectInFlight() && !saveSongUI.docInFlight() && !SaveUI::saveInFlight();
}

/// Start loading the saved document back through the real loader.
///
/// Bytes that look plausible are not the claim; a song is. A staged write that produced a broken
/// header or lost its closing tag would satisfy any comparison between two staged builds, because
/// both would be broken identically.
///
/// Starting and waiting are separate STAGES rather than one blocking helper. A load is driven by the
/// harness's tick pump -- the owner and the repeating tasks run BETWEEN ticks -- so a helper that
/// spun here could never see one finish, however long it waited.
bool beginSavedSongLoad(const char* path) {
	currentSong->setSongFullPath(path);
	if (!openUI(&loadSongUI)) {
		return false;
	}
	loadSongUI.beginLoad();
	return true;
}

void runTick() {
	switch (g_stage) {
	case Stage::Inflate: {
		(void)deluge::io::unlink(kSavePath);
		// The card image is keyed by the fixture's fingerprint and SHARED between runs, so anything a
		// previous run left behind is still here. A save that abandons leaves its temp file, which
		// makes the next run's temp-name search take more attempts and its step count drift -- so the
		// run starts by clearing them rather than by hoping.
		for (int32_t t = 0; t < 16; t++) {
			std::string temp = "SONGS/TEMP";
			temp.append(deluge::string::fromInt(t, 4));
			temp.append(writeJsonFlag ? ".Json" : ".XML");
			(void)deluge::io::unlink(temp);
		}
		inflateSong();
		g_stage = (g_failures == 0) ? Stage::Fixture : Stage::Report;
		break;
	}
	case Stage::Fixture: {
		// performSave() pushes the overwrite context menu with openUI(), which needs a root UI
		// beneath it -- this harness boots without one.
		changeRootUI(&arrangerView);
		(void)SongSaveChannelProbe::openOn(kSaveDir);
		g_stage = Stage::FixtureWait;
		break;
	}
	case Stage::FixtureWait: {
		if (!SongSaveChannelProbe::listingInProgress()) {
			g_stage = Stage::SaveFirst;
		}
		break;
	}
	case Stage::SaveFirst: {
		SongSaveChannelProbe::typeName(kSaveName);
		SongSaveChannelProbe::pressEnter();
		g_waitTicks = 0;
		g_stage = Stage::SaveFirstWait;
		break;
	}
	case Stage::SaveFirstWait: {
		if (saveMachineryIdle()) {
			g_stage = Stage::SaveFirstCheck;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("SS-SAVED FAIL the save never finished (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::SaveFirstCheck: {
		// SS-GREW first. If the fixture did not outgrow the buffer, every other check here is running
		// over a path this rung did not touch, and saying so first is what stops a green run being
		// mistaken for a meaningful one.
		const int32_t doublings = SaveSongUI::stagedDocumentGrowths();
		printf("SS-GREW doublings=%d\n", doublings);
		if (doublings < 2) {
			printf("SS-GREW FAIL the song did not outgrow the staging buffer twice; the growth path was"
			       " not exercised and the fixture needs to be bigger\n");
			g_failures++;
		}

		std::vector<uint8_t> bytes;
		if (!readWholeFile(kSavePath, bytes)) {
			printf("SS-SAVED FAIL the save wrote no file at %s\n", kSavePath);
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		g_crcFirst = crc32(bytes.data(), bytes.size());
		g_bytesFirst = bytes.size();
		printf("SS-SAVED bytes=%zu crc=%08X\n", g_bytesFirst, g_crcFirst);

		// SS-VERIFIED. The read-back check runs INSIDE the save, and a save that fails it abandons --
		// after having already written the file. So the file being here says nothing; only the save
		// reaching its end does. Reporting the real count rather than a fixed string is the
		// difference between a check and a decoration.
		const int32_t completed = SaveSongUI::documentSavesCompleted();
		printf("SS-VERIFIED completedSaves=%d\n", completed);
		if (completed != 1) {
			printf("SS-VERIFIED FAIL the save wrote a file but never completed; its read-back check"
			       " rejected what it had written\n");
			g_failures++;
		}

		if (!beginSavedSongLoad(kSavePath)) {
			printf("SS-PARSES FAIL the saved document could not even be opened for loading\n");
			g_failures++;
			g_stage = Stage::Reopen;
			break;
		}
		g_waitTicks = 0;
		g_stage = Stage::ParseWait;
		break;
	}
	case Stage::ParseWait: {
		if (!loadSongUI.isLoadingSong()) {
			g_stage = Stage::ParseCheck;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("SS-PARSES FAIL the saved document never finished loading (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Reopen;
		}
		break;
	}
	case Stage::ParseCheck: {
		printf("SS-PARSES ok\n");
		g_stage = Stage::Reopen;
		break;
	}
	case Stage::Reopen: {
		// A successful save ENDS in close(), which pops the browser off the UI stack, so a second
		// save has to reach the save screen again exactly as the user would.
		changeRootUI(&arrangerView);
		(void)SongSaveChannelProbe::openOn(kSaveDir);
		// Typing happens on the NEXT tick: the listing this open starts is async, and when it settles
		// it picks a brand-new unused name, which would silently aim the second save at a free slot.
		g_stage = Stage::SaveSecond;
		break;
	}
	case Stage::SaveSecond: {
		SongSaveChannelProbe::typeName(kSaveName);
		SongSaveChannelProbe::pressEnter();
		g_stage = Stage::SaveSecondCheck;
		break;
	}
	case Stage::SaveSecondCheck: {
		if (!SongSaveChannelProbe::overwritePromptOpen()) {
			printf("SS-INTACT FAIL saving onto an existing song did not prompt\n");
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		g_stage = Stage::Confirm;
		break;
	}
	case Stage::Confirm: {
		SongSaveChannelProbe::confirmOverwrite();
		g_waitTicks = 0;
		g_stage = Stage::ConfirmWait;
		break;
	}
	case Stage::ConfirmWait: {
		// Confirming STARTS the overwrite; it does not perform it.
		if (saveMachineryIdle()) {
			g_stage = Stage::ConfirmCheck;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("SS-INTACT FAIL the confirmed overwrite never finished (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::ConfirmCheck: {
		// SS-INTACT. Whatever the overwrite did, a song must still be here and must still load. When
		// the save succeeds this is the new one; when it fails partway it is the old one, untouched,
		// which is the property that justifies writing to a temp name and renaming.
		std::vector<uint8_t> bytes;
		if (!readWholeFile(kSavePath, bytes)) {
			printf("SS-INTACT FAIL nothing is left at %s after the overwrite\n", kSavePath);
			g_failures++;
		}
		else if (!beginSavedSongLoad(kSavePath)) {
			printf("SS-INTACT FAIL what is left at %s could not be opened for loading\n", kSavePath);
			g_failures++;
		}
		else {
			g_bytesFirst = bytes.size();
			g_waitTicks = 0;
			g_stage = Stage::IntactWait;
			break;
		}
		g_stage = Stage::Report;
		break;
	}
	case Stage::IntactWait: {
		if (!loadSongUI.isLoadingSong()) {
			g_stage = Stage::IntactCheck;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("SS-INTACT FAIL what is left at %s never finished loading (%d ticks)\n", kSavePath, g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::IntactCheck: {
		printf("SS-INTACT bytes=%zu\n", g_bytesFirst);
		g_stage = Stage::Report;
		break;
	}
	case Stage::Report: {
		// Both arms are reported, and the gate holds them to account for every card step between
		// them: either number alone reads as a success on a build where half the steps quietly took
		// the other arm.
		printf("SS-SUMMARY served=%d ownerServed=%d offOwnerTicks=%d steps=%d\n",
		       (int)SaveSongUI::documentChannelServedOps(), (int)SaveSongUI::documentOwnerServedOps(),
		       (int)SaveSongUI::offOwnerDocumentTicks(), (int)SaveSongUI::documentSteps());
		printf("SS-RESULT failures=%d\n", g_failures);
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

void deluge_scenario_song_save_channel_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Inflate;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_waitTicks = 0;
	g_notesAdded = 0;
	g_crcFirst = 0;
	g_bytesFirst = 0;
}

bool deluge_scenario_song_save_channel_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_song_save_channel_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// A marker op behind whatever the tick queued: the owner queue is FIFO, so this runs after it.
	// Setting g_stepDone directly races the owner and the scenario sails past work it just queued.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_song_save_channel_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_song_save_channel_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
