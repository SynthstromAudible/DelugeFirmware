#pragma once

#include <cstdint>

template <typename IsEligible>
constexpr int32_t nextSectionInSequence(int32_t oldSection, int32_t sectionCount, bool loop, IsEligible isEligible) {
	if (oldSection < 0 || oldSection >= sectionCount) {
		return -1;
	}
	for (int32_t offset = 1; offset <= sectionCount; ++offset) {
		int32_t nextSection = oldSection + offset;
		if (loop) {
			nextSection %= sectionCount;
		}
		else if (nextSection >= sectionCount) {
			break;
		}
		if (isEligible(nextSection)) {
			return nextSection;
		}
	}
	return -1;
}
