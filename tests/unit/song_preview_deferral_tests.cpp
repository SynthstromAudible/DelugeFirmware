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
#include "storage/deferred_load.h"

#include <string>

TEST_GROUP(SongPreviewDeferral){};

namespace {
/// The shape of `LoadSongUI::PreviewTarget`, kept local because that type is private to a UI class
/// this binary does not link. Only the two fields the deferral machinery moves around matter here.
struct Target {
	std::string path;
	bool toStore;
};
} // namespace

/// A preview that defers is armed for the per-tick surface, and the surface re-asks. Rung 1 shipped
/// this broken for the sample browser: the A/B gate drove its own retry loop, so the gate's driver
/// WAS the handler the real caller lacked. This test drives only what the UI drives.
TEST(SongPreviewDeferral, aDeferredPreviewIsReAskedByTheTickSurface) {
	deluge::storage::DeferredLoad<Target> load;
	const Target a{"SONGS/A.XML", true};
	CHECK(load.request(a));
	const auto attempt = load.attempt();
	CHECK(load.is_current(attempt));
	CHECK_FALSE(load.complete(attempt, /*deferred=*/true).has_value());

	const auto pending = load.take_pending();
	CHECK(pending.has_value());
	STRCMP_EQUAL("SONGS/A.XML", pending->path.c_str());
}

/// Leaving the UI cancels: a result landing afterwards must not paint over whatever replaced it.
TEST(SongPreviewDeferral, aCancelledPreviewIsNotCurrentAndArmsNothing) {
	deluge::storage::DeferredLoad<Target> load;
	CHECK(load.request(Target{"SONGS/A.XML", true}));
	const auto attempt = load.attempt();
	load.cancel();
	CHECK_FALSE(load.is_current(attempt));
	CHECK_FALSE(load.complete(attempt, /*deferred=*/true).has_value());
	CHECK_FALSE(load.take_pending().has_value());
}

/// The re-ask carries the ORIGINAL `toStore`, not a fresh guess at it.
///
/// `toStore` decides whether the decode lands in `PadLEDs::imageStore` (behind a scroll-in that has
/// not started yet) or straight into the live `PadLEDs::image`. The tick surface that re-asks knows
/// nothing about the scroll the cursor move set up, so if the target did not carry the flag the
/// re-drive would paint the live image while a scroll-in was still waiting on an unfilled store --
/// the first-two-columns-are-the-previous-song failure, arriving a few ticks later instead.
TEST(SongPreviewDeferral, aReAskedPreviewKeepsTheTargetItWasDispatchedWith) {
	deluge::storage::DeferredLoad<Target> load;
	CHECK(load.request(Target{"SONGS/A.XML", /*toStore=*/false}));
	const auto attempt = load.attempt();
	CHECK_FALSE(load.complete(attempt, /*deferred=*/true).has_value());

	const auto pending = load.take_pending();
	CHECK(pending.has_value());
	CHECK_FALSE(pending->toStore);
}

/// A cursor move mid-deferral supersedes the deferral rather than joining it. Without this the
/// browser would draw the song the cursor has LEFT once its prefix landed, then draw the current one
/// -- visible as artwork that flashes the wrong song on a fast scroll.
TEST(SongPreviewDeferral, aNewerTargetSupersedesADeferredOne) {
	deluge::storage::DeferredLoad<Target> load;
	CHECK(load.request(Target{"SONGS/A.XML", true}));
	const auto first = load.attempt();
	CHECK_FALSE(load.complete(first, /*deferred=*/true).has_value());

	// The cursor moves: request() drops the armed re-ask for A and makes B current.
	CHECK(load.request(Target{"SONGS/B.XML", true}));
	CHECK_FALSE(load.pending());

	const auto second = load.attempt();
	STRCMP_EQUAL("SONGS/B.XML", second.target.path.c_str());
	CHECK_FALSE(load.complete(second, /*deferred=*/false).has_value());
	CHECK_FALSE(load.pending());
}
