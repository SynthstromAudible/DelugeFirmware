#pragma once
#include <cstdint>

/// The dB spanned by each of the 16 intervals a volume param value is divided into. Interval 0 is not a real interval.
constexpr float dbIntervals[] = {
    24, // Not really real
    12.1, 7, 5, 3.9, 3.2, 2.6, 2.4, 2, 1.8, 1.7, 1.5, 1.4, 1.3, 1.2, 1.1,
};

inline int32_t shiftVolumeByDB(int32_t oldValue, float offset) {
	uint32_t oldValuePositive = (uint32_t)oldValue + 2147483648;

	uint32_t currentInterval = oldValuePositive >> 28;

	if (currentInterval >= 1) {

		uint32_t newValuePositive;

		int32_t howFarUpInterval = oldValuePositive & 268435455;
		float howFarUpIntervalFloat = howFarUpInterval / 268435456;

doInterval:

		float dbThisInterval = dbIntervals[currentInterval];

		// How many more dB can we get before we reach the top end of this interval?
		float dbLeftThisInterval = ((float)1 - howFarUpIntervalFloat) * dbThisInterval;

		// If we finish in this interval...
		if (dbLeftThisInterval > offset) {
			float amountOfRemainingDBWeWant = offset / dbLeftThisInterval;
			float newHowFarUpIntervalFloat = howFarUpIntervalFloat + amountOfRemainingDBWeWant;

			uint32_t newHowFarUpInterval = newHowFarUpIntervalFloat * 268435456;
			newValuePositive = (currentInterval << 28) + newHowFarUpInterval;
		}

		// Or if we need more...
		else {
			currentInterval++;

			if (currentInterval == 16) {
				newValuePositive = 4294967295u;
			}
			else {
				offset -= dbLeftThisInterval;
				howFarUpIntervalFloat = 0;
				goto doInterval;
			}
		}

		return newValuePositive - 2147483648u;
	}
	else {
		return oldValue;
	}
}
