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

// The analytic waves now live in dsp-next: the square/triangle kernels (deluge::dsp::fixed::square /
// triangle / triangle_full, dsp-next/fixed/bandlimited/waves.h) and the sine LUT evaluator
// (deluge::dsp::fixed::sine_table, dsp-next/fixed/osc/sine_table.h) — the canonical home shared by the
// crude oscillators, the LFO, the envelope and GUI easing. interpolateTableSigned stays here as the generic
// signed-table linear interp still used across the sample/timestretch/filter DSP.
