#pragma once
#include "definitions_cxx.hpp"
#include <arm_neon_shim.h>
#include <array>
#include <cstdint>

extern const int16_t windowedSincKernel[][17][16];

namespace deluge::dsp {

std::array<int32_t, 2>
interpolate(std::array<std::array<int16x4_t, kInterpolationMaxNumSamples / 4>, 2>& interpolationBuffer, size_t channels,
            int32_t whichKernel, uint32_t oscPos);

std::array<int32_t, 2>
interpolateLinear(std::array<std::array<int16x4_t, kInterpolationMaxNumSamples / 4>, 2>& interpolationBuffer,
                  size_t channels, int32_t whichKernel, uint32_t oscPos);

} // namespace deluge::dsp
