#pragma once
#include "definitions_cxx.hpp"
#include "deluge/dsp/stereo_sample.h"
#include <array>
#include <cstdint>

extern const int16_t windowedSincKernel[][17][16];

namespace deluge::dsp {
struct Interpolator {

	Interpolator() = default;
	StereoSample interpolate(size_t channels, int32_t whichKernel, uint32_t oscPos);
	StereoSample interpolateLinear(size_t channels, uint32_t oscPos);

	[[gnu::always_inline]] constexpr void pushL(int16_t value) {
#pragma unroll
		for (int32_t i = kInterpolationMaxNumSamples - 1; i >= 1; i--) {
			buffer_l[i] = buffer_l[i - 1];
		}
		buffer_l[0] = value;
	}

	[[gnu::always_inline]] constexpr void pushR(int16_t value) {
#pragma unroll
		for (int32_t i = kInterpolationMaxNumSamples - 1; i >= 1; i--) {
			buffer_r[i] = buffer_r[i - 1];
		}
		buffer_r[0] = value;
	}

	[[gnu::always_inline]] constexpr void jumpForward(size_t num_samples) {
#pragma unroll
		for (int32_t i = kInterpolationMaxNumSamples - 1; i >= num_samples; i--) {
			buffer_l[i] = buffer_l[i - num_samples];
			buffer_r[i] = buffer_r[i - num_samples];
		}
	}

	// These are the state buffers (quadword alignment for NEON)
	alignas(sizeof(int32_t) * 4) std::array<int16_t, kInterpolationMaxNumSamples> buffer_l;
	alignas(sizeof(int32_t) * 4) std::array<int16_t, kInterpolationMaxNumSamples> buffer_r;
};
} // namespace deluge::dsp
