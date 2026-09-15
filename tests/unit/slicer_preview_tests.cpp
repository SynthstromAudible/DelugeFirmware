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

#include "CppUTest/TestHarness.h"

#include "display_mock.h"
#include "gui/ui/slicer.h"
#include "slicer_env_mock.h"

using deluge::test::LoadAnswer;

/// Drives the real Slicer through its real entry points -- a pad's press or release (`preview()`), the tick surface
/// (`graphicsRoutine()`) and the cancel -- with two seams: how each tick of the async sample load answers, and a count
/// of audition notes. Nothing here reimplements slicer logic -- if it did, the test would pass against a UI that never
/// works. In particular `graphicsRoutine()` is the shipped tick surface, so a Slicer that never ticks its held load
/// fails `aColdPressDefersThenSoundsOnTheNextTick` rather than being rescued by a loop the test brought with it.
class SlicerPreviewProbe {
public:
	SlicerPreviewProbe() {
		deluge::test::resetSlicerEnv();
		deluge::test::lastDisplayedError = Error::NONE;
	}

	/// How every tick of a held load answers from now on.
	void answer(LoadAnswer answer) { deluge::test::loadAnswer = answer; }

	/// The shipped pad entry, as `padAction()` calls it for a held slice pad.
	void press() { slicer_.preview(0, -1, 0, 1); }
	/// The shipped pad entry, as `padAction()` calls it when the pad is lifted.
	void release() { slicer_.preview(0, 0, 0, 0); }

	/// The shipped cancel: what leaving the slicer, switching its mode or committing a slice calls.
	void cancel() { slicer_.cancelPreview(); }

	void graphicsRoutine() { slicer_.graphicsRoutine(); }

	/// Whether a load is held for the tick surface to finish.
	[[nodiscard]] bool previewIsPending() const { return slicer_.auditionLoad_.pending(); }
	[[nodiscard]] int32_t auditionNotesSent() const { return deluge::test::auditionNoteCount; }
	[[nodiscard]] int32_t lastAuditionOn() const { return deluge::test::lastAuditionOn; }
	[[nodiscard]] int32_t previewGiveUps() const { return slicer_.previewGiveUps(); }

private:
	Slicer slicer_{};
};

TEST_GROUP(SlicerPreview){};

/// A release loads nothing and therefore never waits: its note-off is sent at once, whatever a load is doing. This is
/// asserted rather than left as a reading of the control flow, because a release that waited for a load would leave a
/// note sounding after the finger lifted.
TEST(SlicerPreview, aReleaseNeverDefersAndAlwaysSoundsItsNoteOff) {
	SlicerPreviewProbe probe;
	probe.answer(LoadAnswer::Loading); // every load is still loading
	probe.release();
	CHECK_FALSE(probe.previewIsPending());
	CHECK_EQUAL(1, probe.auditionNotesSent());
	CHECK_EQUAL(0, probe.lastAuditionOn());
}

/// A cold press holds its load and sounds nothing yet; the tick surface finishes it, and the note lands with the load.
/// The load, the params and the note stay together; only WHEN they run moves.
TEST(SlicerPreview, aColdPressDefersThenSoundsOnTheNextTick) {
	SlicerPreviewProbe probe;
	probe.answer(LoadAnswer::Loading);
	probe.press();
	CHECK(probe.previewIsPending());
	CHECK_EQUAL(0, probe.auditionNotesSent());

	// Ticked while the load is STILL loading: the holder already names the file, so a slicer that asked only "is this a
	// different sample?" would sound a slice of a sample that never arrived.
	probe.graphicsRoutine();
	CHECK_EQUAL(0, probe.auditionNotesSent());
	CHECK(probe.previewIsPending());

	probe.answer(LoadAnswer::Loaded);
	probe.graphicsRoutine();
	CHECK_EQUAL(1, probe.auditionNotesSent());
	CHECK_EQUAL(1, probe.lastAuditionOn());
	CHECK_FALSE(probe.previewIsPending());
}

/// A load that stops moving gives up once, rather than being asked for on every tick for ever.
///
/// When to give up is the async sample load's own stall bound (600 walks, pinned by sample-load-tree's stall leg); here
/// the load simply answers Failed/Stalled. At the give-up nothing is left held, no audition note is sent, the card
/// error is shown and the give-up is counted. The next press asks again, so a card that does answer later still
/// auditions.
TEST(SlicerPreview, aStalledLoadGivesUpOnce) {
	SlicerPreviewProbe probe;
	probe.answer(LoadAnswer::Loading);
	probe.press();
	CHECK(probe.previewIsPending());

	probe.answer(LoadAnswer::Stalled);
	probe.graphicsRoutine();
	CHECK_FALSE(probe.previewIsPending());
	CHECK_EQUAL(1, probe.previewGiveUps());
	CHECK_EQUAL(0, probe.auditionNotesSent());
	CHECK(deluge::test::lastDisplayedError == Error::SD_CARD);

	probe.graphicsRoutine(); // nothing left to tick
	CHECK_EQUAL(0, probe.auditionNotesSent());
	CHECK_EQUAL(1, probe.previewGiveUps());

	probe.answer(LoadAnswer::Loaded);
	probe.press();
	probe.graphicsRoutine();
	CHECK_EQUAL(1, probe.auditionNotesSent());
	CHECK_EQUAL(1, probe.lastAuditionOn());
}

/// A finger lifted before its press's load lands sends its note-off, and the note-on never sounds.
///
/// The release cancels the held load. Without that, the load would land on a later tick and sound the note-on after
/// the note-off -- on a device, a note sounding with no finger on the pad, and no note-off left to stop it.
TEST(SlicerPreview, aReleaseBeforeTheLoadLandsNeverSoundsItsNoteOn) {
	SlicerPreviewProbe probe;
	probe.answer(LoadAnswer::Loading);
	probe.press();
	probe.release();
	CHECK_EQUAL(1, probe.auditionNotesSent());
	CHECK_EQUAL(0, probe.lastAuditionOn());

	// The notes first: a held load that lands sounds a note-on here, which is the damage. Nothing held is how.
	probe.answer(LoadAnswer::Loaded);
	probe.graphicsRoutine();
	CHECK_EQUAL(1, probe.auditionNotesSent());
	CHECK_EQUAL(0, probe.lastAuditionOn());
	CHECK_FALSE(probe.previewIsPending());
}

/// A cancel while a press's load is held sounds nothing, and leaves nothing that would wedge the next press.
///
/// A held load that survived the cancel would sound a slice into a slicer the user has left: `slicer` is a file-scope
/// singleton, and `graphicsRoutine()` ticks again the moment it is current.
TEST(SlicerPreview, aCancelWhileLoadingSoundsNothingAndStrandsNothing) {
	SlicerPreviewProbe probe;
	probe.answer(LoadAnswer::Loading);
	probe.press();
	probe.cancel();
	CHECK_FALSE(probe.previewIsPending());

	probe.answer(LoadAnswer::Loaded);
	probe.graphicsRoutine();
	CHECK_EQUAL(0, probe.auditionNotesSent());

	probe.press();
	probe.graphicsRoutine();
	CHECK_EQUAL(1, probe.auditionNotesSent());
	CHECK_EQUAL(1, probe.lastAuditionOn());
}
