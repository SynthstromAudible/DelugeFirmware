#include "CppUTest/TestHarness.h"
#include "model/drum/choke_group.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using deluge::drum::countDistinctChokeGroups;
using deluge::drum::readChokeGroupFromFile;
using deluge::drum::shouldChoke;
using deluge::drum::writeChokeGroupToFile;

namespace {

// Lightweight stand-ins exposing exactly the members writeChokeGroupToFile() / readChokeGroupFromFile()
// touch, following the pattern established by round_robin_parser_tests.cpp's MockDeserializer.
struct MockWriter {
	std::vector<std::string> attributeNames;
	std::vector<int32_t> attributeValues;

	void writeAttribute(char const* name, int32_t value, bool onNewLine = true) {
		attributeNames.emplace_back(name);
		attributeValues.push_back(value);
	}
};

struct MockReader {
	int32_t valueToReturn = 0;
	int32_t exitTagCalls = 0;

	int32_t readTagOrAttributeValueInt() { return valueToReturn; }
	void exitTag(char const* name = nullptr, bool closeObject = false) { exitTagCalls++; }
};

} // namespace

TEST_GROUP(ChokeGroup){};

TEST(ChokeGroup, sameGroupChokesEachOther) {
	CHECK_TRUE(shouldChoke(PolyphonyMode::CHOKE, 3, 3));
}

TEST(ChokeGroup, differentGroupsDoNotChokeEachOther) {
	CHECK_FALSE(shouldChoke(PolyphonyMode::CHOKE, 3, 4));
}

TEST(ChokeGroup, nonChokeDrumNeverChokesRegardlessOfGroup) {
	// A drum that isn't even in CHOKE mode must never be silenced by a choke broadcast, even if
	// its (otherwise meaningless) chokeGroup happens to match the triggering group.
	CHECK_FALSE(shouldChoke(PolyphonyMode::MONO, 3, 3));
	CHECK_FALSE(shouldChoke(PolyphonyMode::POLY, 3, 3));
	CHECK_FALSE(shouldChoke(PolyphonyMode::AUTO, 3, 3));
	CHECK_FALSE(shouldChoke(PolyphonyMode::LEGATO, 3, 3));
}

TEST(ChokeGroup, freshDrumsDefaultToGroupOneAndChokeEachOther) {
	// Mirrors SoundDrum's `uint8_t chokeGroup = 1;` field-construction default: two drums that
	// never had this attribute in their saved XML both land in group 1 and choke each other,
	// reproducing the pre-existing "one shared, kit-wide choke group" behaviour with zero
	// migration code.
	constexpr uint8_t kDefaultGroup = 1;
	CHECK_TRUE(shouldChoke(PolyphonyMode::CHOKE, kDefaultGroup, kDefaultGroup));
}

TEST(ChokeGroup, triggeringDrumChokesItself) {
	// Kit::choke()'s broadcast includes the triggering drum itself (no exclusion in its loop), so
	// its own retrigger cuts off its own previous voice purely via group-matching - no special
	// self-choke path exists or should exist.
	CHECK_TRUE(shouldChoke(PolyphonyMode::CHOKE, /*targetChokeGroup=*/5, /*triggeringChokeGroup=*/5));
}

TEST(ChokeGroup, xmlRoundTrip) {
	MockWriter writer;
	writeChokeGroupToFile(writer, 5);

	CHECK_EQUAL(1u, writer.attributeNames.size());
	STRCMP_EQUAL("chokeGroup", writer.attributeNames[0].c_str());
	CHECK_EQUAL(5, writer.attributeValues[0]);

	MockReader reader;
	reader.valueToReturn = writer.attributeValues[0];
	uint8_t readBack = readChokeGroupFromFile(reader);

	CHECK_EQUAL(5, readBack);
	CHECK_EQUAL(1, reader.exitTagCalls);
}

TEST(ChokeGroup, xmlRoundTripAcrossAllValidGroups) {
	for (uint8_t group = 1; group <= 8; group++) {
		MockWriter writer;
		writeChokeGroupToFile(writer, group);

		MockReader reader;
		reader.valueToReturn = writer.attributeValues[0];
		CHECK_EQUAL(group, readChokeGroupFromFile(reader));
	}
}

TEST(ChokeGroup, xmlOutOfRangeValuesFallBackToDefaultGroup) {
	// Out-of-range values can only come from hand-edited or corrupted song files. They fall back to
	// group 1 - the same group every pre-feature CHOKE drum lands in - so untrusted data behaves
	// like legacy data. 257 in particular guards the truncation trap: naively read into a uint8_t
	// it would wrap to a "valid" 1 before any validation could see it.
	for (int32_t invalid : {0, -1, 9, 100, 256, 257}) {
		MockReader reader;
		reader.valueToReturn = invalid;
		CHECK_EQUAL(1, readChokeGroupFromFile(reader));
	}
}

// countDistinctChokeGroups() backs choke-group stem export: it's what turns "these drums have these
// effective choke groups" into "this many WAV files get produced." Tested here against a fake
// predicate (a lookup into a plain std::vector standing in for a kit's eligible drums) rather than
// real NoteRow/InstrumentClip/Kit objects, the same host-testable approach the XML round-trip tests
// above use for the (de)serializer - see stem_export.cpp's disarmAllChokeGroupsForStemExport() and
// currentKitSpansMultipleChokeGroups() for the real, firmware-side callers of this same function.
namespace {
uint8_t countGroupsPresentIn(const std::vector<uint8_t>& presentGroups) {
	return countDistinctChokeGroups([&](uint8_t group) {
		return std::find(presentGroups.begin(), presentGroups.end(), group) != presentGroups.end();
	});
}
} // namespace

TEST_GROUP(ChokeGroupExport){};

TEST(ChokeGroupExport, noEligibleDrumsYieldsZeroGroups) {
	// An empty kit, or one where every drum is muted/empty/non-CHOKE, produces no stems at all.
	CHECK_EQUAL(0, countGroupsPresentIn({}));
}

TEST(ChokeGroupExport, allDrumsInOneGroupYieldsOneStem) {
	// Every drum still defaulted to (or explicitly set to) group 1 - the common case for kits that
	// never touched the CHOKE GROUP menu - produces exactly one file, same as a plain whole-kit
	// export. This is also the exact condition currentKitSpansMultipleChokeGroups() checks for to
	// decide whether the choke-group export option is worth surfacing in the UI at all.
	CHECK_EQUAL(1, countGroupsPresentIn({1, 1, 1, 1}));
}

TEST(ChokeGroupExport, drumsSpanningMultipleGroupsAreCountedSeparately) {
	CHECK_EQUAL(3, countGroupsPresentIn({1, 1, 3, 3, 3, 5}));
}

TEST(ChokeGroupExport, duplicateGroupValuesAreNotDoubleCounted) {
	// Ten drums all sharing group 7 must still yield exactly one stem, not ten.
	CHECK_EQUAL(1, countGroupsPresentIn({7, 7, 7, 7, 7, 7, 7, 7, 7, 7}));
}

TEST(ChokeGroupExport, allEightGroupsPresentYieldsEightStems) {
	CHECK_EQUAL(8, countGroupsPresentIn({1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST(ChokeGroupExport, spansMultipleGroupsMatchesGreaterThanOneCount) {
	// currentKitSpansMultipleChokeGroups() is implemented as countDistinctChokeGroups(...) > 1;
	// verify that composition directly rather than just trusting the individual counts above.
	CHECK_FALSE(countGroupsPresentIn({}) > 1);
	CHECK_FALSE(countGroupsPresentIn({4, 4, 4}) > 1);
	CHECK_TRUE(countGroupsPresentIn({4, 4, 6}) > 1);
}
