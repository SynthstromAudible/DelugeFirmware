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

#include "visualizer_bar_spectrum.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "model/settings/runtime_feature_settings.h"
#include "visualizer_common.h"
#include "visualizer_fft.h"
#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

// Equalizer-specific constants and buffers
namespace {
// Smoothing filter coefficients (first-order IIR)
constexpr float kSmoothingAlpha = 0.75f; // Temporal smoothing strength (higher = more stable, less responsive)
constexpr float kSmoothingBeta = 0.25f;  // Responsiveness to input changes (higher = more responsive)

// Peak decay parameters
constexpr float kPeakDecayRate = 0.005f; // Decay rate per frame

// Visualizer calibration constants
constexpr int32_t kFftReferenceMagnitude = 60000000; // Q31 format for FFT output

// Static peak tracking arrays for equalizer visualizer
// Store peak heights in normalized 0-1 range for proper decay scaling
constexpr int32_t kEqualizerNumBars = 16;
std::array<float, kEqualizerNumBars> equalizer_peak_heights{};
std::array<float, kEqualizerNumBars> equalizer_peak_decay{};

// Static smoothing arrays for visual compression (optional time-averaging)
std::array<float, kEqualizerNumBars> equalizer_smoothed_values{};

// 16 frequency band center frequencies (Hz) - standard equalizer bands
// Band 1: 31 Hz (Sub-bass), 2: 50 Hz (Bass thump), 3: 80 Hz (Bass body), 4: 125 Hz (Upper bass),
// 5: 200 Hz (Low mids), 6: 315 Hz (Warmth), 7: 500 Hz (Midrange), 8: 800 Hz (Mid clarity),
// 9: 1.25 kHz (Presence), 10: 2 kHz (Presence), 11: 3.15 kHz (Upper mids), 12: 5 kHz (Clarity),
// 13: 8 kHz (High presence), 14: 12.5 kHz (Brilliance), 15: 16 kHz (Air), 16: 20 kHz (Ultrasonic)
constexpr std::array<float, kEqualizerNumBars> kEqualizerFrequencies = {
    31.0f,   50.0f,   80.0f,   125.0f,  200.0f,  315.0f,   500.0f,   800.0f,
    1250.0f, 2000.0f, 3150.0f, 5000.0f, 8000.0f, 12500.0f, 16000.0f, 20000.0f};
} // namespace

// Calculate frequency band range for equalizer bar
// Uses logarithmic spacing around center frequency (approximately 1/3 octave bands)
void calculateFrequencyBandRange(int32_t bar, float& lower_freq, float& upper_freq) {
	float center_freq = kEqualizerFrequencies[bar];
	if (bar == 0) {
		// First band: from 20 Hz to midpoint between band 1 (31 Hz) and band 2 (50 Hz)
		lower_freq = 20.0f;
		upper_freq = (kEqualizerFrequencies[bar] + kEqualizerFrequencies[bar + 1]) / 2.0f;
	}
	else if (bar == kEqualizerNumBars - 1) {
		// Last band: from midpoint between previous and center to 20kHz
		lower_freq = (kEqualizerFrequencies[bar - 1] + center_freq) / 2.0f;
		upper_freq = 20000.0f;
	}
	else {
		// Middle bands: range between midpoints of adjacent bands
		lower_freq = (kEqualizerFrequencies[bar - 1] + center_freq) / 2.0f;
		upper_freq = (center_freq + kEqualizerFrequencies[bar + 1]) / 2.0f;
	}
}

// Update peak tracking and draw peak indicator for equalizer bar
void updateAndDrawPeak(oled_canvas::Canvas& canvas, int32_t bar, float normalized_height, int32_t bar_left_x,
                       int32_t bar_right_x, int32_t k_graph_min_y, int32_t k_graph_max_y, int32_t k_graph_height,
                       uint32_t visualizer_mode) {
	// Only use peak tracking arrays when in equalizer mode (conditional memory usage)
	if (visualizer_mode != RuntimeFeatureStateVisualizer::VisualizerBarSpectrum) {
		return;
	}

	normalized_height = std::min(1.0f, normalized_height); // Clamp to 0-1 range

	if (normalized_height > equalizer_peak_heights[bar]) {
		// New peak reached - set peak to current height and reset decay
		equalizer_peak_heights[bar] = normalized_height;
		equalizer_peak_decay[bar] = 0.0f;
	}
	else {
		// Accumulate decay and apply squared decay formula
		// Peak decays using squared decay: peak = max(current, peak - decay^2)
		// This provides smooth exponential-like decay that slows down as peak approaches current value
		equalizer_peak_decay[bar] += kPeakDecayRate;
		equalizer_peak_heights[bar] = std::max(
		    normalized_height, equalizer_peak_heights[bar] - (equalizer_peak_decay[bar] * equalizer_peak_decay[bar]));
	}

	// Draw peak indicator as thicker horizontal line (3 pixels thick, matching reference)
	// Convert normalized peak height back to pixels for drawing
	if (equalizer_peak_heights[bar] > 0.0f) {
		auto peak_height_pixels =
		    static_cast<int32_t>(equalizer_peak_heights[bar] * static_cast<float>(k_graph_height));
		int32_t peak_y = k_graph_max_y - peak_height_pixels;
		peak_y = std::clamp(peak_y, k_graph_min_y, k_graph_max_y);

		// Draw 2-pixel thick horizontal line for peak indicator
		for (int32_t thickness = 0; thickness <= 1; thickness++) {
			int32_t draw_y = peak_y + thickness;
			if (draw_y >= k_graph_min_y && draw_y <= k_graph_max_y) {
				canvas.drawHorizontalLine(draw_y, bar_left_x, bar_right_x);
			}
		}
	}
}

/// Render visualizer equalizer on OLED display using FFT with 16 frequency bands (16 bars)
///
/// Algorithm:
/// 1. Compute FFT on most recent 512 audio samples (shared with spectrum visualizer)
/// 2. For each of 16 frequency bands, map to FFT bins using weighted interpolation:
///    - Each band covers a frequency range (lowerFreq to upperFreq)
///    - Bins overlapping the range contribute proportionally to their overlap
///    - Weighted average magnitude is calculated across all overlapping bins
/// 3. Apply visual compression: ((amplitude^0.5) * ((frequency/1000)^0.05)) * 1.1
///    - Compression exponent provides soft-knee dynamics compression
///    - Frequency term provides subtle treble emphasis (reduces bass/treble extremes)
///    - Amplitude boost increases overall visual amplitude by 10%
/// 4. Apply smoothing filter (first-order IIR) to stabilize display
/// 5. Track peak heights with squared decay for visual feedback
void renderVisualizerBarSpectrum(oled_canvas::Canvas& canvas) {
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

	// Bar layout constants: 16 bars with even margins and clean pixel alignment
	// Bar width: 5 px, Gap: 2 px, Margins: 7 px each side = 124 px total
	// Layout: 7px left margin + [5px bar + 2px gap] × 15 + 5px final bar + 7px right margin
	constexpr int32_t k_bar_width = 5;
	constexpr int32_t k_bar_gap = 2;
	constexpr int32_t k_equalizer_margin = 7;
	constexpr int32_t k_equalizer_content_start_x = k_graph_min_x + k_equalizer_margin;
	constexpr int32_t k_equalizer_content_end_x = k_graph_max_x - k_equalizer_margin;

	// Compute FFT using shared helper function (with caching optimization)
	FFTResult fft_result = computeVisualizerFFT();
	if (!fft_result.isValid) {
		// Not enough samples or FFT config not available, draw empty
		return;
	}

	// Fixed reference magnitude for fixed-amplitude display aligned with VU meter
	// When VU meter shows clipping, spectrum should be at peak
	// FFT output is Q31 complex format, so magnitude is sqrt(r^2 + i^2) in Q31 range.
	constexpr int32_t k_fixed_reference_magnitude = kFftReferenceMagnitude;

	// Check for silence by examining a few representative bins
	// If all bins are very small, don't update display to avoid flicker from brief gaps
	// Previous equalizer frame remains visible
	if (fft_result.isSilent) {
		return;
	}

	// Clear the visualizer area before drawing
	canvas.clearAreaExact(k_graph_min_x, k_graph_min_y, k_graph_max_x, k_graph_max_y + 1);

	// Calculate frequency resolution per bin
	constexpr int32_t k_spectrum_fft_size = 512; // Match FFT size from visualizer_fft.cpp
	float freq_resolution = static_cast<float>(::kSampleRate) / static_cast<float>(k_spectrum_fft_size);

	// Render 16 frequency bars
	for (int32_t bar = 0; bar < kEqualizerNumBars; bar++) {
		float center_freq = kEqualizerFrequencies[bar];

		// Calculate frequency range for this band
		float lower_freq = 0.0f;
		float upper_freq = 0.0f;
		calculateFrequencyBandRange(bar, lower_freq, upper_freq);

		// Calculate weighted average magnitude using FFT bin interpolation
		float avg_magnitude_float = calculateWeightedMagnitude(fft_result, lower_freq, upper_freq, freq_resolution);

		// Apply music sweet-spot visual compression
		// Normalize amplitude to 0-1 range
		float amplitude = avg_magnitude_float / static_cast<float>(k_fixed_reference_magnitude);
		float display_value = applyVisualizerCompression(amplitude, center_freq);

		// Apply smoothing filter for stability (first-order IIR)
		// Only use smoothing buffer when in equalizer mode (conditional memory usage)
		if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerBarSpectrum) {
			equalizer_smoothed_values[bar] =
			    (equalizer_smoothed_values[bar] * kSmoothingAlpha) + (display_value * kSmoothingBeta);
			display_value = equalizer_smoothed_values[bar];
		}
		// If not in equalizer mode, use display_value directly (skip smoothing)
		// This shouldn't happen since we're in renderVisualizerBarSpectrum, but handle gracefully

		// Clamp display_value to valid range and scale to graph height
		display_value = std::clamp(display_value, 0.0f, 1.0f);
		auto scaled_height = static_cast<int32_t>(display_value * static_cast<float>(k_graph_height));

		// Clamp scaled_height to graph height
		scaled_height = std::min(scaled_height, k_graph_height);

		// Calculate bar position (bar width: 5 px, gap: 2 px between bars)
		int32_t bar_left_x = k_equalizer_content_start_x + (bar * (k_bar_width + k_bar_gap));
		int32_t bar_right_x = bar_left_x + k_bar_width - 1;
		int32_t bar_bottom_y = k_graph_max_y;
		int32_t bar_top_y = k_graph_max_y - scaled_height;

		// Clamp bar coordinates to valid display range
		bar_left_x = std::clamp(bar_left_x, k_equalizer_content_start_x, k_equalizer_content_end_x);
		bar_right_x = std::clamp(bar_right_x, k_equalizer_content_start_x, k_equalizer_content_end_x);
		bar_top_y = std::clamp(bar_top_y, k_graph_min_y, k_graph_max_y);

		// Draw bar with checkered dither pattern using 2 pixel squares
		for (int32_t x = bar_left_x; x <= bar_right_x; x++) {
			for (int32_t y = bar_top_y; y <= bar_bottom_y; y++) {
				// Checkered pattern: draw pixel if (x + y) is even
				if ((x + y) % 2 == 0) {
					canvas.drawPixel(x, y);
				}
			}
		}

		// Update peak tracking and draw peak indicator
		float normalized_height = static_cast<float>(scaled_height) / static_cast<float>(k_graph_height);
		updateAndDrawPeak(canvas, bar, normalized_height, bar_left_x, bar_right_x, k_graph_min_y, k_graph_max_y,
		                  k_graph_height, visualizer_mode);
	}

	// Mark OLED as changed so it gets sent to display
	OLED::markChanged();
}

} // namespace deluge::hid::display
