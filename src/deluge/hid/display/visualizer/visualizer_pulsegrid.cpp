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

#include "visualizer_pulsegrid.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "model/settings/runtime_feature_settings.h"
#include "visualizer_common.h"
#include "visualizer_fft.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace deluge::hid::display {

// Constants for grid layout and audio processing
namespace {
// Grid layout constants
constexpr int32_t kGridSize = 4;                              // 4x4 grid
constexpr int32_t kCellSize = 10;                             // 10x10 pixels per cell
constexpr int32_t kSingleGridWidth = kGridSize * kCellSize;   // 4 * 10 = 40px
constexpr int32_t kSingleGridHeight = kGridSize * kCellSize;  // 4 * 10 = 40px
constexpr int32_t kNumGrids = 3;                              // Triple mirroring
constexpr int32_t kTotalWidth = kSingleGridWidth * kNumGrids; // 40 * 3 = 120px
constexpr int32_t kTotalHeight = kSingleGridHeight;           // 40px height

// Screen layout constants (OLED is 128x43)
constexpr int32_t kLeftPadding = (OLED_MAIN_WIDTH_PIXELS - kTotalWidth) / 2; // Center horizontally
constexpr int32_t kTopPadding =
    (OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL - kTotalHeight) / 2; // Center vertically

// 16 frequency bands for audio analysis
constexpr int32_t kNumFrequencyBands = 16;
// Frequency range constants
constexpr float kMinFrequency = 20.0f;    // Minimum frequency for first band
constexpr float kMaxFrequency = 20000.0f; // Maximum frequency for last band
// Frequency bands covering audible spectrum from 31Hz to 20kHz
constexpr std::array<float, kNumFrequencyBands> kPulseGridFrequencies = {
    31.0f,   50.0f,   80.0f,   125.0f,  200.0f,  315.0f,   500.0f,   800.0f,
    1250.0f, 2000.0f, 3150.0f, 5000.0f, 8000.0f, 12500.0f, 16000.0f, 20000.0f};

// Visualizer calibration constants
constexpr int32_t kFftReferenceMagnitude = 60000000; // Q31 format for FFT output

// Amplitude smoothing constants (first-order IIR filter)
// Uses balanced smoothing for responsive pulsing effects
constexpr float kSmoothingAlpha = 0.5f;             // Temporal smoothing strength
constexpr float kSmoothingBeta = 0.5f;              // Responsiveness to input changes
constexpr float kAudioDetectionThreshold = 0.0075f; // Amplitude threshold for audio detection

// Dithering thresholds (normalized 0-1 amplitude)
constexpr float kOffThreshold = 0.15f;   // Below this = completely off
constexpr float kSolidThreshold = 0.50f; // Above this = solid fill
// Between thresholds = dithered (checkerboard pattern)

} // namespace

// Calculate frequency band range for pulse grid cell
// Uses logarithmic spacing around center frequency
void calculatePulseGridFrequencyBandRange(int32_t band, float& lower_freq, float& upper_freq) {
	// Bounds checking for safety
	if (band < 0 || band >= kNumFrequencyBands) {
		lower_freq = 0.0f;
		upper_freq = 0.0f;
		return;
	}
	float center_freq = kPulseGridFrequencies[band];
	if (band == 0) {
		// First band: from minimum frequency to midpoint between band 1 and band 2
		lower_freq = kMinFrequency;
		upper_freq = (kPulseGridFrequencies[band] + kPulseGridFrequencies[band + 1]) / 2.0f;
	}
	else if (band == kNumFrequencyBands - 1) {
		// Last band: from midpoint between previous and center to maximum frequency
		lower_freq = (kPulseGridFrequencies[band - 1] + center_freq) / 2.0f;
		upper_freq = kMaxFrequency;
	}
	else {
		// Middle bands: range between midpoints of adjacent bands
		lower_freq = (kPulseGridFrequencies[band - 1] + center_freq) / 2.0f;
		upper_freq = (center_freq + kPulseGridFrequencies[band + 1]) / 2.0f;
	}
}

// Render a single pulse grid cell with dithering based on amplitude
void renderPulseGridCell(oled_canvas::Canvas& canvas, int32_t base_x, int32_t base_y, int32_t cell_x, int32_t cell_y,
                         float amplitude) {
	// Clamp amplitude to valid range
	amplitude = std::clamp(amplitude, 0.0f, 1.0f);

	// Determine dithering level based on amplitude (3 levels)
	bool (*ditherFunc)(int32_t, int32_t) = nullptr;
	if (amplitude < kOffThreshold) {
		// Low amplitude: completely off
		ditherFunc = [](int32_t, int32_t) { return false; };
	}
	else if (amplitude < kSolidThreshold) {
		// Medium amplitude: dithered pattern (checkerboard)
		ditherFunc = [](int32_t x, int32_t y) { return ((x + y) % 2) == 0; };
	}
	else {
		// High amplitude: solid fill
		ditherFunc = [](int32_t, int32_t) { return true; };
	}

	// Calculate cell position
	int32_t cell_left = base_x + (cell_x * kCellSize);
	int32_t cell_top = base_y + (cell_y * kCellSize);

	// Render 10x10 cell with dithering
	for (int32_t y = 0; y < kCellSize; y++) {
		for (int32_t x = 0; x < kCellSize; x++) {
			if (ditherFunc(x, y)) {
				int32_t pixel_x = cell_left + x;
				int32_t pixel_y = cell_top + y;
				canvas.drawPixel(pixel_x, pixel_y);
			}
		}
	}
}

/// Render visualizer pulse grid on OLED display
///
/// Algorithm:
/// 1. Compute FFT on most recent 512 audio samples (shared with other visualizers)
/// 2. Compute current audio amplitude and apply IIR smoothing for stability
/// 3. Check for silence using both FFT analysis and amplitude threshold
/// 4. For each of 16 frequency bands, map to FFT bins using weighted interpolation
/// 5. Apply visual compression for consistent amplitude scaling
/// 6. Render 4x4 grid with frequency mapping (bottom-left = low, top-right = high)
/// 7. Mirror the grid 3 times across the display width
/// 8. Use dithering for 3 brightness levels per cell (off, dithered, solid)
void renderVisualizerPulseGrid(oled_canvas::Canvas& canvas) {
	// Static smoothed amplitude for IIR filtering (persistent between frames)
	static float smoothed_amplitude_value = 0.0f;

	// Pulse grid occupies centered area with padding
	constexpr int32_t k_grid_start_x = kLeftPadding;
	constexpr int32_t k_grid_start_y = OLED_MAIN_TOPMOST_PIXEL + kTopPadding;
	constexpr int32_t k_grid_end_x = k_grid_start_x + kTotalWidth - 1;
	constexpr int32_t k_grid_end_y = k_grid_start_y + kTotalHeight - 1;

	// Compute FFT using shared helper function
	FFTResult fft_result = computeVisualizerFFT();
	if (!fft_result.isValid) {
		// Not enough samples or FFT config not available, draw empty
		return;
	}

	// Check for audio amplitude to determine if we should render
	float current_amplitude = ::deluge::hid::display::computeCurrentAmplitude();

	// Apply amplitude smoothing using IIR filter for stability (first-order IIR)
	smoothed_amplitude_value = (smoothed_amplitude_value * kSmoothingAlpha) + (current_amplitude * kSmoothingBeta);
	float smoothed_amplitude = smoothed_amplitude_value;

	// Use amplitude-based audio detection (more sensitive than FFT silence detection)
	bool has_audio = (smoothed_amplitude > kAudioDetectionThreshold);

	// Check for silence by examining representative bins
	// If all bins are very small, don't update display to avoid flicker from brief gaps
	if (fft_result.isSilent) {
		return;
	}

	// Clear the visualizer area before drawing
	canvas.clearAreaExact(k_grid_start_x, k_grid_start_y, k_grid_end_x, k_grid_end_y + 1);

	// Calculate frequency resolution per bin
	constexpr int32_t k_spectrum_fft_size = 512; // Match FFT size from visualizer_fft.cpp
	float freq_resolution = static_cast<float>(::kSampleRate) / static_cast<float>(k_spectrum_fft_size);

	// Process each frequency band and render corresponding grid cell
	for (int32_t band = 0; band < kNumFrequencyBands; band++) {
		// Calculate frequency range for this band
		float lower_freq = 0.0f;
		float upper_freq = 0.0f;
		calculatePulseGridFrequencyBandRange(band, lower_freq, upper_freq);

		// Calculate weighted average magnitude using FFT bin interpolation
		float avg_magnitude_float = calculateWeightedMagnitude(fft_result, lower_freq, upper_freq, freq_resolution);

		// Apply visual compression for consistent scaling
		float amplitude = avg_magnitude_float / static_cast<float>(kFftReferenceMagnitude);
		float display_value = applyVisualizerCompression(amplitude, kPulseGridFrequencies[band]);

		// Clamp display value to valid range
		display_value = std::clamp(display_value, 0.0f, 1.0f);

		// Map band index to grid coordinates (4x4 grid, row-major order)
		// band 0 = cell (0,0) bottom-left, band 15 = cell (3,3) top-right
		int32_t grid_x = band % kGridSize; // Column (0-3)
		int32_t grid_y = band / kGridSize; // Row (0-3), but invert Y for bottom-to-top layout
		grid_y = (kGridSize - 1) - grid_y; // Flip Y so row 0 is at bottom

		// Render the same cell in all 3 mirrored grids
		for (int32_t grid_index = 0; grid_index < kNumGrids; grid_index++) {
			int32_t grid_base_x = k_grid_start_x + (grid_index * kSingleGridWidth);
			int32_t grid_base_y = k_grid_start_y;

			renderPulseGridCell(canvas, grid_base_x, grid_base_y, grid_x, grid_y, display_value);
		}
	}

	// Mark OLED as changed so it gets sent to display
	OLED::markChanged();
}

} // namespace deluge::hid::display
