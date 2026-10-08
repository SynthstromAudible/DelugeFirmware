#include "CppUTest/TestHarness.h"
#include "model/drum/choke_group.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using deluge::drum::countBundlingChokeGroups;
using deluge::drum::readChokeGroupFromFile;
using deluge::drum::shouldChoke;
using deluge::drum::writeChokeGroupToFile;

namespace {

// Lightweight stand-ins exposing exactly the members writeChokeGroupToFile() / readChokeGroupFromFile()
// touch - enough to exercise the real read/write logic on the host, with no Serializer/Deserializer.
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
	// Group 1 is the omitted-attribute case, covered separately below.
	for (uint8_t group = 2; group <= 8; group++) {
		MockWriter writer;
		writeChokeGroupToFile(writer, group);

		MockReader reader;
		reader.valueToReturn = writer.attributeValues[0];
		CHECK_EQUAL(group, readChokeGroupFromFile(reader));
	}
}

TEST(ChokeGroup, xmlDefaultGroupIsOmittedButStillRoundTrips) {
	// Group 1 writes nothing at all, so a kit nobody has assigned groups in saves exactly as it did
	// before the feature existed - no attribute appears on any drum, and re-saving an untouched song
	// doesn't show a change on every drum in it.
	MockWriter writer;
	writeChokeGroupToFile(writer, 1);
	CHECK_EQUAL(0u, writer.attributeNames.size());

	// The omission is lossless: SoundDrum's field initialiser gives an absent attribute group 1
	// anyway, and a file that does carry an explicit 1 still reads back as 1.
	MockReader reader;
	reader.valueToReturn = 1;
	CHECK_EQUAL(1, readChokeGroupFromFile(reader));
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

// countBundlingChokeGroups() backs choke-group stem export: it's what turns "these drums belong to
// these choke groups" into "which drums get rendered together, and how many files that comes to."
// Tested here against a fake predicate (counting into a plain std::vector standing in for a kit's
// exportable drums) rather than real NoteRow/InstrumentClip/Kit objects, the same host-testable
// approach the XML round-trip tests above use for the (de)serializer - see stem_export.cpp's
// exportChokeGroupStems() and currentKitHasBundlingChokeGroup() for the real callers.
//
// The vector is one entry per exportable drum, holding the group that drum belongs to FOR EXPORT
// PURPOSES - which is 0 for any drum not in CHOKE mode, however its chokeGroup field happens to
// read. That distinction is the whole point: every drum carries a chokeGroup, defaulting to 1, so
// keying on the stored number alone would put the entire non-choke half of a kit into group 1.
// stem_export.cpp's exportChokeGroupOf() is what collapses that to 0.
namespace {
uint8_t countBundlesIn(const std::vector<uint8_t>& drumGroups) {
	return countBundlingChokeGroups(
	    [&](uint8_t group) { return (int32_t)std::count(drumGroups.begin(), drumGroups.end(), group); });
}

/// Total files a kit of these drums produces: one per bundling group, plus one for every drum not
/// in one. Mirrors what disarmAllChokeGroupsForStemExport() computes for the progress display.
int32_t filesProducedBy(const std::vector<uint8_t>& drumGroups) {
	int32_t files = 0;
	int32_t drumsInBundles = 0;
	for (uint8_t group = 1; group <= 8; group++) {
		auto members = (int32_t)std::count(drumGroups.begin(), drumGroups.end(), group);
		if (members > 1) {
			files++;
			drumsInBundles += members;
		}
	}
	return files + ((int32_t)drumGroups.size() - drumsInBundles);
}
} // namespace

TEST_GROUP(ChokeGroupExport){};

TEST(ChokeGroupExport, emptyKitBundlesNothing) {
	CHECK_EQUAL(0, countBundlesIn({}));
	CHECK_EQUAL(0, filesProducedBy({}));
}

TEST(ChokeGroupExport, aGroupOfOneIsNotABundle) {
	// One drum alone in a group has nothing to choke it, so rendering it "as a group" would produce
	// exactly the same audio as rendering it on its own, under a more confusing name. It is not a
	// bundle, and its file is named after the drum.
	CHECK_EQUAL(0, countBundlesIn({3}));
	CHECK_EQUAL(1, filesProducedBy({3}));

	CHECK_EQUAL(0, countBundlesIn({1, 2, 3, 4}));
	CHECK_EQUAL(4, filesProducedBy({1, 2, 3, 4}));
}

TEST(ChokeGroupExport, twoDrumsSharingAGroupAreOneBundle) {
	CHECK_EQUAL(1, countBundlesIn({1, 1}));
	CHECK_EQUAL(1, filesProducedBy({1, 1}));
}

TEST(ChokeGroupExport, nonChokeDrumsAreNeverBundled) {
	// The bug this design replaced. A kit of a hi-hat pair on choke group 1 plus three Poly drums -
	// which carry the default chokeGroup of 1 without being in any group at all - used to produce a
	// single "ChokeGroup1" file containing all five. With non-choke drums reported as group 0, the
	// hats bundle and the three Poly drums each get their own file: four files, not one.
	CHECK_EQUAL(1, countBundlesIn({1, 1, 0, 0, 0}));
	CHECK_EQUAL(4, filesProducedBy({1, 1, 0, 0, 0}));

	// And a kit with no choke drums at all is just a per-drum export.
	CHECK_EQUAL(0, countBundlesIn({0, 0, 0, 0}));
	CHECK_EQUAL(4, filesProducedBy({0, 0, 0, 0}));
}

TEST(ChokeGroupExport, theMixedKit) {
	// Kick and snare on Poly, closed and open hat on choke group 1, crash alone on choke group 3.
	// Files: kick, snare, ChokeGroup1 (both hats), crash = 4.
	CHECK_EQUAL(1, countBundlesIn({0, 0, 1, 1, 3}));
	CHECK_EQUAL(4, filesProducedBy({0, 0, 1, 1, 3}));
}

TEST(ChokeGroupExport, everyDrumInItsOwnGroupIsAPlainDrumExport) {
	// Eight choke drums, eight groups: nothing bundles, so this is the same division of the kit a
	// plain per-drum export makes.
	CHECK_EQUAL(0, countBundlesIn({1, 2, 3, 4, 5, 6, 7, 8}));
	CHECK_EQUAL(8, filesProducedBy({1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST(ChokeGroupExport, allEightGroupsCanBundleAtOnce) {
	std::vector<uint8_t> kit;
	for (uint8_t group = 1; group <= 8; group++) {
		kit.push_back(group);
		kit.push_back(group);
	}
	CHECK_EQUAL(8, countBundlesIn(kit));
	CHECK_EQUAL(8, filesProducedBy(kit));
}

TEST(ChokeGroupExport, tenDrumsInOneGroupAreStillOneFile) {
	CHECK_EQUAL(1, countBundlesIn({7, 7, 7, 7, 7, 7, 7, 7, 7, 7}));
	CHECK_EQUAL(1, filesProducedBy({7, 7, 7, 7, 7, 7, 7, 7, 7, 7}));
}

TEST(ChokeGroupExport, groupZeroIsNeverCountedAsAGroup) {
	// countBundlingChokeGroups() only ever asks about groups 1-8, so the ungrouped drums can never
	// bundle with each other no matter how many of them there are.
	CHECK_EQUAL(0, countBundlesIn({0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_EQUAL(10, filesProducedBy({0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
}

TEST(ChokeGroupExport, offeringTheOptionMatchesWhetherAnythingBundles) {
	// currentKitHasBundlingChokeGroup() is countBundlingChokeGroups(...) > 0. Turning the toggle on
	// changes the output only when something actually bundles, so that is exactly when the menu
	// item is worth showing.
	CHECK_FALSE(countBundlesIn({}) > 0);
	CHECK_FALSE(countBundlesIn({4}) > 0);
	CHECK_FALSE(countBundlesIn({0, 0, 0}) > 0);
	CHECK_FALSE(countBundlesIn({1, 2, 3}) > 0);
	CHECK_TRUE(countBundlesIn({4, 4}) > 0);
	CHECK_TRUE(countBundlesIn({0, 0, 3, 3}) > 0);
}
