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

#include "scenarios/pattern_load_scenario.h"

#include "definitions_cxx.hpp"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_pattern_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "io/file.hpp"
#include "libdeluge/file_bytes.h" // deluge_file_bytes_set_latency_ms
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
// implementation emits through `putchar_`, which the Rust BSP builds do not define -- this harness links the Rust BSP
// and has no provider, so the alias would turn every line this file writes into an undefined symbol at link time.
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
/// How long the byte channel holds each operation in the legs that act over a read the channel holds, in ms.
constexpr uint32_t kHoldMs = 20;

/// The legs, in order. Each starts from a cleared clip and a freshly opened browser.
enum class Leg : uint8_t {
	Load,      ///< Enter: the browser closes at once, and the notes land after it.
	Preview,   ///< PLAY: the notes land with the browser still open.
	Discard,   ///< PLAY, then BACK at once: the preview is discarded and lands nothing.
	Supersede, ///< PLAY, then Enter at once: Enter's load replaces the preview and lands after the browser closes.
	Count,
};

enum class Stage : uint8_t {
	Setup,
	Clear,
	Open,
	List,
	Act,
	Hold,
	Await,
	Report,
	Done,
};

Stage g_stage = Stage::Setup;
Leg g_leg = Leg::Load;
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

const char* legName(Leg leg) {
	switch (leg) {
	case Leg::Load:
		return "load";
	case Leg::Preview:
		return "preview";
	case Leg::Discard:
		return "discard";
	case Leg::Supersede:
		return "supersede";
	default:
		return "?";
	}
}

/// Tick the browser's graphics routine while it is open. The pattern read runs on its own job, which the app loop runs
/// when the read is asked for and when the channel answers.
void tickLoad() {
	if (getCurrentUI() == &loadPatternUI) {
		loadPatternUI.graphicsRoutine();
	}
}

/// Whether the leg has finished: no pattern read left in flight.
bool legSettled() {
	return !loadPatternUI.loadInFlight();
}

void reportLeg() {
	const std::vector<std::string> notes = notesOf(g_clip);
	const bool wantNotes = (g_leg != Leg::Discard);
	const bool landed = wantNotes ? (notes == g_expected) : notes.empty();
	const bool browserOnTop = getCurrentUI() == &loadPatternUI;
	// Enter closes the browser; a preview leaves it open.
	const bool browserRight = browserOnTop == (g_leg == Leg::Preview);
	const bool ok = landed && legSettled() && browserRight;
	printf("PT-%s ok=%d notes=%d want=%d ticks=%d browserOnTop=%d inFlight=%d\n",
	       g_leg == Leg::Load      ? "LOAD"
	       : g_leg == Leg::Preview ? "PREVIEW"
	       : g_leg == Leg::Discard ? "DISCARD"
	                               : "SUPERSEDE",
	       ok ? 1 : 0, static_cast<int>(notes.size()), wantNotes ? static_cast<int>(g_expected.size()) : 0, g_ticks,
	       browserOnTop ? 1 : 0, loadPatternUI.loadInFlight() ? 1 : 0);
	if (!ok) {
		switch (g_leg) {
		case Leg::Load:
			fail("Enter did not load the pattern's notes after closing the browser");
			break;
		case Leg::Preview:
			fail("PLAY did not preview the pattern's notes");
			break;
		case Leg::Discard:
			fail("a preview discarded by BACK still pasted");
			break;
		default:
			fail("Enter over a preview in flight did not land its own load");
			break;
		}
	}
}

/// The press that lands on a preview whose read the channel holds: BACK discards it, Enter replaces it.
void actOverHeldRead() {
	using namespace deluge::hid::button;
	if (g_leg == Leg::Discard) {
		(void)loadPatternUI.buttonAction(BACK, true);
	}
	else {
		(void)loadPatternUI.buttonAction(SELECT_ENC, true);
		(void)loadPatternUI.buttonAction(SELECT_ENC, false);
	}
}

/// Press the leg's buttons, as a user would, against the listing's selection.
void act() {
	using namespace deluge::hid::button;
	switch (g_leg) {
	case Leg::Load:
		(void)loadPatternUI.buttonAction(SELECT_ENC, true);
		(void)loadPatternUI.buttonAction(SELECT_ENC, false);
		break;
	case Leg::Preview:
		// Press and release: the press previews, and each edge toggles playback, so the pair leaves it stopped.
		(void)loadPatternUI.buttonAction(PLAY, true);
		(void)loadPatternUI.buttonAction(PLAY, false);
		break;
	case Leg::Discard:
	case Leg::Supersede:
		// The preview first; the second press waits in Stage::Hold until the channel holds the preview's buffer.
		(void)loadPatternUI.buttonAction(PLAY, true);
		(void)loadPatternUI.buttonAction(PLAY, false);
		break;
	default:
		break;
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
		g_leg = Leg::Load;
		g_stage = Stage::Open;
		break;
	}

	case Stage::Clear:
		if (playbackHandler.playbackState) {
			playbackHandler.endPlayback();
		}
		if (getCurrentUI() == &loadPatternUI) {
			loadPatternUI.close();
		}
		clearNotes();
		if (!notesOf(g_clip).empty()) {
			fail("the notes could not be cleared between legs");
			g_stage = Stage::Report;
			break;
		}
		g_stage = Stage::Open;
		break;

	case Stage::Open:
		g_stage = openBrowser(legName(g_leg)) ? Stage::List : Stage::Report;
		break;

	case Stage::List:
		if (listingFinished()) {
			g_stage = fixtureSelected(legName(g_leg)) ? Stage::Act : Stage::Report;
		}
		break;

	case Stage::Act:
		if (g_leg == Leg::Discard || g_leg == Leg::Supersede) {
			// The read runs when its channel answers, so with no hold it can land before the next tick looks for it.
			deluge_file_bytes_set_latency_ms(kHoldMs);
		}
		act();
		g_ticks = 0;
		g_stage = (g_leg == Leg::Discard || g_leg == Leg::Supersede) ? Stage::Hold : Stage::Await;
		break;

	case Stage::Hold:
		// Ticked until the preview's read is issued, so the second press has to wait a held buffer out rather than
		// drop a read the channel has not started.
		tickLoad();
		if (loadPatternUI.readIssued()) {
			printf("PT-HELD leg=%s ticks=%d\n", legName(g_leg), g_ticks);
			actOverHeldRead();
			deluge_file_bytes_set_latency_ms(0);
			g_ticks = 0;
			g_stage = Stage::Await;
		}
		else if (++g_ticks >= kLegTicks || !loadPatternUI.loadInFlight()) {
			deluge_file_bytes_set_latency_ms(0);
			fail("the preview's read was never held by the channel");
			g_stage = Stage::Report;
		}
		break;

	case Stage::Await:
		tickLoad();
		if (legSettled() || ++g_ticks >= kLegTicks) {
			reportLeg();
			g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
			g_stage = (g_leg == Leg::Count) ? Stage::Report : Stage::Clear;
		}
		break;

	case Stage::Report:
		if (playbackHandler.playbackState) {
			playbackHandler.endPlayback();
		}
		if (getCurrentUI() == &loadPatternUI) {
			loadPatternUI.close();
		}
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
	g_leg = Leg::Load;
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
