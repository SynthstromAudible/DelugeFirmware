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
	ne10_fft_cpx_int32_t* output;
	bool isValid;
	bool isSilent;

	FFTResult(ne10_fft_cpx_int32_t* output = nullptr, bool isValid = false, bool isSilent = false)
	    : output(output), isValid(isValid), isSilent(isSilent) {}
};

// Shared constants
constexpr int32_t kDisplayMargin = 2; // Standard margin for visualizer display areas

// Helper functions
uint32_t getVisualizerReadStartPos(uint32_t sample_count);
float applyVisualizerCompression(float amplitude, float frequency);

} // namespace deluge::hid::display
