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
#include "visualizer_common.h"

namespace deluge::hid::display {

// FFT constants shared across visualizers
constexpr int32_t kSpectrumFFTOutputSize = 257; // (512 >> 1) + 1 bins

// FFT computation and utilities
FFTResult computeVisualizerFFT();
FFTResult computeVisualizerStereoFFT();
void initSpectrumHanningWindow();
bool isFFTSilent(const ne10_fft_cpx_int32_t* fft_output, int32_t threshold);
float calculateWeightedMagnitude(const FFTResult& fft_result, float lower_freq, float upper_freq,
                                 float freq_resolution);

} // namespace deluge::hid::display
