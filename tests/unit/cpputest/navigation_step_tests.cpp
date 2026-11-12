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
	return {.items = items, .deletedAtStart = atStart, .deletedAtEnd = atEnd};
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

// ── Paging ────────────────────────────────────────────────────────

TEST(NavigationStepTests, pagesOnFromTheLastEntryShown) {
	// Pages on from the last entry the window SHOWS -- not from its search name, which would re-read
	// the same window forever, and not from FileListing::lastRemaining, which dropFoldersAndDuplicates
	// pops off the window and the next read (which excludes its own search name) would then skip.
	const std::array items{NavigationItem{.inSong = true, .name = "NAV01.XML"},
	                       NavigationItem{.inSong = true, .name = "NAV09.XML"}};
	const auto d = decideNavigation(window(items, 0, 5), 1, true, true, 0);
	CHECK(std::holds_alternative<PageOn>(d));
	STRCMP_EQUAL("NAV09.XML", std::get<PageOn>(d).from.c_str());
	CHECK_EQUAL(0, std::get<PageOn>(d).wraps);
}

TEST(NavigationStepTests, pagesBackFromTheFirstEntryShown) {
	const std::array items{NavigationItem{.inSong = true, .name = "NAV15.XML"},
	                       NavigationItem{.inSong = true, .name = "NAV23.XML"}};
	const auto d = decideNavigation(window(items, 5, 0), -1, true, true, 0);
	CHECK(std::holds_alternative<PageOn>(d));
	STRCMP_EQUAL("NAV15.XML", std::get<PageOn>(d).from.c_str());
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

// ── A pile of detents, moved from the landed entry as main's moveAgain does ───

namespace {
void checkPileLand(const PileDecision& d, int32_t index) {
	const Land* land = std::get_if<Land>(&d);
	CHECK(land != nullptr);
	CHECK_EQUAL(index, land->index);
}
} // namespace

TEST(NavigationStepTests, aPileMovesByAllItsDetentsInsideTheWindow) {
	const std::array items{F, F, F, F, F};
	checkPileLand(movePile(window(items, 3, 4), 0 + 3, 3, false, 0), 3);
	checkPileLand(movePile(window(items, 3, 4), 4 - 2, -2, false, 0), 2);
}

TEST(NavigationStepTests, aPilePastATrimmedEdgeReadsAgainFromTheName) {
	const std::array items{F, F, F};
	CHECK(std::holds_alternative<ReadAgain>(movePile(window(items, 0, 4), 1 + 5, 5, false, 0)));
	CHECK(std::holds_alternative<ReadAgain>(movePile(window(items, 4, 0), 1 - 5, -5, false, 0)));
}

TEST(NavigationStepTests, aPilePastTheFolderEndReadsFromTheFarEnd) {
	// NAV23 landed in the window read from NAV22 (NAV23, NAV24; NAV00..NAV22 behind it): three on is past the end.
	const std::array items{F, F};
	const auto d = movePile(window(items, 23, 0), 0 + 3, 3, false, 0);
	CHECK(std::holds_alternative<FromEnd>(d));
}

TEST(NavigationStepTests, aPilePastTheEndOfAWholeFolderWrapsToItsStartNotOnByTheRest) {
	const std::array items{F, F, F, F};
	checkPileLand(movePile(window(items), 2 + 5, 5, false, 0), 0);
	checkPileLand(movePile(window(items), 1 - 5, -5, false, 0), 3);
}

TEST(NavigationStepTests, aPresetInTheSongIsSkippedByTheWholePileAgain) {
	// From index 0, a pile of 2 lands on 2, which is in the song: main moves on by 2 again, to 4.
	const std::array items{F, F, S, F, F, F};
	checkPileLand(movePile(window(items), 0 + 2, 2, true, 0), 4);
}

TEST(NavigationStepTests, aPileSkipsInSongPresetsOnlyUntilItHasWrappedTwice) {
	// 1, 2, past the end (one wrap), 0, 1, 2, past the end again (two): it stops at 0.
	const std::array items{S, S, S};
	checkPileLand(movePile(window(items), 1, 1, true, 0), 0);
}

TEST(NavigationStepTests, aPileKeepsTheWrapsItsLandingMadeInTheSameWindow) {
	// Main's `wrapped` is reset only by a read. From 2, past the end is a landing's second wrap when it had wrapped
	// once already, so the pile stops on 0 there; with none made it skips on to 1.
	const std::array items{S, F, S};
	checkPileLand(movePile(window(items), 2, 1, true, 1), 0);
	checkPileLand(movePile(window(items), 2, 1, true, 0), 1);
}

TEST(NavigationStepTests, aReadForAPileStartsAtTheNearEnd) {
	const std::array items{F, F, F};
	checkPileLand(startPileWindow(window(items, 2, 2), 4, false, true), 0);
	checkPileLand(startPileWindow(window(items, 2, 2), -4, false, true), 2);
}

TEST(NavigationStepTests, anEmptyReadForAPileReadsFromTheFarEndOnceThenGetsOut) {
	const std::array<NavigationItem, 0> items{};
	CHECK(std::holds_alternative<FromEnd>(startPileWindow(window(items), 3, false, true)));
	CHECK(std::holds_alternative<GetOut>(startPileWindow(window(items), 3, false, false)));
}
