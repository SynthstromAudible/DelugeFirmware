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

#include "visualizer_starfield.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "playback/playback_handler.h"
#include "util/functions.h"
#include "visualizer_common.h"

#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

namespace {

// ------------------------------------------------------------
// Starfield Constants
// ------------------------------------------------------------

constexpr int32_t kNumStars = 64;
constexpr float kBaseSpeed = 0.008f;
constexpr float kMinDepth = 0.02f;
constexpr float kMaxDepth = 1.0f;

// ------------------------------------------------------------
// UFO Constants
// ------------------------------------------------------------

constexpr int32_t kNumBeatPositions = 4;  // 4 positions for 4 beats
constexpr float kUFOSize = 10.0f;         // UFO radius in pixels
constexpr float kUFOCenterOffset = 12.0f; // Distance from screen center
constexpr int32_t kUFOThickness = 2;      // UFO outline thickness
constexpr float kReferenceBPM = 120.0f;   // Reference BPM for tempo calculations

// Uniform amplitude analysis (but used *only* for motion effects)
// Uses shared constants from visualizer_common.h

constexpr float kSmoothingAlpha = 0.6f;
constexpr float kSmoothingBeta = 0.4f;

// Silence detection constants (same as waveform)
constexpr int32_t kSilenceThreshold = 10;     // Q15 format - matches waveform visualizer
constexpr int32_t kSilenceCheckInterval = 16; // Check every Nth sample to reduce CPU usage

// Persistence constants (similar to tunnel visualizer)
constexpr uint32_t kBasePersistenceDurationFrames = 10; // Base duration to keep visualizer active after audio stops

// Star rendering constants
constexpr float kMaxSpawnRadius = 0.95f;    // Maximum spawn radius for star distribution
constexpr float kProjectionScaleX = 60.0f;  // X-axis projection scaling factor
constexpr float kProjectionScaleY = 28.0f;  // Y-axis projection scaling factor
constexpr float kCloseStarThreshold = 0.3f; // Depth threshold for close stars
constexpr float kSizeThreshold = 0.6f;      // Depth threshold for star size changes
constexpr int32_t kMediumStarSize = 2;      // Size of medium stars
constexpr int32_t kLargeStarSize = 3;       // Size of large stars

// ------------------------------------------------------------
// Star Data
// ------------------------------------------------------------

struct Star {
	float x; // -1..1
	float y; // -1..1
	float z; // depth
};

std::array<Star, kNumStars> g_stars{};
float g_smoothed_amplitude = 0.0f;
uint32_t g_last_audio_time = 0; // Last time we had audio (for persistence)

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

// Spawn star with jittered center origin
void resetStar(Star& s) {
	// Generate random angle (0 to 2π)
	float angle = (float)random(65535) / 65536.0f * float(M_PI) * 2.0f;

	// Use square root distribution for uniform disc coverage
	float radius = sqrtf((float)random(65535) / 65536.0f) * kMaxSpawnRadius;

	s.x = std::cos(angle) * radius;
	s.y = std::sin(angle) * radius;

	// Randomize depth to prevent synchronized star bursts
	s.z = (float)random(65535) / 65536.0f * (kMaxDepth - kMinDepth) + kMinDepth;
}

// Project 3D star position into 2D OLED coords
void projectStar(const Star& s, int32_t& out_x, int32_t& out_y) {
	const float kPerspective = 180.0f; // strong warp = hyperspace feel

	float scale = kPerspective / (kPerspective * s.z);

	int32_t cx = OLED_MAIN_WIDTH_PIXELS / 2;
	int32_t cy = OLED_MAIN_TOPMOST_PIXEL + (OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL) / 2;

	out_x = int32_t(cx + (s.x * scale * kProjectionScaleX));
	out_y = int32_t(cy + (s.y * scale * kProjectionScaleY));
}

// Draw star (monochrome-only)
void drawStar(oled_canvas::Canvas& canvas, const Star& s) {
	int32_t px, py;
	projectStar(s, px, py);

	if (px < 0 || px >= OLED_MAIN_WIDTH_PIXELS)
		return;
	if (py < OLED_MAIN_TOPMOST_PIXEL || py >= OLED_MAIN_HEIGHT_PIXELS)
		return;

	// Bigger when close
	int32_t size;
	if (s.z > kSizeThreshold)
		size = 1;
	else if (s.z > kCloseStarThreshold)
		size = kMediumStarSize;
	else
		size = kLargeStarSize;

	int32_t half = size / 2;
	for (int32_t dy = -half; dy <= half; dy++) {
		for (int32_t dx = -half; dx <= half; dx++) {
			int32_t xx = px + dx;
			int32_t yy = py + dy;

			if (xx >= 0 && xx < OLED_MAIN_WIDTH_PIXELS && yy >= OLED_MAIN_TOPMOST_PIXEL
			    && yy < OLED_MAIN_HEIGHT_PIXELS) {
				canvas.drawPixel(xx, yy); // ON pixel only
			}
		}
	}
}

// Calculate current beat position (0-3) synchronized to tempo (half time)
int32_t getCurrentBeatPosition(float bpm, uint32_t frame_counter) {
	// Use tempo ratio like tunnel visualizer for proper sync
	float tempo_ratio = bpm / kReferenceBPM;
	tempo_ratio = std::max(tempo_ratio, 0.01f); // Prevent division by zero

	// For half time, we want to move every 2 beats
	// So we scale the frame counter by tempo_ratio to get proper beat timing
	float scaled_counter = frame_counter * tempo_ratio;

	// Each position should last for a certain number of frames at reference BPM
	// At 120 BPM, we want reasonable movement speed - let's say 30 frames per position
	constexpr float kFramesPerPosition = 30.0f;

	// Calculate position based on scaled time
	int32_t position = static_cast<int32_t>(scaled_counter / kFramesPerPosition) % kNumBeatPositions;

	return position;
}

// Get UFO center position for current beat
void getUFOCenterPosition(int32_t beat_position, int32_t& out_x, int32_t& out_y) {
	int32_t cx = OLED_MAIN_WIDTH_PIXELS / 2;
	int32_t cy = OLED_MAIN_TOPMOST_PIXEL + (OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL) / 2;

	switch (beat_position) {
	case 0: // Top
		out_x = cx;
		out_y = cy - static_cast<int32_t>(kUFOCenterOffset);
		break;
	case 1: // Right
		out_x = cx + static_cast<int32_t>(kUFOCenterOffset);
		out_y = cy;
		break;
	case 2: // Bottom
		out_x = cx;
		out_y = cy + static_cast<int32_t>(kUFOCenterOffset);
		break;
	case 3: // Left
	default:
		out_x = cx - static_cast<int32_t>(kUFOCenterOffset);
		out_y = cy;
		break;
	}
}

// Draw improved classic flying saucer (disk with small dome)
void drawUFO(oled_canvas::Canvas& canvas, int32_t center_x, int32_t center_y) {
	const int32_t radius = static_cast<int32_t>(kUFOSize);

	// --- MAIN SAUCER DISK (shallow wide oval) ---
	const float saucerWidthScale = 2.2f;   // Much wider disk
	const float saucerHeightScale = 0.35f; // Shallow saucer

	int32_t saucerHeight = static_cast<int32_t>(radius * saucerHeightScale);
	for (int32_t y = center_y - saucerHeight; y <= center_y + saucerHeight; y++) {

		if (y < OLED_MAIN_TOPMOST_PIXEL || y >= OLED_MAIN_HEIGHT_PIXELS)
			continue;

		float dy = (static_cast<float>(center_y) - y) / (radius * saucerHeightScale);
		float dx = sqrtf(std::max(0.0f, 1.0f - dy * dy)) * (radius * saucerWidthScale);
		int32_t x_min = center_x - static_cast<int32_t>(dx);
		int32_t x_max = center_x + static_cast<int32_t>(dx);

		for (int32_t x = x_min; x <= x_max; x++) {
			if (x >= 0 && x < OLED_MAIN_WIDTH_PIXELS) {
				canvas.drawPixel(x, y);
			}
		}
	}

	// --- SMALL DOME (bubble canopy) ---
	const float domeHeightScale = 0.20f; // Much shorter dome
	const float domeWidthScale = 0.55f;  // Narrower than saucer

	int32_t domeHeight = static_cast<int32_t>(radius * domeHeightScale);
	int32_t domeBaseY = center_y - saucerHeight;

	for (int32_t y = domeBaseY - domeHeight; y <= domeBaseY; y++) {
		if (y < OLED_MAIN_TOPMOST_PIXEL || y >= OLED_MAIN_HEIGHT_PIXELS)
			continue;

		float dy = (static_cast<float>(domeBaseY) - y) / domeHeight;
		float dx = sqrtf(std::max(0.0f, 1.0f - dy * dy)) * (radius * saucerWidthScale * domeWidthScale);

		int32_t x_min = center_x - static_cast<int32_t>(dx);
		int32_t x_max = center_x + static_cast<int32_t>(dx);

		for (int32_t x = x_min; x <= x_max; x++) {
			if (x >= 0 && x < OLED_MAIN_WIDTH_PIXELS) {
				canvas.drawPixel(x, y);
			}
		}
	}
}

// ------------------------------------------------------------
// Initialization
// ------------------------------------------------------------

void initStarfield() {
	for (int32_t i = 0; i < kNumStars; i++)
		resetStar(g_stars[i]);
}

} // namespace

// ------------------------------------------------------------
// Render Function
// ------------------------------------------------------------

void renderVisualizerStarfield(oled_canvas::Canvas& canvas) {
	static bool initialized = false;
	if (!initialized) {
		initStarfield();
		initialized = true;
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
				// Previous starfield frame remains visible
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

	// --------------------------------------------------------
	// Audio amplitude (ONLY used for optional motion effects)
	// --------------------------------------------------------
	float current_amp = 0.0f;

	if (sample_count > 2) {
		uint32_t start = getVisualizerReadStartPos(sample_count);
		float peak = 0.0f;

		for (uint32_t i = 0; i < kAmplitudeSampleCount && i < sample_count; i++) {
			uint32_t idx = (start + i) % Visualizer::kVisualizerBufferSize;
			float abs_s = std::abs((float)Visualizer::visualizer_sample_buffer[idx]);
			peak = std::max(peak, abs_s);
		}

		current_amp = std::min(peak / kReferenceAmplitude, 1.0f);
	}

	g_smoothed_amplitude = g_smoothed_amplitude * kSmoothingAlpha + current_amp * kSmoothingBeta;

	// Optional (safe on monochrome): use amplitude to slightly increase speed
	float audio_speed_boost = g_smoothed_amplitude * 0.01f;

	// BPM-controlled speed
	float bpm = playbackHandler.calculateBPMForDisplay();
	// Prevent division by zero with minimum BPM
	bpm = std::max(bpm, 1.0f);
	float base_speed = kBaseSpeed * (bpm / kReferenceBPM);

	float speed = base_speed + audio_speed_boost;

	// --------------------------------------------------------
	// Clear screen
	// --------------------------------------------------------
	canvas.clear();

	// --------------------------------------------------------
	// Update and draw stars
	// --------------------------------------------------------
	for (int32_t i = 0; i < kNumStars; i++) {
		Star& s = g_stars[i];

		// Move toward viewer
		s.z -= speed;

		// Respawn when star reaches camera
		if (s.z < kMinDepth)
			resetStar(s);

		// Draw star
		drawStar(canvas, s);
	}

	// --------------------------------------------------------
	// Draw UFO synchronized to 4-beat tempo
	// --------------------------------------------------------
	int32_t beat_position = getCurrentBeatPosition(bpm, frame_counter);
	int32_t ufo_x, ufo_y;
	getUFOCenterPosition(beat_position, ufo_x, ufo_y);
	drawUFO(canvas, ufo_x, ufo_y);

	OLED::markChanged();
}

} // namespace deluge::hid::display
