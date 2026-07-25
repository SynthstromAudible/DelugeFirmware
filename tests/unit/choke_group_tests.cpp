#include "CppUTest/TestHarness.h"
#include "model/drum/choke_group.h"

#include <cstdint>
#include <string>
#include <vector>

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
