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

#include "visualizer_cube.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "playback/playback_handler.h"
#include "util/functions.h"
#include "visualizer_common.h"
#include "visualizer_fft.h"
#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

// Forward declaration for renderCube function
void renderCube(oled_canvas::Canvas& canvas, float rotation_angle, float scale, int32_t center_x, int32_t center_y,
                int32_t display_width, int32_t display_height);

// Cube-specific constants and buffers
namespace {
// Cube geometry: 8 vertices, 12 edges
constexpr int32_t kCubeVertices = 8;
constexpr int32_t kCubeEdges = 12;
constexpr int32_t kVertexComponents = 3; // x, y, z per vertex

// Cube vertices (unit cube centered at origin)
const float cube_vertices[kCubeVertices * kVertexComponents] = {
    -0.5f, -0.5f, -0.5f, // 0: bottom-back-left
    0.5f,  -0.5f, -0.5f, // 1: bottom-back-right
    0.5f,  0.5f,  -0.5f, // 2: top-back-right
    -0.5f, 0.5f,  -0.5f, // 3: top-back-left
    -0.5f, -0.5f, 0.5f,  // 4: bottom-front-left
    0.5f,  -0.5f, 0.5f,  // 5: bottom-front-right
    0.5f,  0.5f,  0.5f,  // 6: top-front-right
    -0.5f, 0.5f,  0.5f   // 7: top-front-left
};

// Cube edges (pairs of vertex indices)
const int32_t cube_edges[kCubeEdges * 2] = {
    0, 1, // bottom-back
    1, 2, // back-right
    2, 3, // top-back
    3, 0, // back-left
    4, 5, // bottom-front
    5, 6, // front-right
    6, 7, // top-front
    7, 4, // front-left
    0, 4, // left-bottom
    1, 5, // right-bottom
    2, 6, // right-top
    3, 7  // left-top
};

// Animation constants
constexpr float kBaseRotationSpeed = 0.9f;
constexpr float kMaxRotationSpeed = 1.8f;
constexpr float kBaseScale = 0.5f;       // Base cube scale
constexpr float kBounceFactor = 1.2f;    // Bounce animation factor
constexpr float kSmoothingFactor = 0.3f; // Amplitude smoothing factor
constexpr float kReferenceBPM = 120.0f;  // Reference BPM for tempo calculations

// Audio analysis constants
constexpr float kAmplitudeAmplification = 3.0f; // Amplify amplitude for more visible scaling
constexpr float kAmplitudeScaleFactor = 5.0f;   // Scale amplitude contribution to cube size
constexpr float kOverallScaleMultiplier = 4.0f; // Overall scale multiplier for larger cube

// Frequency analysis ranges (FFT bin indices)
constexpr size_t kPitchStartBin = 5;     // Low frequency range for pitch analysis
constexpr size_t kPitchEndBin = 50;      // High frequency range for pitch analysis
constexpr size_t kAmplitudeStartBin = 0; // Low frequency range for amplitude analysis
constexpr size_t kAmplitudeEndBin = 8;   // High frequency range for amplitude analysis

// Display constants
constexpr int32_t kLineWidth = 1;
constexpr float kFramesPerSecond = 30.0f; // Assumed frame rate for animation timing

// 3D transformation constants
constexpr float kCubeTranslateY = 0.2f;            // Y translation for cube positioning
constexpr float kCubeTiltAngleDegrees = 20.0f;     // Tilt angle in degrees for perspective
constexpr float kRotationMultiplierY = 2.0f;       // Y-axis rotation speed multiplier
constexpr float kRotationMultiplierX = 0.7f;       // X-axis rotation speed multiplier
constexpr float kRotationOffsetXDegrees = 30.0f;   // X-axis rotation offset in degrees
constexpr float kRotationMultiplierZ = 0.5f;       // Z-axis rotation speed multiplier
constexpr float kDegreesToRadians = M_PI / 180.0f; // Conversion factor from degrees to radians

// Perspective projection constants
constexpr float kCameraZPosition = -3.5f;    // Camera Z position (negative for proper projection)
constexpr float kNearClippingPlane = 0.1f;   // Near clipping plane distance
constexpr float kFieldOfViewDegrees = 45.0f; // Field of view in degrees

// Static state for smoothing
float last_amplitude = 0.0f;
} // namespace

/// Render 3D cube visualization on OLED display
void renderVisualizerCube(oled_canvas::Canvas& canvas) {
	constexpr int32_t k_display_width = OLED_MAIN_WIDTH_PIXELS;
	constexpr int32_t k_display_height = OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL;
	constexpr int32_t k_margin = kDisplayMargin;
	constexpr int32_t k_graph_min_x = k_margin;
	constexpr int32_t k_graph_max_x = k_display_width - k_margin - 1;
	constexpr int32_t k_graph_min_y = OLED_MAIN_TOPMOST_PIXEL + k_margin;
	constexpr int32_t k_graph_max_y = OLED_MAIN_TOPMOST_PIXEL + k_display_height - k_margin - 1;
	constexpr int32_t k_center_x = (k_graph_min_x + k_graph_max_x) / 2;
	constexpr int32_t k_center_y = (k_graph_min_y + k_graph_max_y) / 2;

	// Compute FFT using shared helper function
	FFTResult fft_result = computeVisualizerFFT();
	if (!fft_result.isValid) {
		// Not enough samples or FFT config not available, draw empty
		return;
	}

	// Check for silence
	if (fft_result.isSilent) {
		return;
	}

	// Calculate tempo-based rotation speed (BPM determines rotation rate)
	float tempo_bpm = playbackHandler.calculateBPMForDisplay();
	// Prevent division by zero with minimum BPM
	tempo_bpm = std::max(tempo_bpm, 1.0f);
	// Convert BPM to rotation speed - higher BPM = faster rotation
	// Scale so reference BPM gives moderate rotation, higher BPM gives faster rotation
	float rotation_speed = (tempo_bpm / kReferenceBPM) * kBaseRotationSpeed;

	// Calculate audio amplitude from sample buffer for cube scaling
	float current_amplitude = ::deluge::hid::display::computeCurrentAmplitude();
	// Amplify for more visible cube scaling
	current_amplitude = std::min(current_amplitude * kAmplitudeAmplification, 1.0f);

	// Apply smoothing
	float smoothed_amplitude = last_amplitude + kSmoothingFactor * (current_amplitude - last_amplitude);
	last_amplitude = smoothed_amplitude;

	// Scale cube based on audio amplitude for dynamic visual response
	float scale = (kBaseScale + smoothed_amplitude * kAmplitudeScaleFactor) * kOverallScaleMultiplier;

	// Get time for animation
	static uint32_t frame_counter = 0;
	frame_counter++;
	float time_seconds = static_cast<float>(frame_counter) / kFramesPerSecond;

	// Clear the visualizer area
	canvas.clearAreaExact(k_graph_min_x, k_graph_min_y, k_graph_max_x, k_graph_max_y + 1);

	// Render the cube
	renderCube(canvas, time_seconds * rotation_speed, scale, k_center_x, k_center_y, k_graph_max_x - k_graph_min_x,
	           k_graph_max_y - k_graph_min_y);
}

/// Render a 3D wireframe cube with rotation and scaling
void renderCube(oled_canvas::Canvas& canvas, float rotation_angle, float scale, int32_t center_x, int32_t center_y,
                int32_t display_width, int32_t display_height) {

	// Apply 3D transformations to cube vertices
	// 1. Y translation for cube positioning
	float translate_y = kCubeTranslateY;

	// 2. Add tilt for better 3D perspective
	float tilt_angle = kCubeTiltAngleDegrees * kDegreesToRadians;
	float cos_tilt = std::cos(tilt_angle);
	float sin_tilt = std::sin(tilt_angle);

	// 3. Apply XYZ rotations for 3D animation
	float cos_y = std::cos(rotation_angle * kRotationMultiplierY);
	float sin_y = std::sin(rotation_angle * kRotationMultiplierY);

	float cos_x = std::cos(rotation_angle * kRotationMultiplierX + kRotationOffsetXDegrees * kDegreesToRadians);
	float sin_x = std::sin(rotation_angle * kRotationMultiplierX + kRotationOffsetXDegrees * kDegreesToRadians);

	float cos_z = std::cos(rotation_angle * kRotationMultiplierZ);
	float sin_z = std::sin(rotation_angle * kRotationMultiplierZ);

	// Project and draw edges
	for (int32_t i = 0; i < kCubeEdges; i++) {
		// Get vertex indices for this edge
		int32_t v1_idx = cube_edges[i * 2];
		int32_t v2_idx = cube_edges[i * 2 + 1];

		// Get vertex positions
		float v1_x = cube_vertices[v1_idx * 3];
		float v1_y = cube_vertices[v1_idx * 3 + 1] + translate_y; // Apply Y translation
		float v1_z = cube_vertices[v1_idx * 3 + 2];

		float v2_x = cube_vertices[v2_idx * 3];
		float v2_y = cube_vertices[v2_idx * 3 + 1] + translate_y; // Apply Y translation
		float v2_z = cube_vertices[v2_idx * 3 + 2];

		// Apply tilt first (matches mini_cube glRotatef(20.0f, 1.0f, 0.0f, 0.0f))
		float v1_tilt_y = v1_y * cos_tilt - v1_z * sin_tilt;
		float v1_tilt_z = v1_y * sin_tilt + v1_z * cos_tilt;
		float v2_tilt_y = v2_y * cos_tilt - v2_z * sin_tilt;
		float v2_tilt_z = v2_y * sin_tilt + v2_z * cos_tilt;

		// Apply rotations (Y * X * Z order)
		// Y rotation
		float v1_yx = v1_x * cos_y - v1_tilt_z * sin_y;
		float v1_yz = v1_x * sin_y + v1_tilt_z * cos_y;
		float v2_yx = v2_x * cos_y - v2_tilt_z * sin_y;
		float v2_yz = v2_x * sin_y + v2_tilt_z * cos_y;

		// X rotation
		float v1_xy = v1_tilt_y * cos_x - v1_yz * sin_x;
		float v1_xz = v1_tilt_y * sin_x + v1_yz * cos_x;
		float v2_xy = v2_tilt_y * cos_x - v2_yz * sin_x;
		float v2_xz = v2_tilt_y * sin_x + v2_yz * cos_x;

		// Z rotation
		float v1_zx = v1_yx * cos_z - v1_xy * sin_z;
		float v1_zy = v1_yx * sin_z + v1_xy * cos_z;
		float v2_zx = v2_yx * cos_z - v2_xy * sin_z;
		float v2_zy = v2_yx * sin_z + v2_xy * cos_z;

		// Apply scale
		v1_zx *= scale;
		v1_zy *= scale;
		v1_xz *= scale;
		v2_zx *= scale;
		v2_zy *= scale;
		v2_xz *= scale;

		// Apply perspective projection
		// Calculate Z position relative to camera
		float z_pos = kCameraZPosition;
		float v1_proj_z = v1_xz - z_pos;
		float v2_proj_z = v2_xz - z_pos;

		// Clip vertices behind the camera
		if (v1_proj_z <= kNearClippingPlane || v2_proj_z <= kNearClippingPlane) {
			continue;
		}

		// Calculate perspective projection parameters
		float aspect_ratio = static_cast<float>(display_width) / static_cast<float>(display_height);
		float fovy = kFieldOfViewDegrees * kDegreesToRadians;
		float f = 1.0f / tan(fovy / 2.0f);

		// Project to normalized device coordinates (-1 to 1)
		float v1_ndc_x = (v1_zx / v1_proj_z) * (f / aspect_ratio);
		float v1_ndc_y = (v1_zy / v1_proj_z) * f;
		float v2_ndc_x = (v2_zx / v2_proj_z) * (f / aspect_ratio);
		float v2_ndc_y = (v2_zy / v2_proj_z) * f;

		// Convert to screen coordinates
		float v1_screen_x = center_x + v1_ndc_x * (display_width / 2.0f);
		float v1_screen_y = center_y - v1_ndc_y * (display_height / 2.0f);
		float v2_screen_x = center_x + v2_ndc_x * (display_width / 2.0f);
		float v2_screen_y = center_y - v2_ndc_y * (display_height / 2.0f);

		// Convert to integers and clip to display bounds
		int32_t x1 = static_cast<int32_t>(v1_screen_x);
		int32_t y1 = static_cast<int32_t>(v1_screen_y);
		int32_t x2 = static_cast<int32_t>(v2_screen_x);
		int32_t y2 = static_cast<int32_t>(v2_screen_y);

		// Clip coordinates to OLED display bounds to prevent crashes
		const int32_t k_max_x = static_cast<int32_t>(OLED_MAIN_WIDTH_PIXELS) - 1;
		const int32_t k_max_y = static_cast<int32_t>(OLED_MAIN_HEIGHT_PIXELS) - 1;
		x1 = std::max(static_cast<int32_t>(0), std::min(x1, k_max_x));
		y1 = std::max(static_cast<int32_t>(0), std::min(y1, k_max_y));
		x2 = std::max(static_cast<int32_t>(0), std::min(x2, k_max_x));
		y2 = std::max(static_cast<int32_t>(0), std::min(y2, k_max_y));

		// Draw the edge
		canvas.drawLine(x1, y1, x2, y2);
	}

	// Mark OLED as changed
	OLED::markChanged();
}

} // namespace deluge::hid::display
