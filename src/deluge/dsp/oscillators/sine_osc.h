#pragma once

#include "deluge/util/misc.h"
#include <argon.hpp>
#include <array>
#include <cstdint>

namespace deluge::dsp {
class [[gnu::hot]] SineOsc {
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

	// Test and verified to be identical to original function 11/22/24
	static inline Argon<int32_t> getSineVector(uint32_t* thisPhase, uint32_t phaseIncrement) {
		// create a vector containing a ramp of incrementing phases:
		// {phase + (phaseIncrement * 1), phase + (phaseIncrement * 2), phase + (phaseIncrement * 3), phase +
		// (phaseIncrement * 4)}
		auto phaseVector = Argon<uint32_t>::LoadCopy(thisPhase).MultiplyAdd(uint32x4_t{1, 2, 3, 4}, phaseIncrement);
		*thisPhase = *thisPhase + phaseIncrement * 4;

		return render(phaseVector);
	}

	// Test and verified to be identical to original function 11/22/24
	static inline Argon<int32_t> doFMVector(Argon<uint32_t> phaseVector, Argon<uint32_t> phaseShift) {
		return render(phaseVector + (phaseShift << 8));
	}

private:
	static inline Argon<int32_t> render(Argon<uint32_t> phase) {
		// interpolation fractional component
		ArgonHalf<int16_t> strength2 = phase.ShiftRightNarrow<32 - 16 - kSineTableSizeMagnitude + 1>()
		                                   .BitwiseAnd(std::numeric_limits<int16_t>::max())
		                                   .As<int16_t>();

		// multiply by 2 due to interleave. i.e. sin(x) is actually table[x * 2]
		Argon<uint32_t> indices = (phase >> (32 - kSineTableSizeMagnitude)) << 1;

		//  load our two relevent table components
		auto [sine, diff] = ArgonHalf<int16_t>::LoadGatherInterleaved<2>(sineWaveDiff.data(), indices);

		// Essentially a MultiplyAddFixedPoint, but without the reduction back down to q31
		return sine.ShiftLeftLong<16>().MultiplyDoubleAddSaturateLong(diff, strength2);
	}

}; // namespace deluge::dsp
} // namespace deluge::dsp
