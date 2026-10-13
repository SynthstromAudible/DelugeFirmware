#include "CppUTest/TestHarness.h"
#include "storage/multi_range/round_robin_serialization.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

// Lightweight stand-ins for SampleHolderForVoice / MultisampleRange, exposing exactly the members
// readRoundRobinAlternates() touches.
struct FakeHolder {
	std::string filePath;
	int32_t transpose = 0;
	int32_t cents = 0;
	int32_t startPos = 0;
	int32_t endPos = 0;
	int32_t loopStartPos = 0;
	int32_t loopEndPos = 0;
	uint8_t volume = kVariantVolumeUnity;

	void setCents(int32_t newCents) { cents = newCents; }
};

struct FakeRange {
	uint8_t rrCount = 0;
	uint8_t rrIndex = 0;
	uint8_t rrMode = 0;
	FakeHolder holders[kMaxRoundRobinAlternates];
	bool failAllocation = false;
	uint8_t velocityRangeMin[kMaxRoundRobinSlots] = {1, 1, 1, 1};
	uint8_t velocityRangeMax[kMaxRoundRobinSlots] = {127, 127, 127, 127};

	FakeHolder* ensureAlternateHolder(uint8_t alternateSlotIndex) {
		if (failAllocation || alternateSlotIndex >= kMaxRoundRobinAlternates) {
			return nullptr;
		}
		return &holders[alternateSlotIndex];
	}

	uint8_t getVelocityRangeMin(uint8_t slotIndex) const { return velocityRangeMin[slotIndex]; }
	uint8_t getVelocityRangeMax(uint8_t slotIndex) const { return velocityRangeMax[slotIndex]; }
	bool setVelocityRange(uint8_t slotIndex, uint8_t min, uint8_t max) {
		if (slotIndex >= kMaxRoundRobinSlots) {
			return false;
		}
		velocityRangeMin[slotIndex] = min;
		velocityRangeMax[slotIndex] = max;
		return true;
	}
};

// Scripted deserializer: readNextTagOrAttributeName() consumes `names` in order ("" ends a tag
// list, mirroring the real deserializers); value reads consume `ints`/`strings` in order.
// match() always succeeds, as with the XML deserializer where braces don't exist.
struct MockDeserializer {
	std::vector<std::string> names;
	std::vector<int32_t> ints;
	std::vector<std::string> strings;
	size_t nameIdx = 0;
	size_t intIdx = 0;
	size_t stringIdx = 0;
	int32_t exitTagCalls = 0;

	bool match(char c) { return true; }

	char const* readNextTagOrAttributeName() {
		if (nameIdx >= names.size()) {
			return "";
		}
		return names[nameIdx++].c_str();
	}

	int32_t readTagOrAttributeValueInt() {
		if (intIdx >= ints.size()) {
			return 0;
		}
		return ints[intIdx++];
	}

	void readTagOrAttributeValueString(std::string* out) {
		if (stringIdx < strings.size()) {
			*out = strings[stringIdx++];
		}
	}

	void exitTag(char const* name = nullptr, bool closeObject = false) { exitTagCalls++; }
};

} // namespace

TEST_GROUP(RoundRobinParser){};

TEST(RoundRobinParser, parsesTwoFullAlternates) {
	MockDeserializer reader;
	reader.names = {
	    // First alternate: every field present.
	    "alternate", "fileName", "transpose", "cents", "zone", "startSamplePos", "endSamplePos", "startLoopPos",
	    "endLoopPos", "", // end zone
	    "",               // end alternate
	    // Second alternate: file only.
	    "alternate", "fileName", "", // end alternate
	    "",                          // end array
	};
	reader.strings = {"SAMPLES/kick_1.wav", "SAMPLES/kick_2.wav"};
	reader.ints = {-12, 25, 100, 2000, 300, 1900};

	FakeRange range;
	range.rrCount = 3; // Stale values must be reset by the parser.
	range.rrIndex = 2;

	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);

	CHECK_EQUAL(2, range.rrCount);
	CHECK_EQUAL(0, range.rrIndex);
	STRCMP_EQUAL("SAMPLES/kick_1.wav", range.holders[0].filePath.c_str());
	CHECK_EQUAL(-12, range.holders[0].transpose);
	CHECK_EQUAL(25, range.holders[0].cents);
	CHECK_EQUAL(100, range.holders[0].startPos);
	CHECK_EQUAL(2000, range.holders[0].endPos);
	CHECK_EQUAL(300, range.holders[0].loopStartPos);
	CHECK_EQUAL(1900, range.holders[0].loopEndPos);
	STRCMP_EQUAL("SAMPLES/kick_2.wav", range.holders[1].filePath.c_str());
	CHECK_EQUAL(0, range.holders[1].transpose);
}

TEST(RoundRobinParser, doesNotTouchRrMode) {
	// Regression: the writer emits rrMode BEFORE roundRobinAlternates, so the parser resetting
	// rrMode wiped the saved mode on every load.
	MockDeserializer reader;
	reader.names = {"alternate", "fileName", "", ""};
	reader.strings = {"a.wav"};

	FakeRange range;
	range.rrMode = 2; // NoRepeat

	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);
	CHECK_EQUAL(2, range.rrMode);
}

TEST(RoundRobinParser, reinitialisesReusedHolders) {
	// A holder that already held values (e.g. the range is being re-read) must be zeroed even if
	// the file omits those fields.
	MockDeserializer reader;
	reader.names = {"alternate", "fileName", "", ""};
	reader.strings = {"a.wav"};

	FakeRange range;
	range.holders[0].transpose = 7;
	range.holders[0].cents = -3;
	range.holders[0].endPos = 12345;

	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);
	CHECK_EQUAL(0, range.holders[0].transpose);
	CHECK_EQUAL(0, range.holders[0].cents);
	CHECK_EQUAL(0, range.holders[0].endPos);
}

TEST(RoundRobinParser, capsAtMaxAlternatesAndSkipsExtras) {
	MockDeserializer reader;
	reader.names = {
	    "alternate", "fileName", "", // 1
	    "alternate", "fileName", "", // 2
	    "alternate", "fileName", "", // 3
	    "alternate", "fileName", "", // 4 - over the cap, must be skipped
	    "",
	};
	reader.strings = {"1.wav", "2.wav", "3.wav", "4.wav"};

	FakeRange range;
	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);

	CHECK_EQUAL(3, range.rrCount);
	// The 4th file name must NOT have been consumed into any holder.
	STRCMP_EQUAL("1.wav", range.holders[0].filePath.c_str());
	STRCMP_EQUAL("2.wav", range.holders[1].filePath.c_str());
	STRCMP_EQUAL("3.wav", range.holders[2].filePath.c_str());
}

TEST(RoundRobinParser, skipsUnknownTagsWithoutDesync) {
	MockDeserializer reader;
	reader.names = {
	    "futureTag",                                               // unknown at array level
	    "alternate", "someNewField", "fileName", "anotherOne", "", // unknowns inside an alternate
	    "",
	};
	reader.strings = {"a.wav"};

	FakeRange range;
	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);

	CHECK_EQUAL(1, range.rrCount);
	STRCMP_EQUAL("a.wav", range.holders[0].filePath.c_str());
	// Each unknown tag must have been skipped via exitTag (3 unknowns + regular per-field exits).
	CHECK_TRUE(reader.exitTagCalls >= 3);
}

TEST(RoundRobinParser, propagatesAllocationFailure) {
	MockDeserializer reader;
	reader.names = {"alternate", "fileName", "", ""};
	reader.strings = {"a.wav"};

	FakeRange range;
	range.failAllocation = true;

	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::INSUFFICIENT_RAM);
	CHECK_EQUAL(0, range.rrCount);
}

TEST(RoundRobinParser, parsesVelocityRangeOnAlternate) {
	MockDeserializer reader;
	reader.names = {
	    "alternate", "fileName", "velocityRangeMin", "velocityRangeMax", "", "",
	};
	reader.strings = {"a.wav"};
	reader.ints = {40, 90};

	FakeRange range;
	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);

	CHECK_EQUAL(1, range.rrCount);
	CHECK_EQUAL(40, range.getVelocityRangeMin(1));
	CHECK_EQUAL(90, range.getVelocityRangeMax(1));
}

TEST(RoundRobinParser, alternateWithoutVelocityRangeKeepsDefaults) {
	MockDeserializer reader;
	reader.names = {"alternate", "fileName", "", ""};
	reader.strings = {"a.wav"};

	FakeRange range;
	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);

	CHECK_EQUAL(1, range.getVelocityRangeMin(1));
	CHECK_EQUAL(127, range.getVelocityRangeMax(1));
}

TEST(RoundRobinParser, emptyArrayYieldsNoAlternates) {
	MockDeserializer reader;
	reader.names = {""};

	FakeRange range;
	range.rrCount = 2;

	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);
	CHECK_EQUAL(0, range.rrCount);
}

TEST(RoundRobinParser, alternateWithOutOfRangeVelocityFallsBackToDefaults) {
	// End to end through the alternates parser: a hand-edited 300/-1 pair leaves the slot on the
	// full 1-127 band rather than on the 44/255 the raw cast would have produced.
	MockDeserializer reader;
	reader.names = {"alternate", "fileName", "velocityRangeMin", "velocityRangeMax", "", ""};
	reader.strings = {"loud.wav"};
	reader.ints = {300, -1};

	FakeRange range;
	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);

	CHECK_EQUAL(1, range.rrCount);
	CHECK_EQUAL(1, range.getVelocityRangeMin(1));
	CHECK_EQUAL(127, range.getVelocityRangeMax(1));
}

TEST(RoundRobinParser, backwardsVelocityPairIsLeftAlone) {
	// min > max is not reconciled at read time: the two attributes arrive independently, and any
	// "keep them ordered" rule would corrupt a correctly-written file purely because of attribute
	// order. Both values are in range, so both are stored exactly as written.
	MockDeserializer reader;
	reader.names = {"alternate", "fileName", "velocityRangeMin", "velocityRangeMax", "", ""};
	reader.strings = {"a.wav"};
	reader.ints = {100, 60};

	FakeRange range;
	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);

	CHECK_EQUAL(100, range.getVelocityRangeMin(1));
	CHECK_EQUAL(60, range.getVelocityRangeMax(1));
}

// --- readVelocityRangeFromFile() -------------------------------------------------------------
//
// Velocity bands are the only round-robin numbers a song file can carry that the menu itself can't
// produce out of range, so they're the ones worth guarding. Tested directly rather than only
// through readRoundRobinAlternates() because the same helper backs four more read sites in
// sound.cpp (the primary sample's band, at source level and per zone).

TEST_GROUP(VelocityRangeParser){};

TEST(VelocityRangeParser, validValuesPassThroughUnchanged) {
	for (int32_t value : {1, 2, 64, 126, 127}) {
		MockDeserializer reader;
		reader.ints = {value};
		CHECK_EQUAL(value, readVelocityRangeFromFile(reader, "velocityRangeMin", 1));
	}
}

TEST(VelocityRangeParser, outOfRangeFallsBackToTheEndsOwnDefault) {
	// A min falls back to 1 and a max to 127 - between them the full default band, which is exactly
	// what a slot carrying no velocity attributes at all already gets. Failing open like this keeps
	// the slot reachable; clamping would not (a mangled max of -1 would clamp to 1, leaving a band
	// of 1-1 that no realistic velocity plays).
	for (int32_t invalid : {0, -1, 128, 200, 300, 100000}) {
		MockDeserializer readerMin;
		readerMin.ints = {invalid};
		CHECK_EQUAL(1, readVelocityRangeFromFile(readerMin, "velocityRangeMin", 1));

		MockDeserializer readerMax;
		readerMax.ints = {invalid};
		CHECK_EQUAL(127, readVelocityRangeFromFile(readerMax, "velocityRangeMax", 127));
	}
}

TEST(VelocityRangeParser, guardsTheTruncationTrap) {
	// The whole reason the helper exists. Assigning the raw int straight into a uint8_t truncates
	// first, so these three would have arrived as 44, 0 and 255 - and 44 in particular is a
	// perfectly legal velocity, so no check after the cast could ever have caught it.
	MockDeserializer r300;
	r300.ints = {300}; // (uint8_t)300 == 44
	CHECK_EQUAL(1, readVelocityRangeFromFile(r300, "velocityRangeMin", 1));

	MockDeserializer r256;
	r256.ints = {256}; // (uint8_t)256 == 0
	CHECK_EQUAL(1, readVelocityRangeFromFile(r256, "velocityRangeMin", 1));

	MockDeserializer rNeg;
	rNeg.ints = {-1}; // (uint8_t)-1 == 255
	CHECK_EQUAL(127, readVelocityRangeFromFile(rNeg, "velocityRangeMax", 127));
}

TEST(VelocityRangeParser, consumesTheTagExactlyOnce) {
	MockDeserializer reader;
	reader.ints = {64};
	readVelocityRangeFromFile(reader, "velocityRangeMin", 1);
	CHECK_EQUAL(1, reader.exitTagCalls);

	// Rejecting a value must still consume its tag, or the rest of the file parses off by one.
	MockDeserializer bad;
	bad.ints = {300};
	readVelocityRangeFromFile(bad, "velocityRangeMin", 1);
	CHECK_EQUAL(1, bad.exitTagCalls);
}
