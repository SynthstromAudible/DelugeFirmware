#pragma once

#include "util/lookuptables/lookuptables.h"

#define CONG (jcong = 69069 * jcong + 1234567)

extern uint32_t z, w, jcong;

/* Basic waveform generation functions.
 *
 * Extracted from functions.h.
 */

// input must not have any extra bits set than numBitsInInput specifies
[[gnu::always_inline]] inline int32_t interpolateTableSigned(uint32_t input, int32_t numBitsInInput,
                                                             const int16_t* table, int32_t numBitsInTableSize = 8) {
	int32_t whichValue = input >> (numBitsInInput - numBitsInTableSize);
	int32_t rshiftAmount = numBitsInInput - 16 - numBitsInTableSize;
	uint32_t rshifted;
	if (rshiftAmount >= 0)
		rshifted = input >> rshiftAmount;
	else
		rshifted = input << (-rshiftAmount);
	int32_t strength2 = rshifted & 65535;
	int32_t strength1 = 65536 - strength2;
	return (int32_t)table[whichValue] * strength1 + (int32_t)table[whichValue + 1] * strength2;
}

[[gnu::always_inline]] inline int32_t getSine(uint32_t phase, uint8_t numBitsInInput = 32) {
	return interpolateTableSigned(phase, numBitsInInput, sineWaveSmall, 8);
}

[[gnu::always_inline]] inline int32_t getSquare(uint32_t phase, uint32_t phaseWidth = 2147483648u) {
	return ((phase >= phaseWidth) ? (-2147483648) : 2147483647);
}

[[gnu::always_inline]] inline int32_t getSquareSmall(uint32_t phase, uint32_t phaseWidth = 2147483648u) {
	return ((phase >= phaseWidth) ? (-1073741824) : 1073741823);
}

/* Should be faster, but isn't...
 * inline int32_t getTriangleSmall(int32_t phase) {
    int32_t multiplier = (phase >> 31) | 1;
    phase *= multiplier;
 */

[[gnu::always_inline]] inline int32_t getTriangleSmall(uint32_t phase) {
	if (phase >= 2147483648u)
		phase = -phase;
	return phase - 1073741824;
}

[[gnu::always_inline]] inline int32_t getTriangle(uint32_t phase) {
	int32_t slope = 2;
	int32_t offset = 0x80000000u;
	if (phase >= 0x80000000u) {
		slope = -2;
		offset = 0x80000000u - 1;
	}
	return slope * phase + offset;
}

/// Compute phase scaler for audio-rate triangle with dead zone
[[gnu::always_inline]] inline uint64_t computeTrianglePhaseScaler(uint32_t phaseWidth) {
	return 0xFFFFFFFFFFFFFFFFULL / phaseWidth;
}

/// Bipolar triangle with dead zone — one complete cycle then silence
[[gnu::always_inline]] inline int32_t triangleWithDeadzoneBipolar(uint32_t phase, uint32_t phaseWidth,
                                                                  uint64_t phaseScaler = 0) {
	if (phase >= phaseWidth) {
		return 0;
	}

	if (phaseScaler == 0) {
		phaseScaler = 0xFFFFFFFFFFFFFFFFULL / phaseWidth;
	}

	uint32_t scaledPhase = static_cast<uint32_t>((static_cast<uint64_t>(phase) * phaseScaler) >> 32);

	// Offset so waveform starts at 0 (quarter cycle), use getTriangleSmall for amplitude matching
	return getTriangleSmall(scaledPhase + 0x40000000u);
}
