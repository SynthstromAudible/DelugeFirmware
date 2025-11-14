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

#include "visualizer_tunnel.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "playback/playback_handler.h"
#include "util/functions.h"
#include "visualizer_common.h"
#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

// ------------------------------------------------------------
// Ring data
// ------------------------------------------------------------

struct Ring {
	float z; // depth
};

// ------------------------------------------------------------
// Tunnel-specific constants and buffers
// ------------------------------------------------------------

namespace {
// Tunnel geometry: 10 rings for depth effect
constexpr int32_t kTunnelRings = 10;
constexpr float kDepthSpread = 0.20f;      // Depth spacing between rings
constexpr float kPerspective = 45.0f;      // Perspective scaling
constexpr float kBaseSpeed = 0.008f;       // Base movement speed
constexpr float kNearClippingPlane = 0.1f; // Near clipping distance

// Chained illumination constants
constexpr uint32_t kBaseChainDelayFrames = 2;           // Base delay between each ring in the chain
constexpr uint32_t kBaseIlluminationDurationFrames = 5; // Base duration each ring stays illuminated after audio stops

// Tempo and smoothing constants
constexpr float kReferenceBPM = 120.0f; // Reference BPM for tempo calculations
// Smoothing filter coefficients (first-order IIR) - more responsive for better sensitivity
constexpr float kSmoothingAlpha = 0.5f; // Temporal smoothing strength (higher = more stable, less responsive)
constexpr float kSmoothingBeta = 0.5f;  // Responsiveness to input changes (higher = more responsive)

// Audio detection constants
constexpr float kAudioDetectionThreshold = 0.0075f; // Amplitude threshold for audio detection (more sensitive)

// Rendering constants
constexpr float kWaveformIndexScale = 20.0f; // Waveform index scaling factor
constexpr float kPerspectiveScale = 120.0f;  // Perspective scaling factor

// Ring data - static to maintain state between frames
Ring rings[kTunnelRings];

// Static state for amplitude smoothing
float last_amplitude = 0.0f;
// Static smoothed amplitude for IIR filtering
float smoothed_amplitude_value = 0.0f;

// Previous ring corners for connecting lines
int32_t prev_left = -1, prev_top = -1, prev_right = -1, prev_bottom = -1;

// Chained illumination timing
static uint32_t ring_illumination_start[kTunnelRings] = {0}; // When each ring started illuminating
static uint32_t last_audio_time = 0;                         // Last time we had audio

// Display constants
constexpr int32_t kLineWidth = 1;

} // namespace

// ------------------------------------------------------------
// Wave sample helper [-1..1]
// ------------------------------------------------------------

static inline float getWaveSample(uint32_t sample_index) {
	// Get sample from visualizer buffer using atomic operations
	uint32_t sample_count = Visualizer::visualizer_sample_count.load(std::memory_order_acquire);
	if (sample_count == 0) {
		return 0.0f; // No samples available
	}

	// Use circular buffer read position
	uint32_t read_start_pos = getVisualizerReadStartPos(sample_count);
	uint32_t buffer_index = (read_start_pos + sample_index) % Visualizer::kVisualizerBufferSize;

	// Convert Q15 sample to float [-1..1]
	int32_t sample = Visualizer::visualizer_sample_buffer[buffer_index];
	return static_cast<float>(sample) / 32768.0f;
}

// ------------------------------------------------------------
// Initialize tunnel rings
// ------------------------------------------------------------

static void initTunnelRings() {
	for (int32_t i = 0; i < kTunnelRings; i++) {
		rings[i].z = static_cast<float>(i) * kDepthSpread;
	}
}

// ------------------------------------------------------------
// Update + Render
// ------------------------------------------------------------

/// Render tunnel visualization on OLED display
void renderVisualizerTunnel(oled_canvas::Canvas& canvas) {
	constexpr int32_t k_display_width = OLED_MAIN_WIDTH_PIXELS;
	constexpr int32_t k_display_height = OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL;
	constexpr int32_t k_margin = kDisplayMargin;
	constexpr int32_t k_graph_min_x = k_margin;
	constexpr int32_t k_graph_max_x = k_display_width - k_margin - 1;
	constexpr int32_t k_graph_min_y = OLED_MAIN_TOPMOST_PIXEL + k_margin;
	constexpr int32_t k_graph_max_y = OLED_MAIN_TOPMOST_PIXEL + k_display_height - k_margin - 1;

	// Initialize rings on first run
	static bool initialized = false;
	if (!initialized) {
		initTunnelRings();
		initialized = true;
	}

	// Check for audio amplitude to determine illumination level
	float current_amplitude = ::deluge::hid::display::computeCurrentAmplitude();

	// Apply amplitude smoothing using IIR filter for stability (first-order IIR)
	smoothed_amplitude_value = (smoothed_amplitude_value * kSmoothingAlpha) + (current_amplitude * kSmoothingBeta);
	float smoothed_amplitude = smoothed_amplitude_value;

	// Calculate tempo-based values (BPM determines ring movement rate and illumination timing)
	float bpm = playbackHandler.calculateBPMForDisplay();
	// Prevent division by zero with minimum BPM
	bpm = std::max(bpm, 1.0f);
	float speed = kBaseSpeed * (bpm / kReferenceBPM);

	// Calculate tempo-based illumination timing (faster BPM = faster illumination chain)
	float tempo_ratio = bpm / kReferenceBPM;
	// Prevent division by zero in timing calculations
	tempo_ratio = std::max(tempo_ratio, 0.01f);
	uint32_t chain_delay_frames =
	    static_cast<uint32_t>(kBaseChainDelayFrames / tempo_ratio); // Shorter delay at higher BPM
	uint32_t illumination_duration_frames =
	    static_cast<uint32_t>(kBaseIlluminationDurationFrames / tempo_ratio); // Shorter duration at higher BPM

	// Update chained illumination timing using frame counter
	static uint32_t illumination_frame_counter = 0;
	illumination_frame_counter++;
	uint32_t current_time = illumination_frame_counter;               // Use frame count as time basis
	bool has_audio = (smoothed_amplitude > kAudioDetectionThreshold); // Threshold for audio detection (more sensitive)

	if (has_audio) {
		// Audio detected - start/update chained illumination
		last_audio_time = current_time;

		// Start illumination chain if not already started
		if (ring_illumination_start[kTunnelRings - 1] == 0) {
			ring_illumination_start[kTunnelRings - 1] = current_time; // Inner ring starts immediately
		}

		// Start subsequent rings with tempo-based delays (working outward from center)
		for (int32_t i = kTunnelRings - 2; i >= 0; i--) {
			if (ring_illumination_start[i] == 0) {
				uint32_t expected_start =
				    ring_illumination_start[kTunnelRings - 1] + ((kTunnelRings - 1 - i) * chain_delay_frames);
				if (current_time >= expected_start) {
					ring_illumination_start[i] = current_time;
				}
			}
		}
	}
	else {
		// No audio - check if rings should turn off with tempo-based duration
		for (int32_t i = 0; i < kTunnelRings; i++) {
			if (ring_illumination_start[i] > 0) {
				uint32_t time_since_last_audio = current_time - last_audio_time;
				if (time_since_last_audio > illumination_duration_frames) {
					ring_illumination_start[i] = 0; // Turn off ring
				}
			}
		}
	}

	// Use simple frame counter for animation timing (30 FPS)
	static uint32_t frame_counter = 0;
	frame_counter++;

	// Clear the visualizer area
	canvas.clearAreaExact(k_graph_min_x, k_graph_min_y, k_graph_max_x, k_graph_max_y + 1);

	// Reset previous ring tracking
	prev_left = -1;
	prev_top = -1;
	prev_right = -1;
	prev_bottom = -1;

	// Ring 0 is always the outer tunnel frame

	// Update and draw rings
	for (int32_t i = 0; i < kTunnelRings; i++) {
		// Move ring toward viewer
		rings[i].z -= speed;

		// Wrap around when ring reaches the front
		if (rings[i].z < 0.0f) {
			rings[i].z += static_cast<float>(kTunnelRings) * kDepthSpread;
		}

		// Only render rings that are currently illuminated
		if (ring_illumination_start[i] == 0) {
			continue; // Skip rings that aren't illuminated
		}

		// Map depth to waveform index for consistent modulation
		// Use broader range for more waveform variation
		uint32_t wf_index = static_cast<uint32_t>(rings[i].z * kWaveformIndexScale) % Visualizer::kVisualizerBufferSize;

		// Calculate ring corners for connecting lines
		float denominator = kPerspective + rings[i].z * kPerspectiveScale;
		// Prevent division by very small numbers that could cause extreme scaling
		denominator = std::max(denominator, 1.0f);
		float s = kPerspective / denominator;

		int32_t half_w = static_cast<int32_t>(OLED_MAIN_WIDTH_PIXELS * 0.5f * s);
		int32_t half_h = static_cast<int32_t>((OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL) * 0.5f * s);

		// Compute screen center
		const int32_t cx = OLED_MAIN_WIDTH_PIXELS / 2;
		const int32_t cy = OLED_MAIN_TOPMOST_PIXEL + (OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL) / 2;

		// Calculate ring coordinates
		int32_t curr_left = cx - half_w;
		int32_t curr_right = cx + half_w;
		int32_t curr_top = cy - half_h;
		int32_t curr_bottom = cy + half_h;

		// Clip coordinates to OLED bounds for safety
		const int32_t k_max_x = static_cast<int32_t>(OLED_MAIN_WIDTH_PIXELS) - 1;
		const int32_t k_max_y = static_cast<int32_t>(OLED_MAIN_HEIGHT_PIXELS) - 1;
		curr_left = std::max(static_cast<int32_t>(0), std::min(curr_left, k_max_x));
		curr_right = std::max(static_cast<int32_t>(0), std::min(curr_right, k_max_x));
		curr_top = std::max(static_cast<int32_t>(0), std::min(curr_top, k_max_y));
		curr_bottom = std::max(static_cast<int32_t>(0), std::min(curr_bottom, k_max_y));

		// Draw connecting lines to previous ring if it exists
		if (prev_left >= 0 && prev_top >= 0 && prev_right >= 0 && prev_bottom >= 0) {
			// Draw tunnel walls - connect corners of adjacent rings
			canvas.drawLine(prev_left, prev_top, curr_left, curr_top);         // top-left diagonal
			canvas.drawLine(prev_right, prev_top, curr_right, curr_top);       // top-right diagonal
			canvas.drawLine(prev_left, prev_bottom, curr_left, curr_bottom);   // bottom-left diagonal
			canvas.drawLine(prev_right, prev_bottom, curr_right, curr_bottom); // bottom-right diagonal
		}

		// Draw the ring rectangle (special case for ring 0 which may be at z=0)
		if (curr_right > curr_left && curr_bottom > curr_top && curr_right - curr_left > 2 && curr_bottom - curr_top > 2
		    && (i == 0 || rings[i].z > kNearClippingPlane)) {
			canvas.drawLine(curr_left, curr_top, curr_right, curr_top);
			canvas.drawLine(curr_right, curr_top, curr_right, curr_bottom);
			canvas.drawLine(curr_right, curr_bottom, curr_left, curr_bottom);
			canvas.drawLine(curr_left, curr_bottom, curr_left, curr_top);
		}

		// Store current ring corners for next iteration
		prev_left = curr_left;
		prev_top = curr_top;
		prev_right = curr_right;
		prev_bottom = curr_bottom;
	}

	// Mark OLED as changed
	OLED::markChanged();
}

} // namespace deluge::hid::display
