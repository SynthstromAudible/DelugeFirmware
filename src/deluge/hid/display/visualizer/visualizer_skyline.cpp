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

#include "visualizer_skyline.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "playback/playback_handler.h"
#include "util/functions.h"
#include "visualizer_common.h"
#include "visualizer_fft.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace deluge::hid::display {
namespace {
// ------------------------------------------------------------
// Audio + smoothing constants
// ------------------------------------------------------------

constexpr float kReferenceBPM = 120.0f;

constexpr float kAmplitudeSmoothingAlpha = 0.6f;

constexpr float kAmplitudeSmoothingBeta = 0.4f;

// ------------------------------------------------------------
// Layout / visual constants
// ------------------------------------------------------------

// Buildings will be rendered as mirrored 8-band equalizer bars (16 columns)

constexpr int32_t kSkylineNumBands = 8;

constexpr int32_t kSkylineNumColumns = 16;

constexpr int32_t kNumGridLines = 6;

// Grid motion
constexpr float kBaseGridSpeed = 0.008f;    // match tunnel speed
constexpr float kGridOffsetMinValue = 0.0f; // Minimum value for grid offset normalization
constexpr float kGridOffsetMaxValue = 1.0f; // Maximum value for grid offset normalization

// Building height behaviour

constexpr float kBuildingMinHeightRatio = 0.20f; // % of skyline region

constexpr float kBuildingMaxHeightRatio = 1.00f;

// Building smoothing (per-band)

constexpr float kBuildingSmoothingAlpha = 0.7f;

constexpr float kBuildingSmoothingBeta = 0.3f;

// Silence detection constants (same as starfield)
constexpr int32_t kSilenceThreshold = 10;     // Q15 format - matches waveform visualizer
constexpr int32_t kSilenceCheckInterval = 16; // Check every Nth sample to reduce CPU usage

// Persistence constants (similar to tunnel visualizer)
constexpr uint32_t kBasePersistenceDurationFrames = 10; // Base duration to keep visualizer active after audio stops

// Band smoothing constants
constexpr float kSilenceBandDecayFactor = 0.9f; // Multiplier for band decay during silence

// Rendering constants
constexpr float kSunRadiusHeightRatio = 0.50f; // Maximum sun radius as ratio of region height
constexpr int32_t kSunRadiusWidthDivisor = 3;  // OLED width divisor for max sun radius calculation

constexpr int32_t kMinSunRadius = 6;
constexpr int32_t kAbsoluteMinSunRadius = 5;
constexpr int32_t kSunStripeGap = 3;
constexpr int32_t kBuildingBottomY = OLED_MAIN_HEIGHT_PIXELS / 2;
constexpr int32_t kPerspectiveRoadTopHalfWidth = OLED_MAIN_WIDTH_PIXELS / 6;
constexpr int32_t kPerspectiveRoadBottomHalfWidth = OLED_MAIN_WIDTH_PIXELS / 2;

// Equalizer / FFT visual compression reference

constexpr int32_t kFftReferenceMagnitude = 60000000; // matches equalizer

// 16-band equalizer center frequencies

constexpr int32_t kEqualizerNumBars = 16;

constexpr std::array<float, kEqualizerNumBars> kEqualizerFrequencies = {

    31.0f,   50.0f,   80.0f,   125.0f,  200.0f,  315.0f,   500.0f,   800.0f,

    1250.0f, 2000.0f, 3150.0f, 5000.0f, 8000.0f, 12500.0f, 16000.0f, 20000.0f};

// For Skyline we derive 8 bands by skipping every other band from the 16-band EQ.

// Maps to every other band from the 16-band equalizer for frequency distribution

constexpr std::array<int32_t, kSkylineNumBands> kSkylineSourceBandIndices = {

    0, 2, 4, 6, 8, 10, 12, 14};

static_assert(kSkylineNumBands == kSkylineSourceBandIndices.size());

// ------------------------------------------------------------
// State
// ------------------------------------------------------------

float g_smoothed_amplitude = 0.0f;

float g_grid_offset = 0.0f; // 0..1 scroll

uint32_t g_last_audio_time = 0; // Last time we had audio (for persistence)

bool g_initialized = false;

// Per-band smoothed values for skyline buildings (0..1)

std::array<float, kSkylineNumBands> g_skyline_band_values{};

std::array<float, kSkylineNumBands> g_skyline_band_smoothed{};

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

// 16-band helper: calculate band range like in visualizer_bar_spectrum.cpp

void calculateFrequencyBandRange16(int32_t bar, float& lower_freq, float& upper_freq) {
	// Bounds checking for safety
	if (bar < 0 || bar >= kEqualizerNumBars) {
		lower_freq = 0.0f;
		upper_freq = 0.0f;
		return;
	}

	float center_freq = kEqualizerFrequencies[bar];
	if (bar == 0) {
		lower_freq = 20.0f;
		upper_freq = (kEqualizerFrequencies[bar] + kEqualizerFrequencies[bar + 1]) / 2.0f;
	}

	else if (bar == kEqualizerNumBars - 1) {
		lower_freq = (kEqualizerFrequencies[bar - 1] + center_freq) / 2.0f;
		upper_freq = 20000.0f;
	}

	else {
		lower_freq = (kEqualizerFrequencies[bar - 1] + center_freq) / 2.0f;
		upper_freq = (center_freq + kEqualizerFrequencies[bar + 1]) / 2.0f;
	}
}

// Compute 8 EQ-style band values (0..1) for the skyline, using the same

// FFT / compression helpers as the main equalizer visualizer.

void updateSkylineBands() {
	FFTResult fft_result = computeVisualizerFFT();
	if (!fft_result.isValid || fft_result.isSilent) {
		// If FFT isn't valid or is silent, slowly decay the bands
		for (int i = 0; i < kSkylineNumBands; i++) {
			g_skyline_band_smoothed[i] *= kSilenceBandDecayFactor;

			g_skyline_band_values[i] = g_skyline_band_smoothed[i];
		}
		return;
	}
	constexpr int32_t k_fft_size = 512;
	float freq_resolution = static_cast<float>(::kSampleRate) / static_cast<float>(k_fft_size);

	for (int i = 0; i < kSkylineNumBands; i++) {
		int32_t srcIndex = kSkylineSourceBandIndices[i];

		// Defensive check: ensure srcIndex is within bounds for kEqualizerFrequencies array
		if (srcIndex < 0 || srcIndex >= kEqualizerNumBars) {
			continue; // Skip invalid band indices
		}

		float lower_freq = 0.0f;
		float upper_freq = 0.0f;
		calculateFrequencyBandRange16(srcIndex, lower_freq, upper_freq);
		float avg_magnitude = calculateWeightedMagnitude(fft_result, lower_freq, upper_freq, freq_resolution);
		float amplitude = avg_magnitude / static_cast<float>(kFftReferenceMagnitude);
		float display_value = applyVisualizerCompression(amplitude, kEqualizerFrequencies[srcIndex]);
		display_value = std::clamp(display_value, 0.0f, 1.0f);
		// Per-band smoothing for nicer skyline motion
		g_skyline_band_smoothed[i] =
		    g_skyline_band_smoothed[i] * kBuildingSmoothingAlpha + display_value * kBuildingSmoothingBeta;
		g_skyline_band_values[i] = std::clamp(g_skyline_band_smoothed[i], 0.0f, 1.0f);
	}
}

// ------------------------------------------------------------
// Rendering helpers
// ------------------------------------------------------------

void drawSun(oled_canvas::Canvas& canvas, int32_t min_x, int32_t max_x, int32_t region_top, int32_t region_bottom,
             float smoothedAmplitude) {
	// Bigger sun: use more of the region height
	const int32_t cx = OLED_MAIN_WIDTH_PIXELS / 2;
	const int32_t cy = (region_top + region_bottom) / 2;
	const int32_t region_height = region_bottom - region_top + 1;

	// Ensure region_height is valid to prevent division issues
	if (region_height <= 0) {
		return;
	}
	const int32_t max_radius_by_height = static_cast<int32_t>(region_height * kSunRadiusHeightRatio); // ~45% of region
	const int32_t max_radius_by_width =
	    static_cast<int32_t>(OLED_MAIN_WIDTH_PIXELS / kSunRadiusWidthDivisor); // a bit larger
	int32_t base_radius = std::min(max_radius_by_height, max_radius_by_width);
	if (base_radius < kMinSunRadius)
		base_radius = kMinSunRadius;
	// Set sun radius to base radius for consistent appearance
	int32_t radius = base_radius;
	radius = std::max<int32_t>(kAbsoluteMinSunRadius, radius);
	// Fixed stripe gap for consistent vaporwave look
	int32_t stripe_gap = kSunStripeGap;
	const int32_t max_y = OLED_MAIN_HEIGHT_PIXELS - 1;
	for (int32_t y = cy - radius; y <= cy + radius; y++) {
		if (y < region_top || y > region_bottom || y < 0 || y > max_y) {
			continue;
		}
		// Horizontal stripes
		if (std::abs((y - region_top) % stripe_gap) != 0) {
			continue;
		}
		int32_t dy = y - cy;
		int32_t r2 = radius * radius;
		int32_t dy2 = dy * dy;
		if (dy2 > r2) {
			continue;
		}
		int32_t dx = static_cast<int32_t>(std::sqrt(static_cast<float>(r2 - dy2)));
		int32_t x1 = cx - dx;
		int32_t x2 = cx + dx;
		x1 = std::max(min_x, std::min(x1, max_x));
		x2 = std::max(min_x, std::min(x2, max_x));
		if (x2 > x1) {
			canvas.drawLine(x1, y, x2, y);
		}
	}
}

// Buildings = mirrored 8-band EQ bars.

// Low frequencies are at both sides, highs clustered toward the center.

void drawBuildings(oled_canvas::Canvas& canvas, int32_t sky_top_boundary, int32_t region_top, int32_t region_bottom,
                   float /*smoothedAmplitude*/) {
	const int32_t region_height = region_bottom - region_top + 1;

	// Ensure region_height is valid to prevent division issues
	if (region_height <= 0) {
		return;
	}

	const int32_t min_building_height = static_cast<int32_t>(region_height * kBuildingMinHeightRatio);
	const int32_t max_building_height = static_cast<int32_t>(region_height * kBuildingMaxHeightRatio);
	// Column width for 16 mirrored columns (8 bands × 2 sides)
	const int32_t column_width = OLED_MAIN_WIDTH_PIXELS / kSkylineNumColumns;
	const int32_t half_width = OLED_MAIN_WIDTH_PIXELS / 2;
	// Draw buildings from left and right sides, mirroring the 8 bands
	for (int32_t band = 0; band < kSkylineNumBands; band++) {
		float band_value = g_skyline_band_values[band];
		// Building height scales with amplitude, with some minimum height
		int32_t building_height =
		    min_building_height + static_cast<int32_t>(band_value * (max_building_height - min_building_height));
		// Buildings start at OLED center and extend upward to the top
		int32_t building_bottom = kBuildingBottomY; // Center of OLED
		int32_t building_top = building_bottom - building_height + 1;
		// Clamp to screen top
		if (building_top < 0) {
			building_top = 0;
		}

		// Left side building (mirrored)
		{
			int32_t x1 = band * column_width;
			int32_t x2 = x1 + column_width - 1;
			// Fill the building area with alternating dither pattern
			bool use_dither = (band % 2 == 0); // Alternate based on band index
			if (use_dither) {
				// Dithered pattern: checkered pattern like equalizer
				for (int32_t x = x1; x <= x2; x++) {
					for (int32_t y = building_top; y <= building_bottom; y++) {
						// Checkered pattern: draw pixel if (x + y) is even
						if ((x + y) % 2 == 0) {
							canvas.drawPixel(x, y);
						}
					}
				}
			}
			else {
				// Solid fill
				for (int32_t y = building_top; y <= building_bottom; y++) {
					canvas.drawLine(x1, y, x2, y);
				}
			}
		}

		// Right side building (mirrored)
		{
			int32_t x1 = OLED_MAIN_WIDTH_PIXELS - (band + 1) * column_width;
			int32_t x2 = x1 + column_width - 1;
			// Fill the building area with alternating dither pattern
			bool use_dither = (band % 2 == 0); // Alternate based on band index
			if (use_dither) {
				// Dithered pattern: checkered pattern like equalizer
				for (int32_t x = x1; x <= x2; x++) {
					for (int32_t y = building_top; y <= building_bottom; y++) {
						// Checkered pattern: draw pixel if (x + y) is even
						if ((x + y) % 2 == 0) {
							canvas.drawPixel(x, y);
						}
					}
				}
			}
			else {
				// Solid fill
				for (int32_t y = building_top; y <= building_bottom; y++) {
					canvas.drawLine(x1, y, x2, y);
				}
			}
		}
	}
}

// Tunnel-inspired perspective road:

// Lines converge to a center point, with spacing that tightens near the horizon.

// The far end is approximately the width of the sun.

void drawPerspectiveRoad(oled_canvas::Canvas& canvas, int32_t region_top, int32_t region_bottom, float gridOffset) {
	const int32_t cx = OLED_MAIN_WIDTH_PIXELS / 2;
	const int32_t region_height = region_bottom - region_top;
	if (region_height <= 0) {
		return;
	}
	// Approx top width ~= sun width (sun ~1/3 screen, so half of that)
	const int32_t top_half_width = kPerspectiveRoadTopHalfWidth;
	const int32_t bottom_half_width = kPerspectiveRoadBottomHalfWidth;
	// Normalize offset
	float offset = std::fmod(gridOffset, kGridOffsetMaxValue);
	if (offset < kGridOffsetMinValue) {
		offset += kGridOffsetMaxValue;
	}
	for (int32_t i = 0; i < kNumGridLines; i++) {
		// Base depth in [0,1]
		float base_t = static_cast<float>(i) / static_cast<float>(kNumGridLines);
		// Animate forward motion by moving depth with offset
		float t = base_t + offset;
		if (t > kGridOffsetMaxValue) {

			t -= kGridOffsetMaxValue;
		}
		// Non-linear mapping: more spacing near the viewer (bottom),
		// tighter lines near the horizon (top).
		float depth = std::sqrt(t); // tunnel-style feel
		// Y position: depth 0 => bottom, depth 1 => top
		int32_t y = region_bottom - static_cast<int32_t>(depth * region_height);
		if (y < region_top || y > region_bottom) {
			continue;
		}
		// Perspective width: wide at the bottom, narrow at the top.
		float inv_depth = 1.0f - depth;
		int32_t half_w = top_half_width + static_cast<int32_t>((bottom_half_width - top_half_width) * inv_depth);
		int32_t x1 = cx - half_w;
		int32_t x2 = cx + half_w;
		if (x1 < 0)
			x1 = 0;
		if (x2 >= OLED_MAIN_WIDTH_PIXELS)
			x2 = OLED_MAIN_WIDTH_PIXELS - 1;
		canvas.drawLine(x1, y, x2, y);
	}
}

// ------------------------------------------------------------
// Main render function
// ------------------------------------------------------------

void renderVisualizerSkylineInternal(oled_canvas::Canvas& canvas) {
	// Initialize on first run
	if (!g_initialized) {
		g_smoothed_amplitude = 0.0f;
		g_grid_offset = 0.0f;
		g_initialized = true;
		std::fill(g_skyline_band_values.begin(), g_skyline_band_values.end(), 0.0f);
		std::fill(g_skyline_band_smoothed.begin(), g_skyline_band_smoothed.end(), 0.0f);
	}
	// Check for silence and manage persistence
	uint32_t sample_count = Visualizer::visualizer_sample_count.load(std::memory_order_acquire);
	if (sample_count < 2) {
		// Not enough samples yet, draw empty
		return;
	}
	// Use frame counter for timing (similar to tunnel visualizer)
	static uint32_t frame_counter = 0;
	frame_counter++;
	uint32_t current_time = frame_counter;
	int32_t sample_magnitude =
	    std::abs(Visualizer::visualizer_sample_buffer[sample_count / 2 % Visualizer::kVisualizerBufferSize]);
	if (sample_magnitude < kSilenceThreshold) {
		// Check a few more samples to confirm silence
		bool is_silent = true;
		uint32_t read_start_pos = getVisualizerReadStartPos(sample_count);
		for (uint32_t i = 0; i < sample_count && i < kAmplitudeSampleCount; i += kSilenceCheckInterval) {
			uint32_t buffer_index = (read_start_pos + i) % Visualizer::kVisualizerBufferSize;
			int32_t mag = std::abs(Visualizer::visualizer_sample_buffer[buffer_index]);
			if (mag >= kSilenceThreshold) {
				is_silent = false;
				break;
			}
		}
		if (is_silent) {
			// Check if we should still show persistence
			uint32_t time_since_last_audio = current_time - g_last_audio_time;
			if (time_since_last_audio > kBasePersistenceDurationFrames) {
				// Persistence expired, don't update display to avoid flicker from brief gaps
				// Previous skyline frame remains visible
				return;
			}
			// Continue with persistence - don't return
		}
		else {
			// Audio detected, update g_last_audio_time
			g_last_audio_time = current_time;
		}
	}
	else {
		// Audio detected, update g_last_audio_time
		g_last_audio_time = current_time;
	}
	// Update audio analysis for skyline band calculations
	float current_amplitude = ::deluge::hid::display::computeCurrentAmplitude();
	g_smoothed_amplitude =
	    g_smoothed_amplitude * kAmplitudeSmoothingAlpha + current_amplitude * kAmplitudeSmoothingBeta;
	updateSkylineBands();
	// Calculate tempo-based road speed (BPM determines road movement rate)
	float bpm = playbackHandler.calculateBPMForDisplay();
	// Prevent division by zero with minimum BPM
	bpm = std::max(bpm, 1.0f);
	float grid_speed = kBaseGridSpeed * (bpm / kReferenceBPM);
	// Update road motion (continuous forward scroll)
	g_grid_offset -= grid_speed;
	if (g_grid_offset < 0.0f) {
		g_grid_offset += 1.0f;
	}
	// Layout: split screen into sky (top) and skyline (bottom) regions
	// Raised buildings: skyline starts higher so they are closer to the sun.
	const int32_t skyline_top = OLED_MAIN_HEIGHT_PIXELS / 2; // Road in bottom half

	const int32_t skyline_bottom = OLED_MAIN_HEIGHT_PIXELS - 1;
	const int32_t sky_top = 0;
	const int32_t sky_bottom = skyline_top - 1; // Buildings and sun in top half
	// Draw skyline region (buildings + perspective road)
	drawBuildings(canvas, 0, skyline_top, skyline_bottom, g_smoothed_amplitude);
	drawPerspectiveRoad(canvas, skyline_top, skyline_bottom, g_grid_offset);
	// Draw sky region (sun) - after buildings so it appears in front
	drawSun(canvas, 0, OLED_MAIN_WIDTH_PIXELS - 1, sky_top, sky_bottom, g_smoothed_amplitude);
}

} // namespace

void renderVisualizerSkyline(oled_canvas::Canvas& canvas) {
	renderVisualizerSkylineInternal(canvas);
}
} // namespace deluge::hid::display
