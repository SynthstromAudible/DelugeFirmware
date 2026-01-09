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

#include "visualizer_line_spectrum.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"
#include "visualizer_common.h"
#include "visualizer_fft.h"
#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

// Spectrum-specific constants and buffers
namespace {
constexpr int32_t kSpectrumFFTSize = 512; // Match FFT size from visualizer_fft.cpp
// Smoothing filter coefficients (first-order IIR)
constexpr float kSmoothingAlpha = 0.6f; // Temporal smoothing strength (higher = more stable, less responsive)
constexpr float kSmoothingBeta = 0.4f;  // Responsiveness to input changes (higher = more responsive)

// Visualizer calibration constants
constexpr int32_t kFFTReferenceMagnitude = 60000000; // Q31 format for FFT output

// Static smoothing arrays for visual compression (optional time-averaging)
constexpr int32_t kMaxSpectrumPixels = 128; // Max display width (OLED is typically 128 pixels wide)
std::array<float, kMaxSpectrumPixels> spectrum_smoothed_values{};
} // namespace

/// Render visualizer spectrum on OLED display using FFT
void renderVisualizerLineSpectrum(oled_canvas::Canvas& canvas) {
	// Cache visualizer mode to avoid redundant runtime feature settings queries
	uint32_t visualizer_mode = deluge::hid::display::Visualizer::getMode();

	constexpr int32_t k_display_width = OLED_MAIN_WIDTH_PIXELS;
	constexpr int32_t k_display_height = OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL;
	constexpr int32_t k_margin = kDisplayMargin;
	constexpr int32_t k_graph_min_x = k_margin;
	constexpr int32_t k_graph_max_x = k_display_width - k_margin - 1;
	constexpr int32_t k_graph_height = k_display_height - (k_margin * 2);
	constexpr int32_t k_graph_min_y = OLED_MAIN_TOPMOST_PIXEL + k_margin;
	constexpr int32_t k_graph_max_y = OLED_MAIN_TOPMOST_PIXEL + k_display_height - k_margin - 1;

	// Compute FFT using shared helper function (with caching optimization)
	FFTResult fft_result = computeVisualizerFFT();
	if (!fft_result.isValid) {
		// Not enough samples or FFT config not available, draw empty
		return;
	}

	// Fixed reference magnitude for fixed-amplitude display aligned with VU meter
	// When VU meter shows clipping, spectrum should be at peak
	// FFT output is Q31 complex format, so magnitude is sqrt(r^2 + i^2) in Q31 range.
	constexpr int32_t k_fixed_reference_magnitude = kFFTReferenceMagnitude;

	// Check for silence by examining a few representative bins
	// If all bins are very small, don't update display to avoid flicker from brief gaps
	// Previous spectrum frame remains visible
	if (fft_result.isSilent) {
		return;
	}

	// Clear the visualizer area before drawing
	canvas.clearAreaExact(k_graph_min_x, k_graph_min_y, k_graph_max_x, k_graph_max_y + 1);

	// Render spectrum line graph (low frequencies on left, high on right)
	int32_t last_x = -1;
	int32_t last_y = -1;
	bool is_first_point = true;

	// Map frequency bins to display pixels using modified logarithmic frequency scale
	// Bass frequencies (20-200 Hz) are compressed to take up less horizontal space
	// while keeping the rest of the frequency range proportional and natural
	constexpr int32_t k_num_bins = 257;                                         // 257 bins (from FFT)
	constexpr int32_t k_num_pixels = k_graph_max_x - k_graph_min_x + 1;         // 124 pixels
	constexpr float k_min_frequency = 20.0f;                                    // Start at 20 Hz for bass
	constexpr float k_max_frequency = static_cast<float>(::kSampleRate) / 2.0f; // Nyquist frequency

	// Precompute logarithmic scale constant: log10(sample_rate / 2 / 20)
	float log_scale_constant = 0.0f;
	log_scale_constant = std::log10(k_max_frequency / k_min_frequency);

	// Frequency compression exponent: 0.85 provides gentler compression of bass range
	// while keeping mids and highs readable and proportional
	constexpr float k_frequency_compression_exponent = 0.85f;

	for (int32_t pixel = 0; pixel < k_num_pixels; pixel++) {
		// Map pixel to frequency using modified logarithmic scale with compression
		// Apply power curve to compress low frequencies (bass range) more than high frequencies
		// Formula: compressedX = normalizedX^exponent, then f = 20 * 10^(compressedX * log10(sample_rate / 2 / 20))
		float normalized_x = static_cast<float>(pixel) / static_cast<float>(k_num_pixels - 1);
		float compressed_x = 0.0f;
		float frequency = 0.0f;
		compressed_x = std::pow(normalized_x, k_frequency_compression_exponent);
		frequency = k_min_frequency * std::pow(10.0f, compressed_x * log_scale_constant);

		// Map frequency to FFT bin index
		// Bin i represents frequency: f_i = i * sample_rate / kSpectrumFFTSize
		// So: bin = frequency * kSpectrumFFTSize / sample_rate
		float bin_float = frequency * static_cast<float>(kSpectrumFFTSize) / static_cast<float>(::kSampleRate);

		// Use linear interpolation between adjacent bins to avoid stepping artifacts
		// when multiple pixels map to the same bin (especially at low frequencies)
		int32_t bin_index_low = 0;
		bin_index_low = static_cast<int32_t>(std::floor(bin_float));
		int32_t bin_index_high = bin_index_low + 1;
		float fraction = bin_float - static_cast<float>(bin_index_low);

		// Clamp bin indices to valid range
		bin_index_low = std::max(static_cast<int32_t>(0), std::min(bin_index_low, k_num_bins - 1));
		bin_index_high = std::max(static_cast<int32_t>(0), std::min(bin_index_high, k_num_bins - 1));

		// Get magnitudes for both bins
		// Reduce DC bin (bin 0) influence to prevent low frequency artifacts
		int32_t magnitude_low = fastPythag(fft_result.output[bin_index_low].r, fft_result.output[bin_index_low].i);
		int32_t magnitude_high = fastPythag(fft_result.output[bin_index_high].r, fft_result.output[bin_index_high].i);
		if (bin_index_low == 0) {
			magnitude_low /= kDcBinReductionFactor; // Reduce DC bin influence by 75%
		}

		// Interpolate between the two bins
		// Use 64-bit intermediate to prevent overflow during calculation
		float magnitude_float =
		    (static_cast<float>(magnitude_low) * (1.0f - fraction)) + (static_cast<float>(magnitude_high) * fraction);

		// Apply music sweet-spot visual compression
		// Normalize amplitude to 0-1 range
		float amplitude = magnitude_float / static_cast<float>(k_fixed_reference_magnitude);
		float display_value = applyVisualizerCompression(amplitude, frequency);

		// Apply smoothing filter for stability (first-order IIR)
		// Use per-pixel smoothing instead of per-bin to avoid conflicts when multiple pixels map to same bin
		// This prevents stepping artifacts, especially at low frequencies after compression
		// Only use smoothing buffer when in spectrum mode (conditional memory usage)
		if (pixel < kMaxSpectrumPixels && visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerLineSpectrum) {
			spectrum_smoothed_values[pixel] =
			    (spectrum_smoothed_values[pixel] * kSmoothingAlpha) + (display_value * kSmoothingBeta);
			display_value = spectrum_smoothed_values[pixel];
		}

		// Clamp display_value to valid range and scale to graph height
		display_value = std::clamp(display_value, 0.0f, 1.0f);
		auto scaled_height = static_cast<int32_t>(display_value * static_cast<float>(k_graph_height));

		// Convert to pixel Y position (spectrum: baseline at bottom, magnitude grows upward)
		// When magnitude is 0, y should be at bottom (kGraphMaxY)
		// When magnitude reaches fixed reference, y should be at top (kGraphMinY)
		// Clamp scaledHeight to graph height to prevent overflow
		scaled_height = std::min(scaled_height, k_graph_height);
		int32_t y = k_graph_max_y - scaled_height;
		// Clamp to valid display range (k_graph_min_y = top, k_graph_max_y = bottom)
		y = std::clamp(y, k_graph_min_y, k_graph_max_y);

		// X position
		int32_t x = k_graph_min_x + pixel;

		// Draw line from previous point to current point
		if (!is_first_point && last_x >= 0 && last_x != x) {
			canvas.drawLine(last_x, last_y, x, y);
		}
		else if (is_first_point) {
			// First point, just draw a pixel
			canvas.drawPixel(x, y);
			is_first_point = false;
		}

		last_x = x;
		last_y = y;
	}

	// Mark OLED as changed so it gets sent to display
	OLED::markChanged();
}

} // namespace deluge::hid::display
