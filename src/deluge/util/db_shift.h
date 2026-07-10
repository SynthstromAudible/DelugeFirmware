#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

/// The dB spanned by each of the 16 intervals a volume param value is divided into. Interval 0 is not a real interval.
constexpr float dbIntervals[] = {
    24, // Not really real
    12.1, 7, 5, 3.9, 3.2, 2.6, 2.4, 2, 1.8, 1.7, 1.5, 1.4, 1.3, 1.2, 1.1,
};

/// Shift a volume param value by `offset` dB. Positive offsets raise it, negative offsets lower it; both saturate.
/// The value's position is linear in dB within each of the 16 intervals of `dbIntervals`.
inline int32_t shiftVolumeByDB(int32_t oldValue, float offset) {
	uint32_t oldValuePositive = static_cast<uint32_t>(oldValue) + 2147483648u;
	uint32_t currentInterval = oldValuePositive >> 28;

	// Interval 0 is not a real dB interval - nothing to shift within it.
	if (currentInterval < 1) {
		return oldValue;
	}

	float howFarUpInterval = static_cast<float>(oldValuePositive & 268435455u) / 268435456.0f;

	while (true) {
		float dbThisInterval = dbIntervals[currentInterval];

		if (offset >= 0.0f) {
			// How many more dB can we get before we reach the top end of this interval?
			float dbLeftThisInterval = (1.0f - howFarUpInterval) * dbThisInterval;
			if (dbLeftThisInterval > offset) {
				howFarUpInterval += offset / dbThisInterval;
				break;
			}
			if (currentInterval == 15) {
				return std::numeric_limits<int32_t>::max();
			}
			offset -= dbLeftThisInterval;
			currentInterval++;
			howFarUpInterval = 0.0f;
		}
		else {
			// How many dB can we shed before we reach the bottom end of this interval?
			float dbLeftThisInterval = howFarUpInterval * dbThisInterval;
			if (dbLeftThisInterval > -offset) {
				howFarUpInterval += offset / dbThisInterval;
				break;
			}
			if (currentInterval == 1) {
				return std::numeric_limits<int32_t>::min();
			}
			offset += dbLeftThisInterval;
			currentInterval--;
			howFarUpInterval = 1.0f;
		}
	}

	howFarUpInterval = std::clamp(howFarUpInterval, 0.0f, 0.99999994f);
	uint32_t newValuePositive = (currentInterval << 28) + static_cast<uint32_t>(howFarUpInterval * 268435456.0f);
	return static_cast<int32_t>(newValuePositive - 2147483648u);
}
