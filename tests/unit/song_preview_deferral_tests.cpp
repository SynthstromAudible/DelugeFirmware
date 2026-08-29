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

#include <cstdio>
#include <cstring>
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

// ── The parked scroll direction ─────────────────────────────────────────────

namespace {

/// Read a repository source file. ctest runs from the repo root (`song_preview_decode_tests.cpp`
/// reads `tests/data/...` the same way), so relative paths resolve.
std::string readSource(const char* path) {
	std::FILE* f = std::fopen(path, "rb");
	CHECK_TEXT(f != nullptr, path);
	std::string text;
	char chunk[4096];
	while (const size_t got = std::fread(chunk, 1, sizeof(chunk), f)) {
		text.append(chunk, got);
	}
	std::fclose(f);
	return text;
}

int32_t countOf(const std::string& haystack, const char* needle) {
	int32_t count = 0;
	for (size_t at = haystack.find(needle); at != std::string::npos;
	     at = haystack.find(needle, at + std::strlen(needle))) {
		count++;
	}
	return count;
}

} // namespace

/// Every abandonment of a song preview drops the parked scroll direction with it, because
/// `cancelPreview()` is the only door and it does both.
///
/// The invariant, stated: `currentFileChanged()` parks a direction on EVERY cursor move onto a song,
/// and `finishPreviewRender()` is the only thing that consumes one. A cancel is the one way a parked
/// direction can outlive the render that was going to consume it -- so a cancel that forgot to drop
/// it leaves a horizontal scroll owed to a preview that will never arrive, which then fires on top
/// of the NEXT visit's scroll-in and abandons it half-drawn. There is one `LoadSongUI`, so "next
/// visit" is a real thing.
///
/// **This is a source-text pin, and that is a deliberate trade rather than laziness.** The
/// behavioural version needs the whole `LoadSongUI` translation unit compiled against a mocked
/// browser, display, PadLEDs and storage owner -- an order of magnitude more mock surface than
/// `slicer_env_mock.cpp`, for a property that is structural rather than dynamic. What is pinned is
/// exactly the structure the invariant rests on:
///
///   1. `previewLoad_.cancel()` appears ONCE in the whole UI, inside `cancelPreview()`. A seventh
///      cancel site that reached for the primitive directly, rather than for `cancelPreview()`,
///      would leave the direction parked -- and that is the only shape the bug can take.
///   2. `cancelPreview()`'s body drops the direction.
///   3. Six call sites exist. A count, and therefore a tripwire: adding a seventh is fine (it goes
///      through `cancelPreview()` like the rest), but it fails here first, which is what makes
///      whoever adds it read this. Bump it deliberately; never lower it to make a run go green.
///
/// What it does NOT pin: an early return somewhere that skips `finishPreviewRender()` without
/// cancelling. Nothing structural forbids that, and the real fix would be folding the direction into
/// `previewLoad_`'s target -- per-attempt state living inside the attempt. Recorded, not done.
TEST(SongPreviewDeferral, everyPreviewCancelDropsTheParkedScrollDirection) {
	const std::string header = readSource("src/deluge/gui/ui/load/load_song_ui.h");
	const std::string source = readSource("src/deluge/gui/ui/load/load_song_ui.cpp");

	// 1. One `cancel()` on the primitive, in the header, and none anywhere else.
	CHECK_EQUAL(1, countOf(header, "previewLoad_.cancel()"));
	CHECK_EQUAL(0, countOf(source, "previewLoad_.cancel()"));

	// 2. And it is inside cancelPreview(), whose body also drops the direction. Sliced from the
	//    function's opening brace to the first closing brace at column 1 -- enough for a one-purpose
	//    inline body, and it fails loudly rather than silently if the shape changes.
	const size_t body = header.find("void cancelPreview() {");
	CHECK(body != std::string::npos);
	const size_t bodyEnd = header.find("\n\t}", body);
	CHECK(bodyEnd != std::string::npos);
	const std::string cancelBody = header.substr(body, bodyEnd - body);
	CHECK_EQUAL(1, countOf(cancelBody, "previewLoad_.cancel()"));
	CHECK_EQUAL(1, countOf(cancelBody, "pendingScrollDirection_ = 0"));

	// 3. The six sites, as a tripwire. See this test's doc.
	CHECK_EQUAL(6, countOf(source, "cancelPreview()"));

	// And the only OTHER place the direction is written to zero is the render that consumes it.
	CHECK_EQUAL(1, countOf(source, "pendingScrollDirection_ = 0"));
}
