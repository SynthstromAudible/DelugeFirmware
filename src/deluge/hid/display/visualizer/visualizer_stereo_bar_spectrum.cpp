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

#include "visualizer_stereo_bar_spectrum.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "model/settings/runtime_feature_settings.h"
#include "visualizer_common.h"
#include "visualizer_fft.h"
#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

// Stereo Equalizer-specific constants and buffers
namespace {
// Smoothing filter coefficients (first-order IIR) - same as vertical equalizer
constexpr float kSmoothingAlpha = 0.75f; // Temporal smoothing strength (higher = more stable, less responsive)
constexpr float kSmoothingBeta = 0.25f;  // Responsiveness to input changes (higher = more responsive)

// Peak decay parameters - same as vertical equalizer
constexpr float kPeakDecayRate = 0.005f; // Decay rate per frame

// Visualizer calibration constants - same as vertical equalizer
constexpr int32_t kFftReferenceMagnitude = 60000000; // Q31 format for FFT output

// Static peak tracking arrays for stereo equalizer visualizer
// Store peak heights in normalized 0-1 range for proper decay scaling
constexpr int32_t kStereoEQNumBands = 8;
constexpr int32_t kStereoEQPeakArraySize = kStereoEQNumBands * 2; // Double size for stereo (left + right)
std::array<float, kStereoEQPeakArraySize> stereo_eq_peak_heights{};
std::array<float, kStereoEQPeakArraySize> stereo_eq_peak_decay{};

// Static smoothing arrays for visual compression (optional time-averaging)
std::array<float, kStereoEQNumBands> stereo_eq_smoothed_values{};

// 8 frequency bands (combining pairs from the 16-band equalizer)
// Each band combines two adjacent bands from the original 16-band setup
// Band 0: 31 Hz + 50 Hz (Sub-bass), 1: 80 Hz + 125 Hz (Bass), 2: 200 Hz + 315 Hz (Low mids),
// 3: 500 Hz + 800 Hz (Midrange), 4: 1.25 kHz + 2 kHz (Presence), 5: 3.15 kHz + 5 kHz (Upper mids),
// 6: 8 kHz + 12.5 kHz (High presence), 7: 16 kHz + 20 kHz (Ultrasonic)
constexpr std::array<float, kStereoEQNumBands> kStereoEQFrequencies = {
    40.5f,    // (31 + 50) / 2
    102.5f,   // (80 + 125) / 2
    257.5f,   // (200 + 315) / 2
    650.0f,   // (500 + 800) / 2
    1625.0f,  // (1250 + 2000) / 2
    4075.0f,  // (3150 + 5000) / 2
    10250.0f, // (8000 + 12500) / 2
    18000.0f  // (16000 + 20000) / 2
};
} // namespace

// Calculate frequency band range for stereo equalizer bar
// Uses the same logarithmic spacing approach as the vertical equalizer but for 8 bands
void calculateStereoEQFrequencyBandRange(int32_t band, float& lower_freq, float& upper_freq) {
	float center_freq = kStereoEQFrequencies[band];
	if (band == 0) {
		// First band: from 20 Hz to midpoint between band 0 (40.5 Hz) and band 1 (102.5 Hz)
		lower_freq = 20.0f;
		upper_freq = (kStereoEQFrequencies[band] + kStereoEQFrequencies[band + 1]) / 2.0f;
	}
	else if (band == kStereoEQNumBands - 1) {
		// Last band: from midpoint between previous and center to 20kHz
		lower_freq = (kStereoEQFrequencies[band - 1] + center_freq) / 2.0f;
		upper_freq = 20000.0f;
	}
	else {
		// Middle bands: range between midpoints of adjacent bands
		lower_freq = (kStereoEQFrequencies[band - 1] + center_freq) / 2.0f;
		upper_freq = (center_freq + kStereoEQFrequencies[band + 1]) / 2.0f;
	}
}

// Update peak tracking and draw peak indicator for stereo equalizer bar
void updateAndDrawStereoEQPeak(oled_canvas::Canvas& canvas, int32_t band, float normalized_height, int32_t centerX,
                               int32_t maxBarHalfWidth, int32_t bandTopY, int32_t bandBottomY, int32_t k_graph_min_x,
                               int32_t k_graph_max_x, int32_t k_graph_min_y, int32_t k_graph_max_y,
                               int32_t k_graph_height, uint32_t visualizer_mode) {
	// Only use peak tracking arrays when in stereo equalizer mode (conditional memory usage)
	if (visualizer_mode != RuntimeFeatureStateVisualizer::VisualizerStereoBarSpectrum) {
		return;
	}

	normalized_height = std::min(1.0f, normalized_height); // Clamp to 0-1 range

	if (normalized_height > stereo_eq_peak_heights[band]) {
		// New peak reached - set peak to current height and reset decay
		stereo_eq_peak_heights[band] = normalized_height;
		stereo_eq_peak_decay[band] = 0.0f;
	}
	else {
		// Accumulate decay and apply squared decay formula
		// Peak decays using squared decay: peak = max(current, peak - decay^2)
		// This provides smooth exponential-like decay that slows down as peak approaches current value
		stereo_eq_peak_decay[band] += kPeakDecayRate;
		stereo_eq_peak_heights[band] =
		    std::max(normalized_height,
		             stereo_eq_peak_heights[band] - (stereo_eq_peak_decay[band] * stereo_eq_peak_decay[band]));
	}

	// Draw peak indicator as thicker horizontal line (3 pixels thick, matching reference)
	// Convert normalized peak height back to pixels for drawing
	if (stereo_eq_peak_heights[band] > 0.0f) {
		auto peak_width_pixels =
		    static_cast<int32_t>(stereo_eq_peak_heights[band] * static_cast<float>(maxBarHalfWidth));
		int32_t peak_left_x = centerX - peak_width_pixels;
		int32_t peak_right_x = centerX + peak_width_pixels;
		peak_left_x = std::clamp(peak_left_x, k_graph_min_x, centerX);
		peak_right_x = std::clamp(peak_right_x, centerX, k_graph_max_x);

		// Draw 2-pixel thick horizontal line for peak indicator
		for (int32_t thickness = 0; thickness <= 1; thickness++) {
			int32_t draw_y = bandTopY + thickness;
			if (draw_y >= bandTopY && draw_y <= bandBottomY) {
				canvas.drawHorizontalLine(draw_y, peak_left_x, peak_right_x);
			}
		}
	}
}

/// Render horizontal mirrored 8-band equalizer on OLED display using FFT
///
/// Algorithm:
/// 1. Compute FFT on most recent 512 audio samples (shared with other visualizers)
/// 2. For each of 8 frequency bands, map to FFT bins using weighted interpolation:
///    - Each band covers a frequency range (lowerFreq to upperFreq)
///    - Bins overlapping the range contribute proportionally to their overlap
///    - Weighted average magnitude is calculated across all overlapping bins
/// 3. Apply visual compression: ((amplitude^0.5) * ((frequency/1000)^0.05)) * 1.1
///    - Compression exponent provides soft-knee dynamics compression
///    - Frequency term provides subtle treble emphasis (reduces bass/treble extremes)
///    - Amplitude boost increases overall visual amplitude by 10%
/// 4. Apply smoothing filter (first-order IIR) to stabilize display
/// 5. Track peak heights with squared decay for visual feedback
/// 6. Render horizontal bars that mirror around center vertical axis
void renderVisualizerStereoBarSpectrum(oled_canvas::Canvas& canvas) {
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

	// Stereo equalizer layout constants: 8 horizontal bands with even margins and clean pixel alignment
	// Layout: 2px top margin + [4px band + 1px gap] × 7 + 4px final band + 2px bottom margin
	// Total: 8 bands × 4px = 32px, 7 gaps × 1px = 7px, 4px margins = 4px, total = 43px
	constexpr int32_t k_band_height = 4;
	constexpr int32_t k_band_gap = 1;
	constexpr int32_t k_eq_margin = 2; // Top and bottom margins
	constexpr int32_t k_eq_content_start_y = k_graph_min_y + k_eq_margin;
	constexpr int32_t k_eq_content_end_y = k_graph_max_y - k_eq_margin;

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
	// Previous horizontal equalizer frame remains visible
	if (fft_result.isSilent) {
		return;
	}

	// Clear the visualizer area before drawing
	canvas.clearAreaExact(k_graph_min_x, k_graph_min_y, k_graph_max_x, k_graph_max_y + 1);

	// Calculate frequency resolution per bin
	constexpr int32_t k_spectrum_fft_size = 512; // Match FFT size from visualizer_fft.cpp
	float freq_resolution = static_cast<float>(::kSampleRate) / static_cast<float>(k_spectrum_fft_size);

	// Center of display for EQ bars
	const int32_t center_x = (k_graph_min_x + k_graph_max_x) / 2;
	const int32_t max_bar_half_width = (k_graph_max_x - k_graph_min_x) / 2;

	// Render 8 horizontal frequency bands - each band uses both left and right channel data
	for (int32_t band = 0; band < kStereoEQNumBands; band++) {
		float center_freq = kStereoEQFrequencies[band];

		// Calculate frequency range for this band
		float lower_freq = 0.0f;
		float upper_freq = 0.0f;
		calculateStereoEQFrequencyBandRange(band, lower_freq, upper_freq);

		// Calculate weighted average magnitude for left and right channels
		FFTResult left_channel_fft{nullptr, nullptr, nullptr, true, false, false};
		left_channel_fft.output = fft_result.output_left;
		float left_avg_magnitude =
		    calculateWeightedMagnitude(left_channel_fft, lower_freq, upper_freq, freq_resolution);

		FFTResult right_channel_fft{nullptr, nullptr, nullptr, true, false, false};
		right_channel_fft.output = fft_result.output_right;
		float right_avg_magnitude =
		    calculateWeightedMagnitude(right_channel_fft, lower_freq, upper_freq, freq_resolution);

		// Apply music sweet-spot visual compression for both channels
		float left_amplitude = left_avg_magnitude / static_cast<float>(k_fixed_reference_magnitude);
		float right_amplitude = right_avg_magnitude / static_cast<float>(k_fixed_reference_magnitude);
		float left_display_value = applyVisualizerCompression(left_amplitude, center_freq);
		float right_display_value = applyVisualizerCompression(right_amplitude, center_freq);

		// Apply smoothing filter for stability (first-order IIR)
		if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerStereoBarSpectrum) {
			// Use separate smoothing for left and right channels
			stereo_eq_smoothed_values[band] = (stereo_eq_smoothed_values[band] * kSmoothingAlpha)
			                                  + ((left_display_value + right_display_value) * 0.5f * kSmoothingBeta);
			float smoothed_value = stereo_eq_smoothed_values[band];
			// Apply smoothing to both channels
			left_display_value = (left_display_value * 0.7f) + (smoothed_value * 0.3f);
			right_display_value = (right_display_value * 0.7f) + (smoothed_value * 0.3f);
		}

		// Clamp display_values to valid range
		left_display_value = std::clamp(left_display_value, 0.0f, 1.0f);
		right_display_value = std::clamp(right_display_value, 0.0f, 1.0f);

		// Calculate bar extents using separate scaling for left and right
		auto left_scaled_width = static_cast<int32_t>(left_display_value * static_cast<float>(max_bar_half_width));
		auto right_scaled_width = static_cast<int32_t>(right_display_value * static_cast<float>(max_bar_half_width));

		// Calculate band position (bands stacked vertically from bottom to top)
		int32_t band_bottom_y = k_eq_content_end_y - (band * (k_band_height + k_band_gap));
		int32_t band_top_y = band_bottom_y - k_band_height + 1;

		// Clamp band coordinates to valid display range
		band_top_y = std::clamp(band_top_y, k_eq_content_start_y, k_eq_content_end_y);
		band_bottom_y = std::clamp(band_bottom_y, k_eq_content_start_y, k_eq_content_end_y);

		// Calculate bar extents - left channel extends left from center, right channel extends right from center
		int32_t bar_left_x = center_x - left_scaled_width;
		int32_t bar_right_x = center_x + right_scaled_width;

		// Clamp bar coordinates to valid display range
		bar_left_x = std::clamp(bar_left_x, k_graph_min_x, center_x);
		bar_right_x = std::clamp(bar_right_x, center_x, k_graph_max_x);

		// Draw bar with checkered dither pattern using 2 pixel squares
		for (int32_t y = band_top_y; y <= band_bottom_y; y++) {
			for (int32_t x = bar_left_x; x <= bar_right_x; x++) {
				// Checkered pattern: draw pixel if (x + y) is even
				if ((x + y) % 2 == 0) {
					canvas.drawPixel(x, y);
				}
			}
		}

		// Update peak tracking and draw peak indicator (stereo-aware)
		// Use separate peak tracking for left and right channels
		if (stereo_eq_peak_heights[band] < left_display_value) {
			stereo_eq_peak_heights[band] = left_display_value;
			stereo_eq_peak_decay[band] = 0.0f;
		}
		else {
			stereo_eq_peak_decay[band] += kPeakDecayRate;
			stereo_eq_peak_heights[band] =
			    std::max(left_display_value,
			             stereo_eq_peak_heights[band] - (stereo_eq_peak_decay[band] * stereo_eq_peak_decay[band]));
		}

		// Draw left channel peak indicator
		if (stereo_eq_peak_heights[band] > 0.0f) {
			auto peak_left_width =
			    static_cast<int32_t>(stereo_eq_peak_heights[band] * static_cast<float>(max_bar_half_width));
			int32_t peak_left_x = center_x - peak_left_width;
			peak_left_x = std::clamp(peak_left_x, k_graph_min_x, center_x);

			// Draw 2-pixel thick horizontal line for left peak indicator
			for (int32_t thickness = 0; thickness <= 1; thickness++) {
				int32_t draw_y = band_top_y + thickness;
				if (draw_y >= band_top_y && draw_y <= band_bottom_y) {
					canvas.drawHorizontalLine(draw_y, peak_left_x, center_x);
				}
			}
		}

		// Separate peak tracking for right channel
		int32_t right_peak_index = band + kStereoEQNumBands; // Use separate indices for right channel
		if (right_peak_index < kStereoEQPeakArraySize) {
			if (stereo_eq_peak_heights[right_peak_index] < right_display_value) {
				stereo_eq_peak_heights[right_peak_index] = right_display_value;
				stereo_eq_peak_decay[right_peak_index] = 0.0f;
			}
			else {
				stereo_eq_peak_decay[right_peak_index] += kPeakDecayRate;
				stereo_eq_peak_heights[right_peak_index] =
				    std::max(right_display_value,
				             stereo_eq_peak_heights[right_peak_index]
				                 - (stereo_eq_peak_decay[right_peak_index] * stereo_eq_peak_decay[right_peak_index]));
			}

			// Draw right channel peak indicator
			if (stereo_eq_peak_heights[right_peak_index] > 0.0f) {
				auto peak_right_width = static_cast<int32_t>(stereo_eq_peak_heights[right_peak_index]
				                                             * static_cast<float>(max_bar_half_width));
				int32_t peak_right_x = center_x + peak_right_width;
				peak_right_x = std::clamp(peak_right_x, center_x, k_graph_max_x);

				// Draw 2-pixel thick horizontal line for right peak indicator
				for (int32_t thickness = 0; thickness <= 1; thickness++) {
					int32_t draw_y = band_top_y + thickness;
					if (draw_y >= band_top_y && draw_y <= band_bottom_y) {
						canvas.drawHorizontalLine(draw_y, center_x, peak_right_x);
					}
				}
			}
		}
	}

	// Mark OLED as changed so it gets sent to display
	OLED::markChanged();
}

} // namespace deluge::hid::display
