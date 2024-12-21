#pragma once
#include "definitions_cxx.hpp"
#include "deluge/dsp/stereo_sample.h"
#include <arm_neon_shim.h>
#include <array>
#include <cstdint>

extern const int16_t windowedSincKernel[][17][16];

namespace deluge::dsp {
class Interpolator {
	using buffer_t = std::array<int16x4_t, kInterpolationMaxNumSamples / 4>;

public:
	Interpolator() = default;
	StereoSample interpolate(size_t channels, int32_t whichKernel, uint32_t oscPos);
	StereoSample interpolateLinear(size_t channels, uint32_t oscPos);

	constexpr int16_t& bufferL(size_t idx) { return interpolation_buffer_l_[idx / 4][idx % 4]; }
	constexpr int16_t& bufferR(size_t idx) { return interpolation_buffer_r_[idx / 4][idx % 4]; }

private:
	buffer_t interpolation_buffer_l_{};
	buffer_t interpolation_buffer_r_{};
};
} // namespace deluge::dsp
