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

#include "harness/pattern_load_scenario.h"

#include "definitions_cxx.hpp"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_pattern_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "io/file.hpp"
#include "libdeluge/tier.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "storage/storage_manager.h"
#include "util/string.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded `printf_`. That
// implementation emits through `putchar_`, which only the C host BSP defines -- this harness links the Rust BSP and has
// no provider, so the alias would turn every line this file writes into an undefined symbol at link time.
#ifdef printf
#undef printf
#endif

namespace {

constexpr const char* kFolder = "PATTERNS/MELODIC";
constexpr const char* kName = "PT_TEST.XML";
constexpr const char* kPath = "PATTERNS/MELODIC/PT_TEST.XML";
/// The rows the fixture writes notes on, as display rows of the clip: display rows are in the clip's scale by
/// construction, so the rows survive whatever scale the boot song is in.
constexpr int32_t kRows[] = {0, 2, 4};
/// The squares each row gets a note on. The pattern writer copies only what is on screen, so every note is on it.
constexpr int32_t kSquares[] = {0, 4, 8, 12};
constexpr int32_t kVelocity = 100;
/// How many ticks a leg is given to land its notes. A converted load stages the file over a handful of ticks; this is
/// far past that, and short enough that a load which never lands fails in seconds.
constexpr int32_t kLegTicks = 4000;

enum class Stage : uint8_t {
	Setup,
	OpenForLoad,
	ListForLoad,
	PressEnter,
	AwaitLoad,
	ClearForPreview,
	OpenForPreview,
	ListForPreview,
	PressPlay,
	AwaitPreview,
	CloseBrowser,
	Report,
	Done,
};

Stage g_stage = Stage::Setup;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_ticks = 0;
InstrumentClip* g_clip = nullptr;
/// The notes the fixture wrote, as "yNote:pos:length:velocity" in row then position order.
std::vector<std::string> g_expected;
/// The meter's per-entry counts when the first leg began: every synchronous call after it is the code under test's.
std::vector<uint32_t> g_entryBefore;

bool fail(const char* what) {
	printf("PT-FAIL %s\n", what);
	g_failures++;
	return false;
}

/// Every note in the clip, as "yNote:pos:length:velocity" in row then position order.
std::vector<std::string> notesOf(InstrumentClip* clip) {
	std::vector<std::string> out;
	for (NoteRow& row : clip->noteRows) {
		for (Note& note : row.notes) {
			out.push_back(std::to_string(row.y) + ":" + std::to_string(note.pos) + ":" + std::to_string(note.length)
			              + ":" + std::to_string(note.getVelocity()));
		}
	}
	return out;
}

/// Put the fixture's notes on the clip, through the note-add entry point a pad press uses.
bool addFixtureNotes() {
	const int32_t length = instrumentClipView.getPosFromSquare(1) - instrumentClipView.getPosFromSquare(0);
	if (length <= 0) {
		return fail("the clip view shows no time: a square is zero ticks wide");
	}
	for (int32_t yDisplay : kRows) {
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* ms = currentSong->setupModelStackWithCurrentClip(memory);
		const int32_t yNote = g_clip->getYNoteFromYDisplay(yDisplay, currentSong);
		ModelStackWithNoteRow* rowStack = g_clip->getOrCreateNoteRowForYNote(yNote, ms, nullptr);
		NoteRow* row = (rowStack != nullptr) ? rowStack->getNoteRowAllowNull() : nullptr;
		if (row == nullptr) {
			return fail("no note row for a fixture note");
		}
		for (int32_t square : kSquares) {
			(void)row->attemptNoteAdd(instrumentClipView.getPosFromSquare(square), length, kVelocity,
			                          row->getDefaultProbability(), row->getDefaultIterance(),
			                          row->getDefaultFill(rowStack), rowStack, nullptr);
		}
	}
	return true;
}

/// Write the clip's on-screen notes to kPath with the pattern writer the save browser uses.
bool writePatternFile() {
	(void)deluge::io::mkdir("PATTERNS");
	(void)deluge::io::mkdir(kFolder);
	(void)deluge::io::unlink(kPath);
	Error error = StorageManager::createXMLFile(kPath, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return fail("the pattern file could not be created");
	}
	instrumentClipView.copyNotesToFile(GetSerializer());
	error = GetSerializer().closeFileAfterWriting(kPath, nullptr, nullptr);
	return (error == Error::NONE) || fail("the pattern file could not be written");
}

/// Remove every note from the clip, as the clip's CLEAR gesture does. `patternClear()` is not used: it clears the
/// clip with `clearSequenceAndMPE` false, which leaves the notes where they are.
void clearNotes() {
	char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* ms = currentSong->setupModelStackWithCurrentClip(memory);
	g_clip->clear(nullptr, ms, /*clearAutomation=*/false, /*clearSequenceAndMPE=*/true);
}

/// Snapshot the meter's per-entry counts.
void snapshotTier() {
	g_entryBefore.clear();
	for (uint8_t e = 0; e < deluge_tier_sync_entries(); ++e) {
		g_entryBefore.push_back(deluge_tier_sync_count(e));
	}
}

/// Open the pattern browser on the clip, as the clip view's LOAD gesture does.
bool openBrowser(const char* leg) {
	currentUIMode = UI_MODE_NONE;
	const bool opened = openUI(&loadPatternUI);
	if (opened) {
		loadPatternUI.setupLoadPatternUI();
	}
	printf("PT-OPEN leg=%s opened=%d browserOnTop=%d\n", leg, opened ? 1 : 0, getCurrentUI() == &loadPatternUI ? 1 : 0);
	return opened || fail("the pattern browser could not be opened");
}

/// Tick the browser while its listing runs, as its graphics routine is ticked on the device. True once it has finished.
bool listingFinished() {
	if (Browser::isListingInProgress()) {
		getCurrentUI()->graphicsRoutine();
		return false;
	}
	return true;
}

/// Whether the listing left the fixture selected.
bool fixtureSelected(const char* leg) {
	FileItem* item = Browser::getCurrentFileItem();
	const std::string name = (item != nullptr) ? std::string{item->displayName()} : std::string{"-"};
	const bool selected = (item != nullptr) && !item->isFolder && deluge::string::caselessEquals(name, kName);
	printf("PT-LISTED leg=%s selected=%d name=%s\n", leg, selected ? 1 : 0, name.c_str());
	return selected || fail("the listing did not select the fixture");
}

/// Tick whatever the open UI ticks, and say whether the clip's notes are the fixture's.
bool legLanded() {
	if (getCurrentUI() == &loadPatternUI) {
		loadPatternUI.graphicsRoutine();
	}
	return notesOf(g_clip) == g_expected;
}

void reportLeg(const char* marker, bool landed) {
	const std::vector<std::string> notes = notesOf(g_clip);
	printf("%s ok=%d notes=%d want=%d ticks=%d browserOnTop=%d\n", marker, landed ? 1 : 0,
	       static_cast<int>(notes.size()), static_cast<int>(g_expected.size()), g_ticks,
	       getCurrentUI() == &loadPatternUI ? 1 : 0);
	if (!landed) {
		fail(std::strcmp(marker, "PT-LOAD") == 0 ? "Enter did not load the pattern's notes"
		                                         : "PLAY did not preview the pattern's notes");
	}
}

/// Whether @p name is a meter entry that reaches the card. `dir_read` and `dir_close` do not: they walk, and then drop,
/// the in-memory snapshot the directory channel's open already took, so a listing's enumeration is not card I/O.
bool touchesCard(const char* name) {
	return std::strcmp(name, "dir_read") != 0 && std::strcmp(name, "dir_close") != 0;
}

void report() {
	uint32_t total = 0;
	for (uint8_t e = 0; e < deluge_tier_sync_entries(); ++e) {
		const uint32_t n = deluge_tier_sync_count(e) - ((e < g_entryBefore.size()) ? g_entryBefore[e] : 0);
		if (n != 0) {
			printf("PT-TIER-AT entry=%s n=%u card=%d\n", deluge_tier_sync_name(e), n,
			       touchesCard(deluge_tier_sync_name(e)) ? 1 : 0);
		}
		if (touchesCard(deluge_tier_sync_name(e))) {
			total += n;
		}
	}
	printf("PT-TIER sync=%u\n", total);
	if (total != 0) {
		fail("the pattern legs made synchronous card calls on the interaction tier");
	}
	printf("PT-RESULT failures=%d\n", static_cast<int>(g_failures));
	g_summaryPrinted = true;
}

void runTick() {
	using namespace deluge::hid::button;
	switch (g_stage) {
	case Stage::Setup: {
		Clip* first = (std::ssize(currentSong->sessionClips) > 0) ? currentSong->sessionClips[0] : nullptr;
		if (first == nullptr || first->type != ClipType::INSTRUMENT || first->output == nullptr
		    || first->output->type != OutputType::SYNTH) {
			fail("the boot song has no synth clip");
			g_stage = Stage::Report;
			break;
		}
		g_clip = static_cast<InstrumentClip*>(first);
		currentSong->setCurrentClip(g_clip);
		changeRootUI(&instrumentClipView);
		if (!addFixtureNotes() || !writePatternFile()) {
			g_stage = Stage::Report;
			break;
		}
		g_expected = notesOf(g_clip);
		clearNotes();
		const size_t cleared = notesOf(g_clip).size();
		printf("PT-SETUP notes=%d cleared=%d file=%s\n", static_cast<int>(g_expected.size()), static_cast<int>(cleared),
		       kPath);
		if (g_expected.size() != std::size(kRows) * std::size(kSquares) || cleared != 0) {
			fail("the fixture did not write, then clear, the notes it meant to");
			g_stage = Stage::Report;
			break;
		}
		snapshotTier();
		g_stage = Stage::OpenForLoad;
		break;
	}

	case Stage::OpenForLoad:
		g_stage = openBrowser("load") ? Stage::ListForLoad : Stage::Report;
		break;

	case Stage::ListForLoad:
		if (listingFinished()) {
			g_stage = fixtureSelected("load") ? Stage::PressEnter : Stage::Report;
		}
		break;

	case Stage::PressEnter:
		(void)loadPatternUI.buttonAction(SELECT_ENC, true, false);
		(void)loadPatternUI.buttonAction(SELECT_ENC, false, false);
		g_ticks = 0;
		g_stage = Stage::AwaitLoad;
		break;

	case Stage::AwaitLoad: {
		const bool landed = legLanded();
		if (landed || ++g_ticks >= kLegTicks) {
			reportLeg("PT-LOAD", landed);
			g_stage = Stage::ClearForPreview;
		}
		break;
	}

	case Stage::ClearForPreview:
		if (getCurrentUI() == &loadPatternUI) {
			loadPatternUI.close();
		}
		clearNotes();
		if (!notesOf(g_clip).empty()) {
			fail("the notes could not be cleared before the preview leg");
			g_stage = Stage::Report;
			break;
		}
		g_stage = Stage::OpenForPreview;
		break;

	case Stage::OpenForPreview:
		g_stage = openBrowser("preview") ? Stage::ListForPreview : Stage::Report;
		break;

	case Stage::ListForPreview:
		if (listingFinished()) {
			g_stage = fixtureSelected("preview") ? Stage::PressPlay : Stage::Report;
		}
		break;

	case Stage::PressPlay:
		// Press and release: the press previews, and each edge toggles playback, so the pair leaves it stopped.
		(void)loadPatternUI.buttonAction(PLAY, true, false);
		(void)loadPatternUI.buttonAction(PLAY, false, false);
		g_ticks = 0;
		g_stage = Stage::AwaitPreview;
		break;

	case Stage::AwaitPreview: {
		const bool landed = legLanded();
		if (landed || ++g_ticks >= kLegTicks) {
			reportLeg("PT-PREVIEW", landed);
			g_stage = Stage::CloseBrowser;
		}
		break;
	}

	case Stage::CloseBrowser:
		if (playbackHandler.playbackState) {
			playbackHandler.endPlayback();
		}
		if (getCurrentUI() == &loadPatternUI) {
			loadPatternUI.close();
		}
		g_stage = Stage::Report;
		break;

	case Stage::Report:
		report();
		g_stage = Stage::Done;
		break;

	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_pattern_load_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Setup;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_ticks = 0;
	g_clip = nullptr;
	g_expected.clear();
	g_entryBefore.clear();
}

bool deluge_scenario_pattern_load_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_pattern_load_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	g_stepDone = true;
}

bool deluge_scenario_pattern_load_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_pattern_load_failures(void) {
	return g_failures;
}

} // extern "C"
