#pragma once
#include "definitions_cxx.hpp"
#include "deluge/dsp/stereo_sample.h"
#include <array>
#include <cstdint>

extern const int16_t windowedSincKernel[][17][16];

namespace deluge::dsp {
class Interpolator {
	using buffer_t = std::array<int16_t, kInterpolationMaxNumSamples>;

public:
	Interpolator() = default;
	StereoSample interpolate(size_t channels, int32_t whichKernel, uint32_t oscPos);
	StereoSample interpolateLinear(size_t channels, uint32_t oscPos);

	constexpr int16_t& bufferL(size_t idx) { return interpolation_buffer_l_[idx]; }
	constexpr int16_t& bufferR(size_t idx) { return interpolation_buffer_r_[idx]; }

	constexpr void pushL(int16_t value) {
#pragma unroll
		for (int32_t i = kInterpolationMaxNumSamples - 1; i >= 1; i--) {
			bufferL(i) = bufferL(i - 1);
		}
		interpolation_buffer_l_[0] = value;
	}

	inline void pushR(int16_t value) {
#pragma unroll
		for (int32_t i = kInterpolationMaxNumSamples - 1; i >= 1; i--) {
			bufferR(i) = bufferR(i - 1);
		}
		interpolation_buffer_r_[0] = value;
	}

	constexpr void jumpForward(size_t num_samples) {
#pragma unroll
		for (int32_t i = kInterpolationMaxNumSamples - 1; i >= num_samples; i--) {
			bufferL(i) = bufferL(i - num_samples);
			bufferR(i) = bufferR(i - num_samples);
		}
	}

private:
	buffer_t interpolation_buffer_l_;
	buffer_t interpolation_buffer_r_;
};
} // namespace deluge::dsp
