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

	/// Calls the shipped drive rather than restating it: `drivePreview()` is exactly what
	/// `Slicer::preview()` runs, and it already returns the "deferred" answer the specs assert on.
	/// Restating the request → attempt → is_current → work → complete sequence here would leave the
	/// latest-wins hand-off loop and the is_current gate inside it unexercised -- the "spec restates
	/// the caller" shape this whole spec file exists to avoid.
	void previewForTarget(const Slicer::PreviewTarget& target) { deferred_ = slicer_.drivePreview(target); }

	/// The shipped user-driven entry -- what a pad tap calls. Used where the spec needs a SECOND
	/// press to land while an attempt is already running, which is the one interleaving an inline
	/// drive has (see `deluge::test::duringNextLoadFile`).
	void press(const Slicer::PreviewTarget& target) {
		slicer_.preview(target.startPoint, target.endPoint, target.transpose, target.on);
	}

	/// The shipped cancel -- what leaving the slicer, or switching slicing mode, calls.
	void cancel() { slicer_.cancelPreview(); }

	void graphicsRoutine() { slicer_.graphicsRoutine(); }

	[[nodiscard]] bool deferred() const { return deferred_; }
	/// Whether a deferral is armed for the next `graphicsRoutine()` -- i.e. the re-ask loop is alive.
	[[nodiscard]] bool previewIsPending() const { return slicer_.previewLoad_.pending(); }
	[[nodiscard]] int32_t auditionNotesSent() const { return deluge::test::auditionNoteCount; }
	[[nodiscard]] int32_t lastAuditionOn() const { return deluge::test::lastAuditionOn; }
	/// How many storage-owner dispatches the auditions above made.
	[[nodiscard]] int32_t ownerRuns() const { return deluge::test::ownerRunCount; }

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

/// The audition's card read happens on the storage owner, not on the tier that pressed the pad.
///
/// This binary is built at the shipping default -- `deluge_audio_load_async_active()` false -- where
/// `previewForTarget()`'s `loadFile()` is a real blocking card read, and `preview()` is reached from
/// `padAction()` and `graphicsRoutine()`, both on the interaction tier. On the Rust BSP the efatfs
/// entry points HOLD the calling context for the whole transfer rather than yielding it, so a cold
/// audition run inline there stalls the UI outright.
///
/// Asserted rather than left to the arms looking alike: `Owner::run()` is INLINE on this host, so an
/// audition that ran the load on the caller's tier produces byte-identical results in every other
/// spec in this file. The dispatch count is the only thing that can tell them apart.
///
/// The residency argument -- the sliced sample is normally already loaded, so the load returns from
/// the in-memory search without touching the card -- is why this is rarely a cold read. It is not a
/// reason to skip the dispatch: it rests on `waveformBasicNavigator` holding a raw `Sample*` with no
/// reason held, which nothing enforces.
TEST(SlicerPreview, theAuditionsCardReadRunsOnTheStorageOwner) {
	SlicerPreviewProbe probe;
	CHECK_EQUAL(0, probe.ownerRuns());

	probe.press({.startPoint = 0, .endPoint = -1, .transpose = 0, .on = 1});

	CHECK_EQUAL(1, probe.auditionNotesSent());
	CHECK_EQUAL(1, probe.lastAuditionOn());
	CHECK_EQUAL(1, probe.ownerRuns());
}

/// A cold press defers, sounds nothing yet, and is re-driven by the tick surface -- at which point
/// the note lands. The load, the params and the note stay together; only WHEN they run moves.
TEST(SlicerPreview, aColdPressDefersThenSoundsOnTheNextTick) {
	SlicerPreviewProbe probe;
	probe.forceLoadMiss(true);
	probe.previewForTarget({.startPoint = 0, .endPoint = -1, .transpose = 0, .on = 1});
	CHECK(probe.deferred());
	CHECK_EQUAL(0, probe.auditionNotesSent());

	// Re-driven while the load STILL misses: the re-ask must ask the loader again, not walk past it.
	// The holder's filePath was assigned before the deferred load, so a guard that asks only "is this
	// a different sample?" answers no here and falls straight through to the audition note -- sounding
	// a slice of a sample that never arrived. previewForTarget() also asks whether the file actually
	// landed (`audioFile == nullptr`), which is what this pins.
	probe.graphicsRoutine();
	CHECK_EQUAL(0, probe.auditionNotesSent());
	CHECK(probe.previewIsPending());

	probe.forceLoadMiss(false);
	probe.graphicsRoutine();
	CHECK_EQUAL(1, probe.auditionNotesSent());
	CHECK_EQUAL(1, probe.lastAuditionOn());
}

namespace {
/// The probe the `duringNextLoadFile` hooks below re-enter. A file-scope pointer because the hook is
/// a plain function pointer with no capture -- which is what the mock's seam is, deliberately: a
/// capturing hook would need an allocation on a seam that models an interrupt-time arrival.
SlicerPreviewProbe* g_probe = nullptr;
} // namespace

/// A pad tap that lands WHILE an attempt is running is handed off inside the same drive, and
/// actually runs.
///
/// This is the loop in `drivePreview()`, and it is not decoration. `complete()` returning a target
/// keeps the single-flight guard HELD -- the caller is expected to run that target itself. A drive
/// that recursed into `drivePreview(*next)` instead of looping would call `request()` on a flight
/// that is still open, get `false`, and silently drop the tap. (That was the first draft of this
/// function, and nothing in the two specs above can tell the two shapes apart: neither ever has a
/// second target arrive mid-attempt, so `complete()` never returns one and the loop never iterates.)
///
/// The interleaving is the real one: the second press arrives inside `loadFile()`, which on the
/// shipping synchronous backing is where the drive blocks on the card and the fiber pump runs.
TEST(SlicerPreview, aTapLandingDuringAnAttemptIsHandedOffInsideTheSameDrive) {
	SlicerPreviewProbe probe;
	g_probe = &probe;
	// The finger lifts while the note-on's load is still running: a note-OFF supersedes the note-on.
	deluge::test::duringNextLoadFile = [] {
		g_probe->press({.startPoint = 0, .endPoint = -1, .transpose = 0, .on = 0});
	};

	probe.previewForTarget({.startPoint = 0, .endPoint = -1, .transpose = 0, .on = 1});

	CHECK_FALSE(probe.deferred());
	// BOTH sounded, in order: the note-on the drive was already committed to, then the note-off the
	// hand-off picked up. One note here means the superseding target was dropped -- on a device, a
	// note-on with no matching note-off, i.e. a stuck note.
	CHECK_EQUAL(2, probe.auditionNotesSent());
	CHECK_EQUAL(0, probe.lastAuditionOn());
	// And nothing is left armed: the hand-off ran to completion rather than parking a re-ask.
	CHECK_FALSE(probe.previewIsPending());
}

/// A cancel that lands while an attempt is running arms NO re-ask, and strands nothing.
///
/// `cancel()` deliberately leaves the single-flight guard held -- an op genuinely is still out there
/// -- and it is the stale attempt reporting back that releases it. Both halves are asserted, because
/// getting either wrong is silent: an armed re-ask sounds a slice into a slicer the user has left
/// (`graphicsRoutine()` keeps running the moment it is current again, and `slicer` is a file-scope
/// singleton), and a guard never released wedges every later audition with nothing on screen to say
/// why.
///
/// The cancel arrives inside `loadFile()` for the same reason the tap above does: that is the only
/// place an inline drive gives up control.
///
/// @note This does NOT reach `drivePreview()`'s own `is_current()` guard, and no spec can: that
///       check sits between `attempt()` and the work, two adjacent statements in a drive that never
///       yields between them, so nothing can bump the generation in that window. What refuses the
///       cancelled attempt here is `complete()`'s `deferred && is_current(a)`, one line later.
TEST(SlicerPreview, aCancelLandingDuringAnAttemptArmsNoReAskAndStrandsNothing) {
	SlicerPreviewProbe probe;
	g_probe = &probe;
	probe.forceLoadMiss(true); // the press defers, so there IS something a re-ask could be armed for
	deluge::test::duringNextLoadFile = [] { g_probe->cancel(); };

	probe.previewForTarget({.startPoint = 0, .endPoint = -1, .transpose = 0, .on = 1});

	CHECK(probe.deferred());
	CHECK_EQUAL(0, probe.auditionNotesSent());
	CHECK_FALSE(probe.previewIsPending());

	// The tick surface has nothing to re-drive, and must not invent one.
	probe.graphicsRoutine();
	CHECK_EQUAL(0, probe.auditionNotesSent());

	// And the next press works: the guard the cancel left held was released by the stale attempt
	// reporting back, so this is not wedged.
	probe.forceLoadMiss(false);
	probe.press({.startPoint = 0, .endPoint = -1, .transpose = 0, .on = 1});
	CHECK_EQUAL(1, probe.auditionNotesSent());
	CHECK_EQUAL(1, probe.lastAuditionOn());
}
