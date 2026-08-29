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

#include "gui/ui/slicer.h"
#include "slicer_env_mock.h"

/// Drives the real Slicer through the real entry points, with two seams: a forced load miss, and a
/// count of audition notes. Nothing here reimplements slicer logic -- if it did, the test would pass
/// against a UI that never works. In particular `graphicsRoutine()` is the shipped tick surface, so
/// a Slicer that never re-asks fails `aColdPressDefersThenSoundsOnTheNextTick` rather than being
/// rescued by a retry loop the test brought with it.
class SlicerPreviewProbe {
public:
	SlicerPreviewProbe() { deluge::test::resetSlicerEnv(); }

	void forceLoadMiss(bool on) { deluge::test::forceLoadInProgress = on; }

	/// Stands in for `Slicer::preview()`'s dispatch: the same request → attempt → is_current → drive
	/// → complete sequence, so the spec can see whether the attempt deferred.
	void previewForTarget(const Slicer::PreviewTarget& target) {
		CHECK(slicer_.previewLoad_.request(target));
		const auto attempt = slicer_.previewLoad_.attempt();
		deferred_ = slicer_.previewLoad_.is_current(attempt) && slicer_.previewForTarget(attempt);
		(void)slicer_.previewLoad_.complete(attempt, deferred_);
	}

	void graphicsRoutine() { slicer_.graphicsRoutine(); }

	[[nodiscard]] bool deferred() const { return deferred_; }
	[[nodiscard]] int32_t auditionNotesSent() const { return deluge::test::auditionNoteCount; }
	[[nodiscard]] int32_t lastAuditionOn() const { return deluge::test::lastAuditionOn; }

private:
	Slicer slicer_{};
	bool deferred_{false};
};

TEST_GROUP(SlicerPreview){};

/// A note-OFF does no I/O and therefore can never defer: the load sits inside `if (target.on)` while
/// sendAuditionNote is outside it. This is asserted rather than left as a reading of the control flow,
/// because hoisting the note into the branch would silently create a stuck-note bug -- finger up
/// before the load lands, note-off swallowed, note sounding forever.
TEST(SlicerPreview, aReleaseNeverDefersAndAlwaysSoundsItsNoteOff) {
	SlicerPreviewProbe probe;
	probe.forceLoadMiss(true); // every load answers LOAD_IN_PROGRESS
	probe.previewForTarget({.startPoint = 0, .endPoint = -1, .transpose = 0, .on = 0});
	CHECK_FALSE(probe.deferred());
	CHECK_EQUAL(1, probe.auditionNotesSent());
	CHECK_EQUAL(0, probe.lastAuditionOn());
}

/// A cold press defers, sounds nothing yet, and is re-driven by the tick surface -- at which point
/// the note lands. The load, the params and the note stay together; only WHEN they run moves.
TEST(SlicerPreview, aColdPressDefersThenSoundsOnTheNextTick) {
	SlicerPreviewProbe probe;
	probe.forceLoadMiss(true);
	probe.previewForTarget({.startPoint = 0, .endPoint = -1, .transpose = 0, .on = 1});
	CHECK(probe.deferred());
	CHECK_EQUAL(0, probe.auditionNotesSent());

	probe.forceLoadMiss(false);
	probe.graphicsRoutine();
	CHECK_EQUAL(1, probe.auditionNotesSent());
	CHECK_EQUAL(1, probe.lastAuditionOn());
}
