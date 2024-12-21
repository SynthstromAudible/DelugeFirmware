#pragma once
#include "definitions_cxx.hpp"
#include "deluge/dsp/stereo_sample.h"
#include <arm_neon_shim.h>
#include <array>
#include <cstdint>

extern const int16_t windowedSincKernel[][17][16];

namespace deluge::dsp {
StereoSample interpolate(int32_t numChannelsNow, int32_t whichKernel, uint32_t oscPos,
                         std::array<std::array<int16x4_t, kInterpolationMaxNumSamples / 4>, 2>& interpolationBuffer);
StereoSample
interpolateLinear(int32_t numChannelsNow, int32_t whichKernel, uint32_t oscPos,
                  std::array<std::array<int16x4_t, kInterpolationMaxNumSamples / 4>, 2>& interpolationBuffer);

} // namespace deluge::dsp
