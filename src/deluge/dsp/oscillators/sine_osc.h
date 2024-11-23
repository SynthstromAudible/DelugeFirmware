#pragma once

#include "deluge/util/misc.h"
#include <arm_neon.h>
#include <array>
#include <cstdint>

namespace deluge::dsp {
class SineOsc {
	static constexpr size_t kSineTableSizeMagnitude = 8;
	static std::array<int16_t, 512> sineWaveDiff;

public:
	static inline int32_t doFMNew(uint32_t carrierPhase, uint32_t phaseShift) {
		// return getSineNew((((*carrierPhase += carrierPhaseIncrement) >> 8) + phaseShift) & 16777215, 24);

		uint32_t phaseSmall = (carrierPhase >> 8) + phaseShift;
		int32_t strength2 = phaseSmall & 65535;

		uint32_t readOffset = (phaseSmall >> (24 - 8 - 1)) & 0b0111111110;

		uint32_t readValue = *(uint32_t*)&sineWaveDiff[readOffset];
		int32_t value = readValue << 16;
		int32_t diff = (int32_t)readValue >> 16;
		return value + diff * strength2;
	}

	static inline int32x4_t getSineVector(uint32_t* thisPhase, uint32_t phaseIncrement) {

		int16x4_t strength2;
		uint32x4_t readValue;

		for (int32_t i = 0; i < 4; i++) {
			*thisPhase += phaseIncrement;
			int32_t whichValue = *thisPhase >> (32 - kSineTableSizeMagnitude);
			strength2[i] = (*thisPhase >> (32 - 16 - kSineTableSizeMagnitude + 1)) & 32767;

			uint32_t readOffset = whichValue << 1;

			readValue[i] = *(uint32_t*)&sineWaveDiff[readOffset];
		}

		int32x4_t enlargedValue1 = vreinterpretq_s32_u32(vshlq_n_u32(readValue, 16));
		int16x4_t diffValue = vshrn_n_s32(vreinterpretq_s32_u32(readValue), 16);

		return vqdmlal_s16(enlargedValue1, strength2, diffValue);
	}

	static inline int32x4_t doFMVector(uint32x4_t phaseVector, uint32x4_t phaseShift) {

		uint32x4_t finalPhase = vaddq_u32(phaseVector, vshlq_n_u32(phaseShift, 8));

		uint32x4_t readValue{0};
		util::constexpr_for<0, 4, 1>([&]<int i>() {
			uint32_t readOffsetNow = (vgetq_lane_u32(finalPhase, i) >> (32 - kSineTableSizeMagnitude)) << 1;
			uint32_t* thisReadAddress = (uint32_t*)&sineWaveDiff[readOffsetNow];
			readValue = vld1q_lane_u32(thisReadAddress, readValue, i);
		});

		int16x4_t strength2 =
		    vreinterpret_s16_u16(vshr_n_u16(vshrn_n_u32(finalPhase, (32 - 16 - kSineTableSizeMagnitude)), 1));

		int32x4_t enlargedValue1 = vreinterpretq_s32_u32(vshlq_n_u32(readValue, 16));
		int16x4_t diffValue = vshrn_n_s32(vreinterpretq_s32_u32(readValue), 16);

		return vqdmlal_s16(enlargedValue1, strength2, diffValue);
	}
}; // namespace deluge::dsp
} // namespace deluge::dsp
