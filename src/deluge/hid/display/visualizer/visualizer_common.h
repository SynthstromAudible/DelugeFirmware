/*
 * Copyright (c) 2025 Bruce Zawadzki (Tone Coder)
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "NE10.h"
#include "definitions_cxx.hpp"

// Forward declarations for OLED canvas
namespace oled_canvas {
class Canvas;
}

namespace deluge::hid::display {

// FFT computation result structure
struct FFTResult {
	ne10_fft_cpx_int32_t* output;       // Mono FFT output (for backward compatibility)
	ne10_fft_cpx_int32_t* output_left;  // Left channel FFT output
	ne10_fft_cpx_int32_t* output_right; // Right channel FFT output
	bool isValid;
	bool isSilent;
	bool isStereo; // Whether stereo FFT results are available

	FFTResult(ne10_fft_cpx_int32_t* output = nullptr, ne10_fft_cpx_int32_t* output_left = nullptr,
	          ne10_fft_cpx_int32_t* output_right = nullptr, bool isValid = false, bool isSilent = false,
	          bool isStereo = false)
	    : output(output), output_left(output_left), output_right(output_right), isValid(isValid), isSilent(isSilent),
	      isStereo(isStereo) {}
};

// Shared constants
constexpr int32_t kDisplayMargin = 2;        // Standard margin for visualizer display areas
constexpr int32_t kDcBinReductionFactor = 4; // DC bin (bin 0) magnitude reduction factor (75% reduction)

// Amplitude computation constants
constexpr float kReferenceAmplitude = 10000.0f; // Q15 reference for moderate audio levels
constexpr uint32_t kAmplitudeSampleCount = 256; // Number of samples to analyze for amplitude

// Helper functions
uint32_t getVisualizerReadStartPos(uint32_t sample_count);
float applyVisualizerCompression(float amplitude, float frequency);

// Compute current audio amplitude from recent samples (peak detection)
float computeCurrentAmplitude();

} // namespace deluge::hid::display
