#include "playback/section_sequence.h"
#include <array>

namespace {
constexpr int32_t sectionCount = 24;
constexpr auto anySection = [](int32_t) { return true; };
constexpr auto noSections = [](int32_t) { return false; };
constexpr auto sparseSections = [](int32_t section) { return section == 3 || section == 17; };
constexpr auto singleSection = [](int32_t section) { return section == 9; };

static_assert(nextSectionInSequence(0, sectionCount, false, anySection) == 1);
static_assert(nextSectionInSequence(23, sectionCount, false, anySection) == -1);
static_assert(nextSectionInSequence(23, sectionCount, true, anySection) == 0);
static_assert(nextSectionInSequence(3, sectionCount, true, sparseSections) == 17);
static_assert(nextSectionInSequence(17, sectionCount, true, sparseSections) == 3);
static_assert(nextSectionInSequence(17, sectionCount, false, sparseSections) == -1);
static_assert(nextSectionInSequence(9, sectionCount, true, singleSection) == 9);
static_assert(nextSectionInSequence(9, sectionCount, false, singleSection) == -1);
static_assert(nextSectionInSequence(0, sectionCount, true, noSections) == -1);
static_assert(nextSectionInSequence(-1, sectionCount, true, anySection) == -1);
static_assert(nextSectionInSequence(sectionCount, sectionCount, true, anySection) == -1);
static_assert(nextSectionInSequence(0, 0, true, anySection) == -1);

constexpr bool respectsSectionBounds() {
	std::array<bool, sectionCount> populated{};
	populated[23] = true;
	return nextSectionInSequence(23, sectionCount, true, [&](int32_t section) { return populated[section]; }) == 23;
}
static_assert(respectsSectionBounds());

constexpr bool skipsExcludedSections() {
	constexpr std::array<int32_t, 4> repetitions = {1, -2, 0, 2};
	return nextSectionInSequence(0, repetitions.size(), true,
	                             [&](int32_t section) { return repetitions[section] != -2; })
	       == 2;
}
static_assert(skipsExcludedSections());
} // namespace
