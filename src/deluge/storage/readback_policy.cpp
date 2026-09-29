#include "storage/readback_policy.h"

#include <cstring>

namespace deluge::storage {

ReadBackStage nextReadBackStage(ReadBackStage stage, const ReadBackExpectation& expect) {
	switch (stage) {
	case ReadBackStage::Length:
		return ReadBackStage::Head;
	case ReadBackStage::Head:
		return expect.tail != nullptr ? ReadBackStage::Tail : ReadBackStage::Passed;
	case ReadBackStage::Tail:
	case ReadBackStage::Passed:
		return ReadBackStage::Passed;
	}
	return ReadBackStage::Passed;
}

uint32_t readBackWant(ReadBackStage stage, const ReadBackExpectation& expect) {
	switch (stage) {
	case ReadBackStage::Head:
		return static_cast<uint32_t>(strlen(expect.head));
	case ReadBackStage::Tail:
		return expect.tail != nullptr ? static_cast<uint32_t>(strlen(expect.tail)) : 0;
	case ReadBackStage::Length:
	case ReadBackStage::Passed:
		return 0;
	}
	return 0;
}

uint32_t readBackOffset(ReadBackStage stage, const ReadBackExpectation& expect) {
	if (stage != ReadBackStage::Tail) {
		return 0;
	}
	const uint32_t want = readBackWant(stage, expect);
	// Clamped rather than wrapped: a tail longer than the document is a mismatch, and an offset that
	// wrapped round to four gigabytes would ask the card for something absurd instead.
	return expect.length >= want ? expect.length - want : 0;
}

bool readBackLengthMatches(uint32_t onCard, const ReadBackExpectation& expect) {
	return onCard == expect.length;
}

bool readBackBytesMatch(ReadBackStage stage, const ReadBackExpectation& expect, const char* got, uint32_t gotLen) {
	if (stage != ReadBackStage::Head && stage != ReadBackStage::Tail) {
		return false;
	}
	const uint32_t want = readBackWant(stage, expect);
	const uint32_t offset = readBackOffset(stage, expect);
	if (want == 0 || gotLen != want || want > expect.length || offset + want > expect.length) {
		return false;
	}
	const char* required = stage == ReadBackStage::Head ? expect.head : expect.tail;
	return memcmp(got, required, want) == 0 && memcmp(got, expect.staged + offset, want) == 0;
}

} // namespace deluge::storage
