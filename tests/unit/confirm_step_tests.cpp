#include "CppUTest/TestHarness.h"
#include "gui/ui/load/confirm_step.h"

#include <array>
#include <string_view>
#include <variant>

using namespace deluge::gui;

namespace {
template <size_t N>
ConfirmWindow window(const std::array<std::string_view, N>& names, bool popped, int32_t shown, int32_t atStart = 0,
                     int32_t atEnd = 0) {
	return {.readNames = names,
	        .poppedLast = popped,
	        .shownCount = shown,
	        .deletedAtStart = atStart,
	        .deletedAtEnd = atEnd};
}
constexpr std::array<std::string_view, 0> kNone{};
constexpr std::array<std::string_view, 3> kThree{"NAV03.XML", "NAV04.XML", "NAV05.XML"};
} // namespace

TEST_GROUP(ConfirmStepTests){};

// ── An empty read ─────────────────────────────────────────────────────────────

TEST(ConfirmStepTests, emptyReadWhileGrabbingGoesAnywhere) {
	CHECK(std::holds_alternative<ConfirmAnyPreset>(decideConfirm(window(kNone, false, 0, 3), true)));
}

TEST(ConfirmStepTests, emptyReadTrimmedAtStartReadsFromTheStart) {
	CHECK(std::holds_alternative<ConfirmFromStart>(decideConfirm(window(kNone, false, 0, 3), false)));
}

TEST(ConfirmStepTests, emptyReadOtherwiseGoesAnywhere) {
	CHECK(std::holds_alternative<ConfirmAnyPreset>(decideConfirm(window(kNone, false, 0), false)));
}

// ── Nothing available after filtering ────────────────────────────────────────

TEST(ConfirmStepTests, nothingAvailablePagesOnFromBeforeThePoppedEntry) {
	// The old code went on from NAV05 -- popped off the window unoffered, and excluded by the next read.
	const auto d = decideConfirm(window(kThree, true, 0, 0, 5), false);
	CHECK(std::holds_alternative<ConfirmPageOn>(d));
	STRCMP_EQUAL("NAV04.XML", std::get<ConfirmPageOn>(d).from.c_str());
}

TEST(ConfirmStepTests, nothingAvailableNothingPoppedPagesFromTheLast) {
	const auto d = decideConfirm(window(kThree, false, 0, 0, 5), false);
	CHECK(std::holds_alternative<ConfirmPageOn>(d));
	STRCMP_EQUAL("NAV05.XML", std::get<ConfirmPageOn>(d).from.c_str());
}

TEST(ConfirmStepTests, nothingAvailableTrimmedAtEndPagesEvenWhileGrabbing) {
	CHECK(std::holds_alternative<ConfirmPageOn>(decideConfirm(window(kThree, true, 0, 0, 5), true)));
}

TEST(ConfirmStepTests, nothingAvailableWhileGrabbingGoesAnywhere) {
	CHECK(std::holds_alternative<ConfirmAnyPreset>(decideConfirm(window(kThree, false, 0, 2), true)));
}

TEST(ConfirmStepTests, nothingAvailableTrimmedAtStartReadsFromTheStart) {
	CHECK(std::holds_alternative<ConfirmFromStart>(decideConfirm(window(kThree, false, 0, 2), false)));
}

TEST(ConfirmStepTests, nothingAvailableOtherwiseGoesAnywhere) {
	CHECK(std::holds_alternative<ConfirmAnyPreset>(decideConfirm(window(kThree, false, 0), false)));
}

// ── Something available ──────────────────────────────────────────────────────

TEST(ConfirmStepTests, landsOnTheFirstShown) {
	CHECK(std::holds_alternative<ConfirmLand>(decideConfirm(window(kThree, true, 2, 0, 5), false)));
}
