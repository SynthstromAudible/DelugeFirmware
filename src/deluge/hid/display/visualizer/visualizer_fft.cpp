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

#include "visualizer_fft.h"
#include "dsp/fft/fft_config_manager.h"
#include "hid/display/visualizer.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include "visualizer_common.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace deluge::hid::display {

// Static FFT buffers (shared by Spectrum/Equalizer visualizers)
namespace {
// FFT size tradeoff: Larger size improves frequency resolution for better equalizer accuracy,
// especially at lower frequencies, but increases computational cost and reduces update frequency.
// Reducing FFT size would decrease equalizer accuracy, particularly for lower frequencies.
constexpr int32_t kSpectrumFFTSize = 512;
constexpr int32_t kSpectrumFFTMagnitude = 9; // 2^9 = 512

// Silence detection thresholds
constexpr int32_t kFFTSilenceThreshold = 100; // FFT magnitude threshold

// Format conversion and display constants
constexpr int32_t kQ31ToQ15Shift = 16;        // Convert from Q31 to Q15 format (31-15 = 16 bits)
constexpr int32_t kSilenceCheckInterval = 16; // Interval for checking silence in loops (reduces CPU usage)

// Static FFT buffers for mono (backward compatibility)
std::array<int32_t, kSpectrumFFTSize> spectrum_fft_input{};
std::array<ne10_fft_cpx_int32_t, kSpectrumFFTOutputSize> spectrum_fft_output{};

// Static FFT buffers for stereo channels
std::array<int32_t, kSpectrumFFTSize> spectrum_fft_input_left{};
std::array<int32_t, kSpectrumFFTSize> spectrum_fft_input_right{};
std::array<ne10_fft_cpx_int32_t, kSpectrumFFTOutputSize> spectrum_fft_output_left{};
std::array<ne10_fft_cpx_int32_t, kSpectrumFFTOutputSize> spectrum_fft_output_right{};

// Precomputed Hanning window in Q31 format
std::array<int32_t, kSpectrumFFTSize> spectrum_hanning_window{};

// FFT result caching to avoid recomputation on every frame
struct CachedFFTResult {
	uint32_t last_write_pos; // Last buffer write position when FFT was computed
	std::array<ne10_fft_cpx_int32_t, kSpectrumFFTOutputSize> cached_output{};
	bool is_valid;

	CachedFFTResult(uint32_t last_write_pos = 0, bool is_valid = false)
	    : last_write_pos(last_write_pos), is_valid(is_valid) {}
};
CachedFFTResult cached_fft{0, false};

// Stereo FFT result caching
struct CachedStereoFFTResult {
	uint32_t last_write_pos; // Last buffer write position when FFT was computed
	std::array<ne10_fft_cpx_int32_t, kSpectrumFFTOutputSize> cached_output_left{};
	std::array<ne10_fft_cpx_int32_t, kSpectrumFFTOutputSize> cached_output_right{};
	bool is_valid;

	CachedStereoFFTResult(uint32_t last_write_pos = 0, bool is_valid = false)
	    : last_write_pos(last_write_pos), is_valid(is_valid) {}
};
CachedStereoFFTResult cached_stereo_fft{0, false};
} // namespace

// Initialize Hanning window coefficients (called once)
void initSpectrumHanningWindow() {
	static bool initialized = false;
	if (initialized) {
		return;
	}
	initialized = true;

	for (int32_t i = 0; i < kSpectrumFFTSize; i++) {
		// Hanning window: w(n) = 0.5 * (1 - cos(2πn/(N-1)))
		// Convert to Q31 format (multiply by 2^31)
		float window_value = 0.0f;
		window_value = 0.5f
		               * (1.0f
		                  - std::cos(2.0f * std::numbers::pi_v<float>
		                             * static_cast<float>(i) / static_cast<float>(kSpectrumFFTSize - 1)));
		spectrum_hanning_window[i] = static_cast<int32_t>(window_value * ONE_Q31f);
	}
}

// Check if FFT output indicates silence by examining representative bins
bool isFFTSilent(const ne10_fft_cpx_int32_t* fft_output, int32_t threshold) {
	int32_t sample_magnitude =
	    fastPythag(fft_output[kSpectrumFFTOutputSize / 2].r, fft_output[kSpectrumFFTOutputSize / 2].i);
	if (sample_magnitude < threshold) {
		// Check a few more bins to confirm silence
		for (int32_t i = 0; i < kSpectrumFFTOutputSize; i += kSilenceCheckInterval) {
			int32_t mag = fastPythag(fft_output[i].r, fft_output[i].i);
			if (mag >= threshold) {
				return false;
			}
		}
		return true;
	}
	return false;
}

// Compute stereo FFT for visualizer with caching optimization
// Returns FFT outputs for both left and right channels
FFTResult computeVisualizerStereoFFT() {
	FFTResult result{nullptr, nullptr, nullptr, false, false, false};

	// Read sample count atomically (single read is safe)
	uint32_t sample_count = 0;
	sample_count = Visualizer::visualizer_sample_count.load(std::memory_order_acquire);
	if (sample_count < kSpectrumFFTSize) {
		// Not enough samples yet
		return result;
	}

	// Get FFT config (lazy initialization)
	ne10_fft_r2c_cfg_int32_t fft_config = FFTConfigManager::getConfig(kSpectrumFFTMagnitude);
	if (fft_config == nullptr) {
		// FFT config not available - this can happen during initialization or if memory allocation fails
		// The visualizer will gracefully degrade by returning an invalid result, which causes
		// the render functions to draw empty/blank displays rather than crashing
		return result;
	}

	// Check if we can use cached stereo FFT result
	uint32_t current_write_pos = 0;
	current_write_pos = Visualizer::visualizer_write_pos.load(std::memory_order_acquire);
	if (cached_stereo_fft.is_valid) {
		// Calculate buffer position difference (handle wrap-around)
		uint32_t pos_diff =
		    (current_write_pos >= cached_stereo_fft.last_write_pos)
		        ? (current_write_pos - cached_stereo_fft.last_write_pos)
		        : (Visualizer::kVisualizerBufferSize - cached_stereo_fft.last_write_pos + current_write_pos);

		// Only recompute if buffer has advanced significantly (>= 1/4 FFT size = 128 samples)
		// Balances real-time responsiveness with CPU performance
		constexpr uint32_t k_fft_cache_threshold = kSpectrumFFTSize / 4;
		if (pos_diff < k_fft_cache_threshold) {
			// Use cached result
			result.output_left = cached_stereo_fft.cached_output_left.data();
			result.output_right = cached_stereo_fft.cached_output_right.data();
			result.isValid = true;
			result.isStereo = true;
			result.isSilent = isFFTSilent(cached_stereo_fft.cached_output_left.data(), kFFTSilenceThreshold)
			                  && isFFTSilent(cached_stereo_fft.cached_output_right.data(), kFFTSilenceThreshold);
			return result;
		}
	}

	// Calculate read start position from circular buffer
	uint32_t read_start_pos = getVisualizerReadStartPos(sample_count);

	// Copy samples and apply Hanning window for left channel
	// Samples are in Q15 format, window is in Q31 format
	// Result: (Q15 * Q31) >> kQ31ToQ15Shift = Q15
	initSpectrumHanningWindow();
	for (int32_t i = 0; i < kSpectrumFFTSize; i++) {
		uint32_t buffer_index = (read_start_pos + i) % Visualizer::kVisualizerBufferSize;
		// Buffer bounds check: visualizer_sample_buffer_left is guaranteed to be at least
		// Visualizer::kVisualizerBufferSize and buffer_index is modulo Visualizer::kVisualizerBufferSize, so it's
		// always valid
		int32_t sample_left = Visualizer::visualizer_sample_buffer_left[buffer_index]; // Q15

		// Apply Hanning window: multiply Q15 sample by Q31 window, shift right by kQ31ToQ15Shift
		// Use 64-bit intermediate to prevent overflow
		int64_t windowed_sample_left =
		    (static_cast<int64_t>(sample_left) * static_cast<int64_t>(spectrum_hanning_window[i])) >> kQ31ToQ15Shift;
		spectrum_fft_input_left[i] = static_cast<int32_t>(windowed_sample_left);
	}

	// Copy samples and apply Hanning window for right channel
	for (int32_t i = 0; i < kSpectrumFFTSize; i++) {
		uint32_t buffer_index = (read_start_pos + i) % Visualizer::kVisualizerBufferSize;
		// Buffer bounds check: visualizer_sample_buffer_right is guaranteed to be at least
		// Visualizer::kVisualizerBufferSize and buffer_index is modulo Visualizer::kVisualizerBufferSize, so it's
		// always valid
		int32_t sample_right = Visualizer::visualizer_sample_buffer_right[buffer_index]; // Q15

		// Apply Hanning window: multiply Q15 sample by Q31 window, shift right by kQ31ToQ15Shift
		// Use 64-bit intermediate to prevent overflow
		int64_t windowed_sample_right =
		    (static_cast<int64_t>(sample_right) * static_cast<int64_t>(spectrum_hanning_window[i])) >> kQ31ToQ15Shift;
		spectrum_fft_input_right[i] = static_cast<int32_t>(windowed_sample_right);
	}

	// Perform FFT on left channel (real-to-complex)
	ne10_fft_r2c_1d_int32_neon(spectrum_fft_output_left.data(), spectrum_fft_input_left.data(), fft_config, 0);

	// Perform FFT on right channel (real-to-complex)
	ne10_fft_r2c_1d_int32_neon(spectrum_fft_output_right.data(), spectrum_fft_input_right.data(), fft_config, 0);

	// Update cache
	cached_stereo_fft.last_write_pos = current_write_pos;
	for (int32_t i = 0; i < kSpectrumFFTOutputSize; i++) {
		cached_stereo_fft.cached_output_left[i] = spectrum_fft_output_left[i];
		cached_stereo_fft.cached_output_right[i] = spectrum_fft_output_right[i];
	}
	cached_stereo_fft.is_valid = true;

	result.output_left = spectrum_fft_output_left.data();
	result.output_right = spectrum_fft_output_right.data();
	result.isValid = true;
	result.isStereo = true;
	result.isSilent = isFFTSilent(spectrum_fft_output_left.data(), kFFTSilenceThreshold)
	                  && isFFTSilent(spectrum_fft_output_right.data(), kFFTSilenceThreshold);
	return result;
}

// Compute FFT for visualizer with caching optimization
// Returns FFT output and validity flags
FFTResult computeVisualizerFFT() {
	FFTResult result{nullptr, nullptr, nullptr, false, false, false};

	// Read sample count atomically (single read is safe)
	uint32_t sample_count = 0;
	sample_count = Visualizer::visualizer_sample_count.load(std::memory_order_acquire);
	if (sample_count < kSpectrumFFTSize) {
		// Not enough samples yet
		return result;
	}

	// Get FFT config (lazy initialization)
	ne10_fft_r2c_cfg_int32_t fft_config = FFTConfigManager::getConfig(kSpectrumFFTMagnitude);
	if (fft_config == nullptr) {
		// FFT config not available - this can happen during initialization or if memory allocation fails
		// The visualizer will gracefully degrade by returning an invalid result, which causes
		// the render functions to draw empty/blank displays rather than crashing
		return result;
	}

	// Check if we can use cached FFT result
	uint32_t current_write_pos = 0;
	current_write_pos = Visualizer::visualizer_write_pos.load(std::memory_order_acquire);
	if (cached_fft.is_valid) {
		// Calculate buffer position difference (handle wrap-around)
		uint32_t pos_diff = (current_write_pos >= cached_fft.last_write_pos)
		                        ? (current_write_pos - cached_fft.last_write_pos)
		                        : (Visualizer::kVisualizerBufferSize - cached_fft.last_write_pos + current_write_pos);

		// Only recompute if buffer has advanced significantly (>= 1/4 FFT size = 128 samples)
		// Balances real-time responsiveness with CPU performance
		constexpr uint32_t k_fft_cache_threshold = kSpectrumFFTSize / 4;
		if (pos_diff < k_fft_cache_threshold) {
			// Use cached result
			result.output = cached_fft.cached_output.data();
			result.isValid = true;
			result.isSilent = isFFTSilent(cached_fft.cached_output.data(), kFFTSilenceThreshold);
			return result;
		}
	}

	// Calculate read start position from circular buffer
	uint32_t read_start_pos = getVisualizerReadStartPos(sample_count);

	// Copy samples and apply Hanning window
	// Samples are in Q15 format, window is in Q31 format
	// Result: (Q15 * Q31) >> kQ31ToQ15Shift = Q15
	initSpectrumHanningWindow();
	for (int32_t i = 0; i < kSpectrumFFTSize; i++) {
		uint32_t buffer_index = (read_start_pos + i) % Visualizer::kVisualizerBufferSize;
		// Buffer bounds check: visualizer_sample_buffer is guaranteed to be at least Visualizer::kVisualizerBufferSize
		// and buffer_index is modulo Visualizer::kVisualizerBufferSize, so it's always valid
		int32_t sample = Visualizer::visualizer_sample_buffer[buffer_index]; // Q15

		// Apply Hanning window: multiply Q15 sample by Q31 window, shift right by kQ31ToQ15Shift
		// Use 64-bit intermediate to prevent overflow
		int64_t windowed_sample =
		    (static_cast<int64_t>(sample) * static_cast<int64_t>(spectrum_hanning_window[i])) >> kQ31ToQ15Shift;
		spectrum_fft_input[i] = static_cast<int32_t>(windowed_sample);
	}

	// Perform FFT (real-to-complex)
	ne10_fft_r2c_1d_int32_neon(spectrum_fft_output.data(), spectrum_fft_input.data(), fft_config, 0);

	// Update cache
	cached_fft.last_write_pos = current_write_pos;
	for (int32_t i = 0; i < kSpectrumFFTOutputSize; i++) {
		cached_fft.cached_output[i] = spectrum_fft_output[i];
	}
	cached_fft.is_valid = true;

	result.output = spectrum_fft_output.data();
	result.isValid = true;
	result.isSilent = isFFTSilent(spectrum_fft_output.data(), kFFTSilenceThreshold);
	return result;
}

// Calculate weighted average magnitude for a frequency band using FFT bin interpolation
// Uses weighted interpolation to smooth transitions between bins, preventing stepping artifacts
// when frequency bands don't align exactly with FFT bins
float calculateWeightedMagnitude(const FFTResult& fft_result, float lower_freq, float upper_freq,
                                 float freq_resolution) {
	// Convert frequencies to FFT bin indices (using floating point for interpolation)
	float start_bin_float = lower_freq / freq_resolution;
	float end_bin_float = upper_freq / freq_resolution;

	// Clamp to valid bin range
	start_bin_float = std::max(0.0f, std::min(start_bin_float, static_cast<float>(kSpectrumFFTOutputSize - 1)));
	end_bin_float = std::max(0.0f, std::min(end_bin_float, static_cast<float>(kSpectrumFFTOutputSize - 1)));
	if (end_bin_float <= start_bin_float) {
		end_bin_float = start_bin_float + 1.0f; // Ensure at least one bin worth of range
	}
	end_bin_float = std::min(end_bin_float, static_cast<float>(kSpectrumFFTOutputSize - 1));

	float weighted_sum = 0.0f;
	float total_weight = 0.0f;

	int32_t start_bin = 0;
	int32_t end_bin = 0;
	start_bin = static_cast<int32_t>(std::floor(start_bin_float));
	end_bin = static_cast<int32_t>(std::floor(end_bin_float));

	// Clamp integer bin indices for safety
	start_bin = std::max(static_cast<int32_t>(0), std::min(start_bin, kSpectrumFFTOutputSize - 1));
	end_bin = std::max(static_cast<int32_t>(0), std::min(end_bin, kSpectrumFFTOutputSize - 1));

	// Handle partial overlap with first bin
	if (start_bin >= 0 && start_bin < kSpectrumFFTOutputSize) {
		float bin_end_freq = static_cast<float>(start_bin + 1) * freq_resolution;
		float overlap_start = 0.0f;
		float overlap_end = 0.0f;
		overlap_start = std::max(lower_freq, static_cast<float>(start_bin) * freq_resolution);
		overlap_end = std::min(upper_freq, bin_end_freq);
		if (overlap_end > overlap_start) {
			float weight = (overlap_end - overlap_start) / freq_resolution;
			int32_t magnitude = fastPythag(fft_result.output[start_bin].r, fft_result.output[start_bin].i);
			if (start_bin == 0) {
				magnitude /= kDcBinReductionFactor; // Reduce DC bin influence by 75%
			}
			weighted_sum += static_cast<float>(magnitude) * weight;
			total_weight += weight;
		}
	}

	// Handle full bins in the middle (if any)
	for (int32_t bin = start_bin + 1; bin < end_bin; bin++) {
		if (bin >= 0 && bin < kSpectrumFFTOutputSize) {
			int32_t magnitude = fastPythag(fft_result.output[bin].r, fft_result.output[bin].i);
			if (bin == 0) {
				magnitude /= kDcBinReductionFactor; // Reduce DC bin influence by 75%
			}
			weighted_sum += static_cast<float>(magnitude);
			total_weight += 1.0f;
		}
	}

	// Handle partial overlap with last bin
	if (end_bin >= 0 && end_bin < kSpectrumFFTOutputSize && end_bin > start_bin) {
		float bin_start_freq = static_cast<float>(end_bin) * freq_resolution;
		float overlap_start = 0.0f;
		float overlap_end = 0.0f;
		overlap_start = std::max(lower_freq, bin_start_freq);
		overlap_end = std::min(upper_freq, static_cast<float>(end_bin + 1) * freq_resolution);
		if (overlap_end > overlap_start) {
			float weight = (overlap_end - overlap_start) / freq_resolution;
			int32_t magnitude = fastPythag(fft_result.output[end_bin].r, fft_result.output[end_bin].i);
			if (end_bin == 0) {
				magnitude /= kDcBinReductionFactor; // Reduce DC bin influence by 75%
			}
			weighted_sum += static_cast<float>(magnitude) * weight;
			total_weight += weight;
		}
	}

	// Return average magnitude (avoid division by zero)
	return (total_weight > 0.0f) ? (weighted_sum / total_weight) : 0.0f;
}

} // namespace deluge::hid::display
