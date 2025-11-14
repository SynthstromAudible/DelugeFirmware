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

#include "visualizer_waveform.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

// Waveform-specific constants
namespace {
// Visualizer calibration constants
constexpr int32_t kWaveformReferenceMagnitude = 250; // Q15 format
constexpr int32_t kWaveformSilenceThreshold = 10;    // Q15 format

// Visualizer update and display constants
constexpr uint32_t kMaxDisplaySamples = 62; // Balanced for longer audio history while maintaining coarse style

// Format conversion and display constants
constexpr int32_t kSilenceCheckInterval = 16; // Interval for checking silence in loops (reduces CPU usage)
} // namespace

/// Render visualizer waveform on OLED display
void renderVisualizerWaveform(oled_canvas::Canvas& canvas) {
	constexpr int32_t k_display_width = OLED_MAIN_WIDTH_PIXELS;
	constexpr int32_t k_display_height = OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL;
	constexpr int32_t k_center_y = OLED_MAIN_TOPMOST_PIXEL + (k_display_height / 2);
	constexpr int32_t k_margin = kDisplayMargin;
	constexpr int32_t k_graph_min_x = k_margin;
	constexpr int32_t k_graph_max_x = k_display_width - k_margin - 1;
	constexpr int32_t k_graph_height = k_display_height - (k_margin * 2);

	// Read sample count atomically (single read is safe)
	uint32_t sample_count = 0;
	sample_count = Visualizer::visualizer_sample_count.load(std::memory_order_acquire);
	if (sample_count < 2) {
		// Not enough samples yet, draw empty
		return;
	}

	// Determine how many samples to display - use fewer samples for waveform-style sparse display
	uint32_t num_samples_to_display = 0;
	num_samples_to_display = std::min(sample_count, kMaxDisplaySamples);

	// Calculate step size for downsampling if we have more samples than pixels (integer arithmetic)
	uint32_t step_size = 0;
	uint32_t remainder = 0;
	step_size = (sample_count > kMaxDisplaySamples) ? (sample_count / kMaxDisplaySamples) : 1;
	remainder = (sample_count > kMaxDisplaySamples) ? (sample_count % kMaxDisplaySamples) : 0;

	// Start reading from write position and work backwards to get most recent samples
	uint32_t read_start_pos = getVisualizerReadStartPos(sample_count);

	// Fixed reference amplitude for fixed-amplitude display aligned with VU meter
	// When VU meter shows clipping, waveform should reach peak
	// Samples are in Q15 format (range ~-32768 to +32768)
	// Value determined empirically to align waveform peaks with VU meter clipping indication
	constexpr int32_t k_fixed_reference_magnitude = kWaveformReferenceMagnitude;

	// Check for silence by examining a few representative samples
	// If all samples are very small, skip display update to avoid flicker
	constexpr int32_t k_silence_threshold = kWaveformSilenceThreshold;
	int32_t sample_magnitude = 0;
	sample_magnitude = std::abs(
	    Visualizer::visualizer_sample_buffer[(read_start_pos + sample_count / 2) % Visualizer::kVisualizerBufferSize]);
	if (sample_magnitude < k_silence_threshold) {
		// Check a few more samples to confirm silence
		bool is_silent = true;
		for (uint32_t i = 0; i < num_samples_to_display; i += kSilenceCheckInterval) {
			uint32_t buffer_index = 0;
			buffer_index = (read_start_pos + i) % Visualizer::kVisualizerBufferSize;
			int32_t mag = 0;
			mag = std::abs(Visualizer::visualizer_sample_buffer[buffer_index]);
			if (mag >= k_silence_threshold) {
				is_silent = false;
				break;
			}
		}
		if (is_silent) {
			// Don't update display during silence to avoid flicker from brief gaps
			// Previous waveform frame remains visible
			return;
		}
	}

	// Clear the visualizer area before drawing to prevent ghosting from previous frames
	canvas.clearAreaExact(k_graph_min_x, OLED_MAIN_TOPMOST_PIXEL + k_margin, k_graph_max_x,
	                      OLED_MAIN_TOPMOST_PIXEL + k_display_height - k_margin + 1);

	// Draw waveform - spread samples evenly across full width
	// Reset drawing state to prevent connecting to previous frame (fixes vertical line at start)
	uint32_t sample_index = 0;
	uint32_t remainder_accumulator = 0;
	int32_t last_x = -1;
	int32_t last_y = -1;
	bool is_first_point = true;

	// Ensure num_samples_to_display is not zero (defensive programming)
	if (num_samples_to_display == 0) {
		return;
	}

	for (uint32_t i = 0; i < num_samples_to_display; i++) {
		// Calculate buffer index directly to avoid nested loop
		uint32_t buffer_index = (read_start_pos + sample_index) % Visualizer::kVisualizerBufferSize;
		// Get sample value
		int32_t sample = Visualizer::visualizer_sample_buffer[buffer_index];

		// Calculate Y position using compressed amplitude scaling (center at kCenterY)
		// Apply 2:1 visual compression to match spectrum/equalizer visualizers
		// Scale sample from [-kFixedReferenceMagnitude, +kFixedReferenceMagnitude] to display height
		// Positive samples go up from center, negative samples go down
		int32_t scaled_height = 0;
		if (sample != 0) {
			// Apply 2:1 compression: normalize, compress with square root, then scale
			// Use absolute value for compression, then restore sign
			float abs_sample = 0.0f;
			abs_sample = static_cast<float>(std::abs(sample));
			float normalized_amplitude = abs_sample / static_cast<float>(k_fixed_reference_magnitude);
			// Clamp to valid range
			normalized_amplitude = std::clamp(normalized_amplitude, 0.0f, 1.0f);
			// Apply 2:1 compression (square root)
			float compressed_amplitude = 0.0f;
			compressed_amplitude = std::pow(normalized_amplitude, 0.5f);
			// Scale to display height and restore sign
			float max_height = 0.0f;
			max_height = static_cast<float>(k_graph_height) / 2.0f;
			float compressed_height = compressed_amplitude * max_height;
			scaled_height = static_cast<int32_t>(compressed_height);
			// Restore original sign
			if (sample < 0) {
				scaled_height = -scaled_height;
			}
		}

		// Convert to pixel Y position (waveform: center at k_center_y, positive samples go up, negative go down)
		// When sample is 0, y should be at center (k_center_y)
		// When sample reaches +kFixedReferenceMagnitude, y should be at top (k_graph_min_y)
		// When sample reaches -kFixedReferenceMagnitude, y should be at bottom (k_graph_max_y)
		// Clamp scaled_height to prevent overflow
		scaled_height = std::clamp(scaled_height, -(k_graph_height / 2), (k_graph_height / 2));
		int32_t y = 0;
		y = k_center_y - scaled_height;
		// Clamp to valid display range
		y = std::clamp(y, static_cast<int32_t>(OLED_MAIN_TOPMOST_PIXEL + k_margin),
		               static_cast<int32_t>(OLED_MAIN_TOPMOST_PIXEL + k_display_height - k_margin - 1));

		// Calculate X position - spread samples across full width for coarse oscilloscope style
		int32_t x = 0;
		x = k_graph_min_x + static_cast<int32_t>((i * (k_graph_max_x - k_graph_min_x + 1)) / num_samples_to_display);
		// Ensure we don't exceed bounds
		x = std::min(x, k_graph_max_x);
		if (i == num_samples_to_display - 1) {
			x = k_graph_max_x; // Ensure last point reaches the end
		}

		// Draw line from previous point to current point
		// Skip connecting first point to avoid vertical line from previous frame
		if (!is_first_point && last_x >= 0 && last_x != x) {
			canvas.drawLine(last_x, last_y, x, y);
		}
		else if (is_first_point) {
			// First point of this frame, just draw a pixel (don't connect to previous frame)
			canvas.drawPixel(x, y);
			is_first_point = false;
		}

		last_x = x;
		last_y = y;

		// Advance sample index using integer-only math with remainder accumulation
		sample_index += step_size;
		remainder_accumulator += remainder;
		if (remainder_accumulator >= kMaxDisplaySamples) {
			sample_index++;
			remainder_accumulator -= kMaxDisplaySamples;
		}
	}

	// Mark OLED as changed so it gets sent to display
	OLED::markChanged();
}

} // namespace deluge::hid::display
