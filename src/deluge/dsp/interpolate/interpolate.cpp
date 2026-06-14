#include "interpolate.h"
#include "definitions_cxx.hpp"
#include "deluge/dsp/stereo_sample.h"
#include "dsp-next/fixed/interpolation/sinc_interpolation.h"   // SincWindow, blend_sinc_kernel, windowed_sinc_dot
#include "dsp-next/fixed/interpolation/windowed_sinc_kernel.h" // windowed_sinc_kernel
#include <argon.hpp>
#include <cstdint>
#include <limits>

namespace deluge::dsp {

using deluge::dsp::fixed::blend_sinc_kernel;
using deluge::dsp::fixed::kSincKernelTaps;
using deluge::dsp::fixed::kSincWindowVectors;
using deluge::dsp::fixed::SincWindow;
using deluge::dsp::fixed::windowed_sinc_dot;
using deluge::dsp::fixed::windowed_sinc_kernel;

// The window into a state buffer (buffer_l / buffer_r), as the int16 vectors windowed_sinc_dot wants.
[[gnu::always_inline]] static inline SincWindow loadWindow(const int16_t* buffer) {
	SincWindow window;
	for (size_t i = 0; i < kSincWindowVectors; ++i) {
		window[i] = Argon<int16_t>::Load(&buffer[i * Argon<int16_t>::lanes]);
	}
	return window;
}

StereoSample Interpolator::interpolate(size_t channels, int32_t whichKernel, uint32_t oscPos) {
	constexpr size_t numBitsInTableSize = 8;
	constexpr size_t rshiftAmount = ((24 + kInterpolationMaxNumSamplesMagnitude) - 16 - numBitsInTableSize + 1);

	uint32_t rshifted;
	if constexpr (rshiftAmount >= 0) {
		rshifted = oscPos >> rshiftAmount;
	}
	else {
		rshifted = oscPos << (-rshiftAmount);
	}

	int16_t strength2 = rshifted & std::numeric_limits<int16_t>::max();
	int32_t progressSmall = oscPos >> (24 + kInterpolationMaxNumSamplesMagnitude - numBitsInTableSize);

	// Position the (shared) kernel to sub-sample accuracy once, then weight each channel's window
	// with it. whichKernel is the getWhichKernel-selected oversampling variant.
	const int16_t* kernelRow = windowed_sinc_kernel.variant(whichKernel) + progressSmall * kSincKernelTaps;
	SincWindow kernel = blend_sinc_kernel(kernelRow, strength2);

	StereoSample output;
	output.l = windowed_sinc_dot(kernel, loadWindow(buffer_l.data()));
	if (channels == 2) {
		output.r = windowed_sinc_dot(kernel, loadWindow(buffer_r.data()));
	}
	return output;
}

StereoSample Interpolator::interpolateLinear(size_t channels, uint32_t phase) {
	StereoSample output;
	int16_t strength2 = phase >> 9;
	int16_t strength1 = std::numeric_limits<int16_t>::max() - strength2; // inverse

	output.l = (buffer_l[1] * strength1) + (buffer_l[0] * strength2);
	if (channels == 2) {
		output.r = (buffer_r[1] * strength1) + (buffer_r[0] * strength2);
	}
	return output;
}
} // namespace deluge::dsp
