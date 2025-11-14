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

#include "visualizer_stereo_line_spectrum.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "model/settings/runtime_feature_settings.h"
#include "visualizer_common.h"
#include "visualizer_fft.h"
#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

// Stereo Spectrum-specific constants and buffers
namespace {
// Smoothing filter coefficients (first-order IIR) - same as vertical spectrum
constexpr float kSmoothingAlpha = 0.6f; // Temporal smoothing strength (higher = more stable, less responsive)
constexpr float kSmoothingBeta = 0.4f;  // Responsiveness to input changes (higher = more responsive)

// Visualizer calibration constants - same as vertical spectrum
constexpr int32_t kFftReferenceMagnitude = 60000000; // Q31 format for FFT output

// Static smoothing arrays for visual compression (optional time-averaging)
// Use same number of pixels as vertical spectrum for consistency
constexpr int32_t kMaxSpectrumPixels = 128; // Max display width (OLED is typically 128 pixels wide)
std::array<float, kMaxSpectrumPixels> horizontal_spectrum_smoothed_values{};
} // namespace

/// Render horizontal spectrum on OLED display using FFT
///
/// Algorithm:
/// 1. Compute FFT on most recent 512 audio samples (shared with other visualizers)
/// 2. Map vertical positions to frequencies using logarithmic scaling (low at bottom, high at top)
/// 3. For each vertical position, calculate magnitude at that frequency using FFT bin interpolation
/// 4. Apply visual compression: ((amplitude^0.5) * ((frequency/1000)^0.1)) * 1.0
/// 5. Apply smoothing filter (first-order IIR) to stabilize display
/// 6. Render mirrored line graph around center vertical axis (like standard spectrum but horizontal)
void renderVisualizerStereoLineSpectrum(oled_canvas::Canvas& canvas) {
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

	// Compute stereo FFT using shared helper function (with caching optimization)
	FFTResult fft_result = computeVisualizerStereoFFT();
	if (!fft_result.isValid || !fft_result.isStereo) {
		// Not enough samples, FFT config not available, or stereo not supported, draw empty
		return;
	}

	// Fixed reference magnitude for fixed-amplitude display aligned with VU meter
	// When VU meter shows clipping, spectrum should be at peak
	// FFT output is Q31 complex format, so magnitude is sqrt(r^2 + i^2) in Q31 range.
	constexpr int32_t k_fixed_reference_magnitude = kFftReferenceMagnitude;

	// Check for silence by examining a few representative bins
	// If all bins are very small, don't update display to avoid flicker from brief gaps
	// Previous horizontal spectrum frame remains visible
	if (fft_result.isSilent) {
		return;
	}

	// Clear the visualizer area before drawing
	canvas.clearAreaExact(k_graph_min_x, k_graph_min_y, k_graph_max_x, k_graph_max_y + 1);

	// Render centered stereo horizontal spectrum - left channel on left side, right channel on right side
	int32_t last_left_x = -1;
	int32_t last_right_x = -1;
	int32_t last_y = -1;
	bool is_first_point = true;

	// Map vertical positions to frequencies using modified logarithmic frequency scale
	// Low frequencies at bottom, high frequencies at top (like horizontal EQ)
	constexpr int32_t k_num_bins = 257;                                         // 257 bins (from FFT)
	constexpr int32_t k_num_vertical_pixels = k_graph_height;                   // Use full graph height
	constexpr float k_min_frequency = 20.0f;                                    // Start at 20 Hz for bass
	constexpr float k_max_frequency = static_cast<float>(::kSampleRate) / 2.0f; // Nyquist frequency

	// Precompute logarithmic scale constant: log10(sample_rate / 2 / 20)
	float log_scale_constant = std::log10(k_max_frequency / k_min_frequency);

	// Frequency compression exponent: 0.85 provides gentler compression of bass range
	// while keeping mids and highs readable and proportional
	constexpr float k_frequency_compression_exponent = 0.85f;

	// Center of display for mirrored line graph
	const int32_t center_x = (k_graph_min_x + k_graph_max_x) / 2;
	const int32_t max_half_width = (k_graph_max_x - k_graph_min_x) / 2;

	// Helper function to calculate magnitude for a channel
	auto calculateChannelMagnitude = [&](const ne10_fft_cpx_int32_t* channel_output, int32_t bin_index_low,
	                                     int32_t bin_index_high, float fraction) -> float {
		// Get magnitudes for both bins
		// Reduce DC bin (bin 0) influence to prevent low frequency artifacts
		int32_t magnitude_low = fastPythag(channel_output[bin_index_low].r, channel_output[bin_index_low].i);
		int32_t magnitude_high = fastPythag(channel_output[bin_index_high].r, channel_output[bin_index_high].i);
		if (bin_index_low == 0) {
			magnitude_low /= kDcBinReductionFactor; // Reduce DC bin influence by 75%
		}

		// Interpolate between the two bins
		return (static_cast<float>(magnitude_low) * (1.0f - fraction))
		       + (static_cast<float>(magnitude_high) * fraction);
	};

	for (int32_t vertical_pixel = 0; vertical_pixel < k_num_vertical_pixels; vertical_pixel++) {
		// Map vertical pixel to frequency using modified logarithmic scale with compression
		// Pixel 0 = bottom = low frequency, pixel k_num_vertical_pixels-1 = top = high frequency
		float normalized_y = static_cast<float>(vertical_pixel) / static_cast<float>(k_num_vertical_pixels - 1);
		float compressed_y = std::pow(normalized_y, k_frequency_compression_exponent);
		float frequency = k_min_frequency * std::pow(10.0f, compressed_y * log_scale_constant);

		// Map frequency to FFT bin index
		// Bin i represents frequency: f_i = i * sample_rate / 512
		// So: bin = frequency * 512 / sample_rate
		constexpr int32_t k_spectrum_fft_size = 512; // Match FFT size from visualizer_fft.cpp
		float bin_float = frequency * static_cast<float>(k_spectrum_fft_size) / static_cast<float>(::kSampleRate);

		// Use linear interpolation between adjacent bins to avoid stepping artifacts
		int32_t bin_index_low = static_cast<int32_t>(std::floor(bin_float));
		int32_t bin_index_high = bin_index_low + 1;
		float fraction = bin_float - static_cast<float>(bin_index_low);

		// Clamp bin indices to valid range
		bin_index_low = std::max(static_cast<int32_t>(0), std::min(bin_index_low, k_num_bins - 1));
		bin_index_high = std::max(static_cast<int32_t>(0), std::min(bin_index_high, k_num_bins - 1));

		// Calculate magnitudes for left and right channels
		float left_magnitude =
		    calculateChannelMagnitude(fft_result.output_left, bin_index_low, bin_index_high, fraction);
		float right_magnitude =
		    calculateChannelMagnitude(fft_result.output_right, bin_index_low, bin_index_high, fraction);

		// Apply music sweet-spot visual compression for both channels
		// Normalize amplitude to 0-1 range
		float left_amplitude = left_magnitude / static_cast<float>(k_fixed_reference_magnitude);
		float right_amplitude = right_magnitude / static_cast<float>(k_fixed_reference_magnitude);
		float left_display_value = applyVisualizerCompression(left_amplitude, frequency);
		float right_display_value = applyVisualizerCompression(right_amplitude, frequency);

		// Apply smoothing filter for stability (first-order IIR)
		// Use per-pixel smoothing instead of per-bin to avoid conflicts
		// Only use smoothing buffer when in stereo spectrum mode (conditional memory usage)
		if (vertical_pixel < kMaxSpectrumPixels
		    && visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerStereoLineSpectrum) {
			// For stereo, we could use separate smoothing for left/right, but for simplicity
			// we'll use the same smoothing buffer for both channels
			float combined_value = (left_display_value + right_display_value) * 0.5f;
			horizontal_spectrum_smoothed_values[vertical_pixel] =
			    (horizontal_spectrum_smoothed_values[vertical_pixel] * kSmoothingAlpha)
			    + (combined_value * kSmoothingBeta);
			float smoothed_value = horizontal_spectrum_smoothed_values[vertical_pixel];
			// Apply some channel separation while using shared smoothing
			left_display_value = (left_display_value * 0.7f) + (smoothed_value * 0.3f);
			right_display_value = (right_display_value * 0.7f) + (smoothed_value * 0.3f);
		}

		// Clamp display_values to valid range
		left_display_value = std::clamp(left_display_value, 0.0f, 1.0f);
		right_display_value = std::clamp(right_display_value, 0.0f, 1.0f);

		// Use left channel for left side, right channel for right side
		auto left_scaled_half_width = static_cast<int32_t>(left_display_value * static_cast<float>(max_half_width));
		auto right_scaled_half_width = static_cast<int32_t>(right_display_value * static_cast<float>(max_half_width));

		// Calculate Y position (vertical_pixel 0 = bottom, increasing upward)
		int32_t y = k_graph_max_y - vertical_pixel;

		// Calculate X positions - left channel controls left side, right channel controls right side
		int32_t left_x = center_x - left_scaled_half_width;
		int32_t right_x = center_x + right_scaled_half_width;

		// Clamp X coordinates to valid display range
		left_x = std::clamp(left_x, k_graph_min_x, center_x);
		right_x = std::clamp(right_x, center_x, k_graph_max_x);

		// Draw line from previous point to current point (both left and right sides)
		if (!is_first_point && last_y >= 0 && last_y != y) {
			// Draw left side line
			canvas.drawLine(last_left_x, last_y, left_x, y);
			// Draw right side line (mirrored)
			canvas.drawLine(last_right_x, last_y, right_x, y);
		}
		else if (is_first_point) {
			// First point, just draw pixels
			canvas.drawPixel(left_x, y);
			canvas.drawPixel(right_x, y);
			is_first_point = false;
		}

		last_left_x = left_x;
		last_right_x = right_x;
		last_y = y;
	}

	// Mark OLED as changed so it gets sent to display
	OLED::markChanged();
}

} // namespace deluge::hid::display
