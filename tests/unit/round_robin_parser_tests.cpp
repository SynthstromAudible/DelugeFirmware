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

	FakeHolder* ensureAlternateHolder(uint8_t alternateSlotIndex) {
		if (failAllocation || alternateSlotIndex >= kMaxRoundRobinAlternates) {
			return nullptr;
		}
		return &holders[alternateSlotIndex];
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

// Token-level stand-in for JsonDeserializer, for the brace bookkeeping MockDeserializer can't see.
// Tokens are "{", "}", "[", "]", "key:<name>", "int:<n>" and "str:<s>". As in the real one, match()
// consumes only the character asked for, a tag list ends at "}" without consuming it, and exitTag()
// closes an object if asked, then skips a value that was never read.
struct JsonTokenDeserializer {
	std::vector<std::string> tokens;
	size_t pos = 0;
	bool valueRead = true;
	std::string lastName;

	[[nodiscard]] bool nextIs(std::string const& token) const { return pos < tokens.size() && tokens[pos] == token; }
	[[nodiscard]] bool nextHasPrefix(char const* prefix) const {
		return pos < tokens.size() && tokens[pos].rfind(prefix, 0) == 0;
	}

	bool match(char c) {
		if (!nextIs(std::string(1, c))) {
			return false;
		}
		pos++;
		return true;
	}

	char const* readNextTagOrAttributeName() {
		if (!nextHasPrefix("key:")) {
			return "";
		}
		lastName = tokens[pos++].substr(4);
		valueRead = false;
		return lastName.c_str();
	}

	int32_t readTagOrAttributeValueInt() {
		valueRead = true;
		return nextHasPrefix("int:") ? std::stoi(tokens[pos++].substr(4)) : 0;
	}

	void readTagOrAttributeValueString(std::string* out) {
		valueRead = true;
		if (nextHasPrefix("str:")) {
			*out = tokens[pos++].substr(4);
		}
	}

	void exitTag(char const* name = nullptr, bool closeObject = false) {
		if (closeObject) {
			match('}');
		}
		if (valueRead) {
			return;
		}
		valueRead = true;
		if (nextIs("{") || nextIs("[")) {
			int32_t depth = 0;
			do {
				if (nextIs("{") || nextIs("[")) {
					depth++;
				}
				else if (nextIs("}") || nextIs("]")) {
					depth--;
				}
				pos++;
			} while (depth > 0 && pos < tokens.size());
		}
		else if (pos < tokens.size()) {
			pos++;
		}
	}
};

// One saved alternate as the JSON writer lays it out: an array item wrapping an "alternate" object.
void appendJsonAlternate(std::vector<std::string>& tokens, std::string const& fileName, int32_t startPos) {
	for (std::string token : {"{", "key:alternate", "{", "key:fileName"}) {
		tokens.push_back(token);
	}
	tokens.push_back("str:" + fileName);
	for (std::string token : {"key:zone", "{", "key:startSamplePos"}) {
		tokens.push_back(token);
	}
	tokens.push_back("int:" + std::to_string(startPos));
	for (std::string token : {"}", "}", "}"}) {
		tokens.push_back(token);
	}
}

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

TEST(RoundRobinParser, emptyArrayYieldsNoAlternates) {
	MockDeserializer reader;
	reader.names = {""};

	FakeRange range;
	range.rrCount = 2;

	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);
	CHECK_EQUAL(0, range.rrCount);
}

TEST(RoundRobinParser, jsonLeavesReaderAfterTheArray) {
	// Regression: each JSON alternate is an array item wrapping an "alternate" object, so it ends with
	// two closing braces. Closing only one left the reader a brace and a bracket behind: the second
	// alternate was lost, and so was every tag after the array.
	JsonTokenDeserializer reader;
	reader.tokens = {"["};
	appendJsonAlternate(reader.tokens, "a.wav", 10);
	appendJsonAlternate(reader.tokens, "b.wav", 20);
	for (std::string token : {"]", "key:rrMode", "int:2", "}"}) {
		reader.tokens.push_back(token);
	}

	FakeRange range;
	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);

	CHECK_EQUAL(2, range.rrCount);
	STRCMP_EQUAL("a.wav", range.holders[0].filePath.c_str());
	CHECK_EQUAL(10, range.holders[0].startPos);
	STRCMP_EQUAL("b.wav", range.holders[1].filePath.c_str());
	CHECK_EQUAL(20, range.holders[1].startPos);
	// The next sibling tag must still read correctly.
	STRCMP_EQUAL("rrMode", reader.readNextTagOrAttributeName());
	CHECK_EQUAL(2, reader.readTagOrAttributeValueInt());
}

TEST(RoundRobinParser, jsonSkipsAlternatesPastTheCap) {
	// An alternate past the slot cap is skipped tag by tag, its zone object included, and the
	// reader must come out in the same place as for one it kept.
	JsonTokenDeserializer reader;
	reader.tokens = {"["};
	for (int32_t i = 0; i < kMaxRoundRobinAlternates + 1; i++) {
		appendJsonAlternate(reader.tokens, std::to_string(i) + ".wav", i);
	}
	for (std::string token : {"]", "key:rrMode", "int:1", "}"}) {
		reader.tokens.push_back(token);
	}

	FakeRange range;
	CHECK_TRUE(readRoundRobinAlternates(reader, &range) == Error::NONE);

	CHECK_EQUAL(kMaxRoundRobinAlternates, range.rrCount);
	STRCMP_EQUAL("rrMode", reader.readNextTagOrAttributeName());
	CHECK_EQUAL(1, reader.readTagOrAttributeValueInt());
}
