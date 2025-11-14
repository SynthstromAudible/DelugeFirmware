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

#include "visualizer_common.h"
#include "hid/display/visualizer.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

// Shared constants
namespace {
// Visual compression parameters
constexpr float kCompressionExponent = 0.5f;         // 2:1 compression ratio
constexpr float kFrequencyBoostExponent = 0.1f;      // Treble emphasis bias
constexpr float kFrequencyNormalizationHz = 1000.0f; // Frequency normalization base
constexpr float kAmplitudeBoost = 1.0f;              // Amplitude boost

// Display constants
constexpr int32_t kDisplayMargin = 2; // Standard margin for visualizer display areas
} // namespace

// Helper function to get read start position from circular buffer for most recent samples
uint32_t getVisualizerReadStartPos(uint32_t sample_count) {
	uint32_t write_pos = 0;
	write_pos = Visualizer::visualizer_write_pos.load(std::memory_order_acquire);

	// Start reading from the most recent samples (just before write_pos)
	// Go back by sample_count to get the most recent samples, wrapping around the circular buffer
	return (write_pos - sample_count) % Visualizer::kVisualizerBufferSize;
}

// Apply music sweet-spot visual compression to amplitude and frequency
// Formula: ((amplitude^kCompressionExponent) * ((frequency/kFrequencyNormalizationHz)^kFrequencyBoostExponent)) *
// kAmplitudeBoost The compression exponent provides 2:1 visual compression (square root scaling) The frequency boost
// term provides industry standard treble emphasis (reduces bass/treble extremes) The amplitude boost increases overall
// visual amplitude by 10%
float applyVisualizerCompression(float amplitude, float frequency) {
	// Normalize amplitude to 0-1 range if not already
	amplitude = std::clamp(amplitude, 0.0f, 1.0f);
	return (std::pow(amplitude, kCompressionExponent)
	        * std::pow(frequency / kFrequencyNormalizationHz, kFrequencyBoostExponent))
	       * kAmplitudeBoost;
}

// Compute current audio amplitude from recent samples (peak detection)
// Returns normalized amplitude in range [0, 1]
float computeCurrentAmplitude() {
	float current_amplitude = 0.0f;
	uint32_t sample_count = Visualizer::visualizer_sample_count.load(std::memory_order_acquire);
	if (sample_count < 2) {
		return 0.0f;
	}
	uint32_t read_start_pos = getVisualizerReadStartPos(sample_count);
	float peak_amplitude = 0.0f;

	for (uint32_t i = 0; i < kAmplitudeSampleCount && i < sample_count; i++) {
		uint32_t buffer_index = (read_start_pos + i) % Visualizer::kVisualizerBufferSize;
		int32_t sample = Visualizer::visualizer_sample_buffer[buffer_index];
		float abs_sample = static_cast<float>(std::abs(sample));
		peak_amplitude = std::max(peak_amplitude, abs_sample);
	}

	current_amplitude = peak_amplitude / kReferenceAmplitude;

	// Clamp to valid range [0, 1]
	return std::clamp(current_amplitude, 0.0f, 1.0f);
}

} // namespace deluge::hid::display
