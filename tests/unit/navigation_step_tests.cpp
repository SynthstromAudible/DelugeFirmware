#include "CppUTest/TestHarness.h"
#include "gui/ui/load/navigation_step.h"

#include <array>
#include <variant>

using namespace deluge::gui;

namespace {
constexpr NavigationItem F{};                               // free
constexpr NavigationItem S{.isOld = false, .inSong = true}; // already in the song
constexpr NavigationItem O{.isOld = true, .inSong = true};  // the old instrument

template <size_t N>
NavigationWindow window(const std::array<NavigationItem, N>& items, int32_t atStart = 0, int32_t atEnd = 0) {
	return {.items = items,
	        .deletedAtStart = atStart,
	        .deletedAtEnd = atEnd,
	        .firstRemaining = "NAV02.XML",
	        .lastRemaining = "NAV21.XML"};
}

void checkLand(const NavigationDecision& d, int32_t index, int32_t wraps) {
	const Land* land = std::get_if<Land>(&d);
	CHECK(land != nullptr);
	CHECK_EQUAL(index, land->index);
	CHECK_EQUAL(wraps, land->wraps);
}
} // namespace

TEST_GROUP(NavigationStepTests){};

// ── Today's gotos, which must land as they always did ─────────────────────────

TEST(NavigationStepTests, forwardLandsOnTheFirst) {
	const std::array items{F, F, F};
	checkLand(decideNavigation(window(items), 1, false, true, 0), 0, 0);
}

TEST(NavigationStepTests, backwardLandsOnTheLast) {
	const std::array items{F, F, F};
	checkLand(decideNavigation(window(items), -1, false, true, 0), 2, 0);
}

TEST(NavigationStepTests, anEmptyWindowWithANameReadsFromTheEnd) {
	const std::array<NavigationItem, 0> items{};
	const auto d = decideNavigation(window(items), 1, true, true, 0);
	CHECK(std::holds_alternative<FromEnd>(d));
	CHECK_EQUAL(1, std::get<FromEnd>(d).wraps);
}

TEST(NavigationStepTests, anEmptyWindowWithoutANameGetsOut) {
	const std::array<NavigationItem, 0> items{};
	CHECK(std::holds_alternative<GetOut>(decideNavigation(window(items), 1, true, false, 0)));
}

TEST(NavigationStepTests, onlyTheOldInstrumentReadsFromTheEnd) {
	const std::array items{O};
	CHECK(std::holds_alternative<FromEnd>(decideNavigation(window(items), 1, true, true, 0)));
}

TEST(NavigationStepTests, onlyTheOldInstrumentGetsOutWithoutAName) {
	const std::array items{O};
	CHECK(std::holds_alternative<GetOut>(decideNavigation(window(items), 1, true, false, 0)));
}

TEST(NavigationStepTests, inSongIsLandedOnWhenNotSkipping) {
	const std::array items{S, F};
	checkLand(decideNavigation(window(items), 1, false, true, 0), 0, 0);
}

TEST(NavigationStepTests, skipsInSongWithinTheWindow) {
	const std::array items{S, S, F};
	checkLand(decideNavigation(window(items), 1, true, true, 0), 2, 0);
}

TEST(NavigationStepTests, skipsInSongBackward) {
	const std::array items{F, S, S};
	checkLand(decideNavigation(window(items), -1, true, true, 0), 0, 0);
}

TEST(NavigationStepTests, aWrapWithNothingTrimmedStaysInTheWindow) {
	// Off the end with nothing trimmed on either side: round to the start in the same window, no read.
	// (A wrap always restarts at the edge, so in one window it only ever lands there, once the cap stops skipping.)
	const std::array items{S};
	checkLand(decideNavigation(window(items), 1, true, true, 0), 0, 2);
}

TEST(NavigationStepTests, wrapWithTheFarSideTrimmedReadsFromTheEnd) {
	// At the folder's end (nothing after), start trimmed: the wrap has to read the start back in.
	const std::array items{S};
	const auto d = decideNavigation(window(items, 3, 0), 1, true, true, 0);
	CHECK(std::holds_alternative<FromEnd>(d));
	CHECK_EQUAL(1, std::get<FromEnd>(d).wraps);
}

TEST(NavigationStepTests, skippingStopsAfterTwoWrapsInOneWindow) {
	const std::array items{S, S};
	checkLand(decideNavigation(window(items), 1, true, true, 0), 0, 2);
}

// ── Paging, the change ────────────────────────────────────────────────────────

TEST(NavigationStepTests, pagesOnFromLastRemaining) {
	// The old code re-read from the old name here: the same window, for ever.
	const std::array items{S, S};
	const auto d = decideNavigation(window(items, 0, 5), 1, true, true, 0);
	CHECK(std::holds_alternative<PageOn>(d));
	STRCMP_EQUAL("NAV21.XML", std::get<PageOn>(d).from.c_str());
	CHECK_EQUAL(0, std::get<PageOn>(d).wraps);
}

TEST(NavigationStepTests, pagesBackFromFirstRemaining) {
	const std::array items{S, S};
	const auto d = decideNavigation(window(items, 5, 0), -1, true, true, 0);
	CHECK(std::holds_alternative<PageOn>(d));
	STRCMP_EQUAL("NAV02.XML", std::get<PageOn>(d).from.c_str());
}

TEST(NavigationStepTests, skippingStopsOnceTheWalkHasWrappedTwice) {
	const std::array items{S, F};
	checkLand(decideNavigation(window(items), 1, true, true, 2), 0, 0);
}

TEST(NavigationStepTests, wrapsSoFarCountTowardsTheCap) {
	// One wrap already spent: one more inside this window reaches the cap, and it lands where it stands.
	const std::array items{S, S};
	checkLand(decideNavigation(window(items), 1, true, true, 1), 0, 1);
}

TEST(NavigationStepTests, wholeFolderInSongLandsAfterTwoWraps) {
	const std::array items{S, S, S};
	checkLand(decideNavigation(window(items), -1, true, true, 0), 2, 2);
}
