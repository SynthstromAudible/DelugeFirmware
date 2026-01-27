/*
 * Copyright © 2024-2025 Owlet Records
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
 *
 * --- Additional terms under GNU GPL version 3 section 7 ---
 * This file requires preservation of the above copyright notice and author attribution
 * in all copies or substantial portions of this file.
 */

#include "retrospective_buffer.h"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "memory/memory_allocator_interface.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "util/cfunctions.h"
#include "util/functions.h"
#include <algorithm>
#include <cstring>

extern "C" {
#include "fatfs/ff.h"
}

// Global instance
RetrospectiveBuffer retrospectiveBuffer;

// Session number for this power cycle (0 = not yet determined)
static int32_t current_session_number = 0;

/// Scan a folder for the highest SESSION### subfolder number
/// @param basePath The folder to scan (e.g., "SAMPLES/RETRO" or "SAMPLES/RETRO/SongName")
/// @return The highest session number found, or 0 if none exist
static int32_t findHighestSessionNumber(const char* basePath) {
	DIR dir;
	FILINFO fno;
	int32_t highest = 0;

	FRESULT res = f_opendir(&dir, basePath);
	if (res != FR_OK) {
		return 0;
	}

	while (true) {
		res = f_readdir(&dir, &fno);
		if (res != FR_OK || fno.fname[0] == 0) {
			break;
		}

		// Check if it's a directory starting with "SESSION"
		if ((fno.fattrib & AM_DIR) && std::strncmp(fno.fname, "SESSION", 7) == 0) {
			// Parse the number after "SESSION"
			int32_t num = std::atoi(fno.fname + 7);
			if (num > highest) {
				highest = num;
			}
		}
	}

	f_closedir(&dir);
	return highest;
}

RetrospectiveBuffer::~RetrospectiveBuffer() {
	deinit();
}

void RetrospectiveBuffer::readSettings() {
	// Read duration setting
	uint32_t duration_setting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerDuration);
	switch (duration_setting) {
	case 0:
		durationSeconds_ = 5;
		break;
	case 1:
		durationSeconds_ = 15;
		break;
	case 2:
		durationSeconds_ = 30;
		break;
	case 3:
		durationSeconds_ = 60;
		break;
	default:
		durationSeconds_ = 5;
		break;
	}

	// Read bit depth setting
	uint32_t bit_depth_setting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerBitDepth);
	bytesPerSample_ = (bit_depth_setting == 1) ? 3 : 2; // 0 = 16-bit (2 bytes), 1 = 24-bit (3 bytes)

	// Read channels setting
	uint32_t channels_setting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerChannels);
	numChannels_ = (channels_setting == 0) ? 1 : 2; // 0 = mono, 1 = stereo

	// Read source setting
	uint32_t source_setting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerSource);
	source_ = (source_setting == 0) ? AudioInputChannel::STEREO : AudioInputChannel::MIX;
}

size_t RetrospectiveBuffer::calculateBufferSize() const {
	// For bar modes, allocate buffer scaled to bar count (6 seconds per bar covers 40 BPM minimum)
	if (isBarMode()) {
		constexpr uint32_t kSecondsPerBarAt40BPM = 6;
		uint8_t bars = getBarCount();
		uint32_t buffer_seconds = bars * kSecondsPerBarAt40BPM;
		return buffer_seconds * kSampleRate * numChannels_ * bytesPerSample_;
	}
	// duration * sampleRate * channels * bytesPerSample
	return static_cast<size_t>(durationSeconds_) * kSampleRate * numChannels_ * bytesPerSample_;
}

Error RetrospectiveBuffer::init() {
	if (buffer_ != nullptr) {
		return Error::NONE; // Already initialized
	}

	readSettings();

	bufferSizeBytes_ = calculateBufferSize();
	// Calculate samples from bytes (handles both time and bar modes)
	bufferSizeSamples_ = bufferSizeBytes_ / (numChannels_ * bytesPerSample_);

	// Allocate from SDRAM using low speed allocator (stealable region)
	buffer_ = static_cast<uint8_t*>(GeneralMemoryAllocator::get().allocLowSpeed(bufferSizeBytes_));
	if (buffer_ == nullptr) {
		bufferSizeBytes_ = 0;
		bufferSizeSamples_ = 0;
		return Error::INSUFFICIENT_RAM;
	}

	// Clear the buffer
	std::memset(buffer_, 0, bufferSizeBytes_);

	writePos_.store(0, std::memory_order_relaxed);
	samplesWritten_.store(0, std::memory_order_relaxed);
	runningPeak_.store(0, std::memory_order_relaxed);
	peakPosition_.store(0, std::memory_order_relaxed);
	peakValid_.store(false, std::memory_order_relaxed);
	enabled_ = true;

	return Error::NONE;
}

void RetrospectiveBuffer::deinit() {
	if (buffer_ != nullptr) {
		GeneralMemoryAllocator::get().dealloc(buffer_);
		buffer_ = nullptr;
	}
	bufferSizeBytes_ = 0;
	bufferSizeSamples_ = 0;
	writePos_.store(0, std::memory_order_relaxed);
	samplesWritten_.store(0, std::memory_order_relaxed);
	runningPeak_.store(0, std::memory_order_relaxed);
	peakPosition_.store(0, std::memory_order_relaxed);
	peakValid_.store(false, std::memory_order_relaxed);
	enabled_ = false;
}

Error RetrospectiveBuffer::reinit() {
	// Check if settings have changed
	uint8_t old_duration = durationSeconds_;
	uint8_t old_bytes_per_sample = bytesPerSample_;
	uint8_t old_num_channels = numChannels_;

	readSettings();

	size_t new_size = calculateBufferSize();

	// If size hasn't changed, just clear and return
	if (buffer_ != nullptr && new_size == bufferSizeBytes_) {
		clear();
		return Error::NONE;
	}

	// Size changed, need to reallocate
	deinit();
	return init();
}

void RetrospectiveBuffer::clear() {
	if (buffer_ != nullptr) {
		std::memset(buffer_, 0, bufferSizeBytes_);
	}
	writePos_.store(0, std::memory_order_relaxed);
	samplesWritten_.store(0, std::memory_order_relaxed);
	runningPeak_.store(0, std::memory_order_relaxed);
	peakPosition_.store(0, std::memory_order_relaxed);
	peakValid_.store(false, std::memory_order_relaxed);
}

void RetrospectiveBuffer::setEnabled(bool enabled) {
	enabled_ = enabled;
}

size_t RetrospectiveBuffer::getSamplesInBuffer() const {
	size_t written = samplesWritten_.load(std::memory_order_relaxed);
	if (written >= bufferSizeSamples_) {
		return bufferSizeSamples_; // Buffer is full
	}
	return written;
}

AudioInputChannel RetrospectiveBuffer::getSource() const {
	// Read directly from settings so changes take effect immediately
	uint32_t source_setting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerSource);
	return (source_setting == 0) ? AudioInputChannel::STEREO : AudioInputChannel::MIX;
}

uint8_t RetrospectiveBuffer::getNumChannels() const {
	// Read directly from settings so changes take effect immediately for monitoring
	uint32_t channels_setting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerChannels);
	return (channels_setting == 0) ? 1 : 2; // 0 = mono, 1 = stereo
}

uint8_t RetrospectiveBuffer::getBytesPerSample() const {
	return bytesPerSample_;
}

uint8_t RetrospectiveBuffer::getDurationSeconds() const {
	return durationSeconds_;
}

bool RetrospectiveBuffer::isBarMode() const {
	uint32_t duration_setting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerDuration);
	return duration_setting >= RuntimeFeatureStateRetroDuration::Bars1;
}

uint8_t RetrospectiveBuffer::getBarCount() const {
	uint32_t duration_setting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerDuration);
	switch (duration_setting) {
	case RuntimeFeatureStateRetroDuration::Bars1:
		return 1;
	case RuntimeFeatureStateRetroDuration::Bars2:
		return 2;
	case RuntimeFeatureStateRetroDuration::Bars4:
		return 4;
	default:
		return 0; // Not in bar mode
	}
}

bool RetrospectiveBuffer::isFocusedTrackMode() const {
	uint32_t source_setting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerSource);
	return source_setting == RuntimeFeatureStateRetroSource::FocusedTrack;
}

bool RetrospectiveBuffer::isEnabled() const {
	return buffer_ != nullptr && runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RetrospectiveSampler);
}

void RetrospectiveBuffer::feedAudio(const StereoSample* samples, size_t numSamples, bool skipPendingSaveCheck) {
	if (!enabled_ || buffer_ == nullptr || numSamples == 0) {
		return;
	}

	// Check for pending bar-synced save (skip when called from interrupt-disabled context)
	if (!skipPendingSaveCheck) {
		checkAndExecutePendingSave();
	}

	const size_t bytes_per_frame = numChannels_ * bytesPerSample_;
	size_t pos = writePos_.load(std::memory_order_relaxed);
	size_t written = samplesWritten_.load(std::memory_order_relaxed);

	// Only apply gain when normalization is OFF - normalization will handle levels otherwise
	// and we want to preserve headroom to avoid clipping before normalization
	bool apply_gain = !runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RetrospectiveSamplerNormalize);

	// Cache peak tracking state for this batch
	int32_t peak = runningPeak_.load(std::memory_order_relaxed);
	size_t peak_pos = peakPosition_.load(std::memory_order_relaxed);
	bool peak_is_valid = peakValid_.load(std::memory_order_relaxed);

	for (size_t i = 0; i < numSamples; i++) {
		// Check if we're about to overwrite the peak position
		if (peak_is_valid && pos == peak_pos) {
			peak_is_valid = false;
		}

		// Apply +5 bit gain only when normalization is off
		// Internal mixing level is ~8 bits below DAC output; +5 matches stem export
		int32_t sample_l = apply_gain ? lshiftAndSaturate<5>(samples[i].l) : samples[i].l;
		int32_t sample_r = apply_gain ? lshiftAndSaturate<5>(samples[i].r) : samples[i].r;

		size_t byte_offset = pos * bytes_per_frame;
		uint8_t* dest = buffer_ + byte_offset;
		int32_t sample_peak = 0; // Track peak of this sample (at stored bit depth)

		if (bytesPerSample_ == 2) {
			// 16-bit with TPDF dither to eliminate quantization distortion
			// Generate two random values using LCG and subtract for triangular distribution
			// Dither range is ±1 LSB at 16-bit (±65536 in 32-bit domain)
			ditherState_ = ditherState_ * 1664525u + 1013904223u;
			int32_t rand1 = static_cast<int32_t>(ditherState_ & 0xFFFF); // [0, 65535]
			ditherState_ = ditherState_ * 1664525u + 1013904223u;
			int32_t rand2 = static_cast<int32_t>(ditherState_ & 0xFFFF); // [0, 65535]
			int32_t dither = rand1 - rand2;                              // Triangular distribution (-65535, +65535)

			// Apply dither before truncation (use 64-bit to avoid overflow)
			int32_t left_dithered = static_cast<int32_t>(
			    std::clamp(static_cast<int64_t>(sample_l) + dither, (int64_t)INT32_MIN, (int64_t)INT32_MAX));
			int32_t right_dithered = static_cast<int32_t>(
			    std::clamp(static_cast<int64_t>(sample_r) + dither, (int64_t)INT32_MIN, (int64_t)INT32_MAX));

			int16_t left = static_cast<int16_t>(left_dithered >> 16);
			int16_t right = static_cast<int16_t>(right_dithered >> 16);

			if (numChannels_ == 2) {
				// Stereo
				dest[0] = left & 0xFF;
				dest[1] = (left >> 8) & 0xFF;
				dest[2] = right & 0xFF;
				dest[3] = (right >> 8) & 0xFF;
				sample_peak = std::max(std::abs(static_cast<int32_t>(left)), std::abs(static_cast<int32_t>(right)));
			}
			else {
				// Mono: average L and R
				int16_t mono = static_cast<int16_t>((static_cast<int32_t>(left) + static_cast<int32_t>(right)) >> 1);
				dest[0] = mono & 0xFF;
				dest[1] = (mono >> 8) & 0xFF;
				sample_peak = std::abs(static_cast<int32_t>(mono));
			}
		}
		else {
			// 24-bit: take the upper 24 bits of the gained sample
			int32_t left = sample_l >> 8;
			int32_t right = sample_r >> 8;

			if (numChannels_ == 2) {
				// Stereo
				dest[0] = left & 0xFF;
				dest[1] = (left >> 8) & 0xFF;
				dest[2] = (left >> 16) & 0xFF;
				dest[3] = right & 0xFF;
				dest[4] = (right >> 8) & 0xFF;
				dest[5] = (right >> 16) & 0xFF;
				// Sign-extend for abs (24-bit in 32-bit container)
				if (left & 0x800000) {
					left |= 0xFF000000;
				}
				if (right & 0x800000) {
					right |= 0xFF000000;
				}
				sample_peak = std::max(std::abs(left), std::abs(right));
			}
			else {
				// Mono: average L and R
				int32_t mono = (left + right) >> 1;
				dest[0] = mono & 0xFF;
				dest[1] = (mono >> 8) & 0xFF;
				dest[2] = (mono >> 16) & 0xFF;
				if (mono & 0x800000) {
					mono |= 0xFF000000;
				}
				sample_peak = std::abs(mono);
			}
		}

		// Update peak if this sample is louder
		if (sample_peak >= peak) {
			peak = sample_peak;
			peak_pos = pos;
			peak_is_valid = true;
		}

		// Advance write position (circular)
		pos++;
		if (pos >= bufferSizeSamples_) {
			pos = 0;
		}
		written++;
	}

	writePos_.store(pos, std::memory_order_relaxed);
	samplesWritten_.store(written, std::memory_order_relaxed);
	runningPeak_.store(peak, std::memory_order_relaxed);
	peakPosition_.store(peak_pos, std::memory_order_relaxed);
	peakValid_.store(peak_is_valid, std::memory_order_relaxed);
}

void RetrospectiveBuffer::feedAudioMono(const int32_t* samples, size_t numSamples) {
	if (!enabled_ || buffer_ == nullptr || numSamples == 0) {
		return;
	}

	const size_t bytes_per_frame = numChannels_ * bytesPerSample_;
	size_t pos = writePos_.load(std::memory_order_relaxed);
	size_t written = samplesWritten_.load(std::memory_order_relaxed);

	// Only apply gain when normalization is OFF
	bool apply_gain = !runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RetrospectiveSamplerNormalize);

	// Cache peak tracking state for this batch
	int32_t peak = runningPeak_.load(std::memory_order_relaxed);
	size_t peak_pos = peakPosition_.load(std::memory_order_relaxed);
	bool peak_is_valid = peakValid_.load(std::memory_order_relaxed);

	for (size_t i = 0; i < numSamples; i++) {
		// Check if we're about to overwrite the peak position
		if (peak_is_valid && pos == peak_pos) {
			peak_is_valid = false;
		}

		// Apply +5 bit gain only when normalization is off
		int32_t gained_sample = apply_gain ? lshiftAndSaturate<5>(samples[i]) : samples[i];

		size_t byte_offset = pos * bytes_per_frame;
		uint8_t* dest = buffer_ + byte_offset;
		int32_t sample_peak = 0;

		if (bytesPerSample_ == 2) {
			// 16-bit with TPDF dither
			ditherState_ = ditherState_ * 1664525u + 1013904223u;
			int32_t rand1 = static_cast<int32_t>(ditherState_ & 0xFFFF);
			ditherState_ = ditherState_ * 1664525u + 1013904223u;
			int32_t rand2 = static_cast<int32_t>(ditherState_ & 0xFFFF);
			int32_t dither = rand1 - rand2;

			int32_t dithered = static_cast<int32_t>(
			    std::clamp(static_cast<int64_t>(gained_sample) + dither, (int64_t)INT32_MIN, (int64_t)INT32_MAX));
			int16_t sample = static_cast<int16_t>(dithered >> 16);
			sample_peak = std::abs(static_cast<int32_t>(sample));

			if (numChannels_ == 2) {
				// Duplicate to stereo
				dest[0] = sample & 0xFF;
				dest[1] = (sample >> 8) & 0xFF;
				dest[2] = sample & 0xFF;
				dest[3] = (sample >> 8) & 0xFF;
			}
			else {
				dest[0] = sample & 0xFF;
				dest[1] = (sample >> 8) & 0xFF;
			}
		}
		else {
			// 24-bit
			int32_t sample = gained_sample >> 8;
			// Sign-extend for abs
			int32_t sample_signed = sample;
			if (sample_signed & 0x800000) {
				sample_signed |= 0xFF000000;
			}
			sample_peak = std::abs(sample_signed);

			if (numChannels_ == 2) {
				// Duplicate to stereo
				dest[0] = sample & 0xFF;
				dest[1] = (sample >> 8) & 0xFF;
				dest[2] = (sample >> 16) & 0xFF;
				dest[3] = sample & 0xFF;
				dest[4] = (sample >> 8) & 0xFF;
				dest[5] = (sample >> 16) & 0xFF;
			}
			else {
				dest[0] = sample & 0xFF;
				dest[1] = (sample >> 8) & 0xFF;
				dest[2] = (sample >> 16) & 0xFF;
			}
		}

		// Update peak if this sample is louder
		if (sample_peak >= peak) {
			peak = sample_peak;
			peak_pos = pos;
			peak_is_valid = true;
		}

		// Advance write position (circular)
		pos++;
		if (pos >= bufferSizeSamples_) {
			pos = 0;
		}
		written++;
	}

	writePos_.store(pos, std::memory_order_relaxed);
	samplesWritten_.store(written, std::memory_order_relaxed);
	runningPeak_.store(peak, std::memory_order_relaxed);
	peakPosition_.store(peak_pos, std::memory_order_relaxed);
	peakValid_.store(peak_is_valid, std::memory_order_relaxed);
}

/// Helper to find peak level in buffer for normalization
/// Uses incrementally tracked peak when valid, falls back to scan if peak was overwritten.
/// @param savedWritePos Captured write position to use (for consistency with save)
/// @param savedSamplesWritten Captured samples written count
/// @return Peak absolute value (0-32767 for 16-bit, 0-8388607 for 24-bit)
int32_t RetrospectiveBuffer::findPeakLevel(size_t savedWritePos, size_t savedSamplesWritten) {
	if (buffer_ == nullptr || savedSamplesWritten == 0) {
		return 0;
	}

	// Fast path: use cached peak if still valid
	if (peakValid_.load(std::memory_order_relaxed)) {
		return runningPeak_.load(std::memory_order_relaxed);
	}

	// Slow path: peak position was overwritten, need to rescan
	bool buffer_full = savedSamplesWritten >= bufferSizeSamples_;
	size_t total_samples = buffer_full ? bufferSizeSamples_ : savedSamplesWritten;

	if (total_samples == 0) {
		return 0;
	}

	// Skip fade-in region (44 samples) when finding peak, as those will be attenuated
	constexpr size_t kFadeInSamples = 44;

	// Subsample every 8th sample for ~8x speedup. Audio is band-limited so peaks are preserved.
	constexpr size_t kStride = 8;

	int32_t peak = 0;
	size_t peak_pos = 0;

	// Determine circular buffer start position
	size_t start_pos = buffer_full ? savedWritePos : 0;
	size_t start_sample = kFadeInSamples; // Skip fade-in region

	// 16-bit optimized path - direct int16_t access
	if (bytesPerSample_ == 2) {
		const int16_t* buf16 = reinterpret_cast<const int16_t*>(buffer_);
		size_t samples_per_frame = numChannels_;

		for (size_t i = start_sample; i < total_samples; i += kStride) {
			// Handle circular wrap
			size_t buf_idx = (start_pos + i) % bufferSizeSamples_;
			size_t offset = buf_idx * samples_per_frame;

			// Check all channels at this sample position
			for (size_t ch = 0; ch < numChannels_; ch++) {
				int32_t sample = buf16[offset + ch];
				int32_t abs_sample = (sample < 0) ? -sample : sample;
				if (abs_sample > peak) {
					peak = abs_sample;
					peak_pos = buf_idx;
				}
			}
		}
	}
	// 24-bit path - must reconstruct from bytes
	else {
		size_t bytes_per_frame = numChannels_ * 3;

		for (size_t i = start_sample; i < total_samples; i += kStride) {
			// Handle circular wrap
			size_t buf_idx = (start_pos + i) % bufferSizeSamples_;
			size_t offset = buf_idx * bytes_per_frame;

			for (size_t ch = 0; ch < numChannels_; ch++) {
				int32_t sample = buffer_[offset] | (buffer_[offset + 1] << 8) | (buffer_[offset + 2] << 16);
				// Sign extend from 24-bit
				if (sample & 0x800000) {
					sample |= 0xFF000000;
				}
				int32_t abs_sample = (sample < 0) ? -sample : sample;
				if (abs_sample > peak) {
					peak = abs_sample;
					peak_pos = buf_idx;
				}
				offset += 3;
			}
		}
	}

	// Update cached peak for future calls
	runningPeak_.store(peak, std::memory_order_relaxed);
	peakPosition_.store(peak_pos, std::memory_order_relaxed);
	peakValid_.store(true, std::memory_order_relaxed);

	return peak;
}

Error RetrospectiveBuffer::saveToFile(String* filePath) {
	if (buffer_ == nullptr || samplesWritten_.load(std::memory_order_relaxed) == 0) {
		return Error::UNSPECIFIED;
	}

	// Temporarily disable recording to prevent race conditions while saving
	bool was_enabled = enabled_;
	enabled_ = false;

	// Capture current buffer state with acquire semantics to ensure we see all prior writes
	// from the audio thread. Capture samplesWritten first, then writePos - this ensures we
	// never read unwritten data (if interrupted, we might miss a sample but won't read garbage)
	size_t saved_samples_written = samplesWritten_.load(std::memory_order_acquire);
	size_t saved_write_pos = writePos_.load(std::memory_order_acquire);

	// Validate the captured state - ensure consistency
	bool buffer_was_full = saved_samples_written >= bufferSizeSamples_;
	if (!buffer_was_full) {
		// When buffer not full, writePos should equal samplesWritten
		// If writePos is ahead (due to race), clamp to samplesWritten
		if (saved_write_pos > saved_samples_written) {
			saved_write_pos = saved_samples_written;
		}
	}

	// Ensure we have valid data to write
	if (saved_samples_written == 0) {
		enabled_ = was_enabled;
		return Error::UNSPECIFIED;
	}

	// Check if normalization is enabled
	bool normalize = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RetrospectiveSamplerNormalize);
	int32_t peak_level = 0;
	int32_t max_level = (bytesPerSample_ == 2) ? 32767 : 8388607;
	// Target level with headroom (95% of max)
	double target_level = static_cast<double>(max_level) * 0.95;
	// Use double for gain to avoid fixed-point math issues
	double gain_factor = 1.0;

	if (normalize) {
		peak_level = findPeakLevel(saved_write_pos, saved_samples_written);
		if (peak_level > 0 && static_cast<double>(peak_level) < target_level) {
			gain_factor = target_level / static_cast<double>(peak_level);
			// Cap at 128x gain (~+42dB) for external input which may need significant boost
			if (gain_factor > 128.0) {
				gain_factor = 128.0;
			}
		}
		else {
			// Peak is already at or above target, no normalization needed
			normalize = false;
		}
	}

	// Build folder path: SAMPLES/RETRO/{SongName?}/SESSION###/
	char folder_path[128];
	char filename[160];
	FRESULT fres;

	// Create base folders
	f_mkdir("SAMPLES");
	f_mkdir("SAMPLES/RETRO");

	// Start building path
	std::strcpy(folder_path, "SAMPLES/RETRO");

	// Add song name subfolder if song exists and has a name
	if (currentSong != nullptr && !currentSong->name.isEmpty()) {
		std::strcat(folder_path, "/");
		std::strncat(folder_path, currentSong->name.get(), 40); // Limit song name length
		f_mkdir(folder_path);
	}

	// Determine session number on first save this power cycle
	if (current_session_number == 0) {
		current_session_number = findHighestSessionNumber(folder_path) + 1;
	}

	// Add session subfolder
	char session_folder[20];
	std::strcpy(session_folder, "/SESSION");
	intToString(current_session_number, session_folder + 8, 3);
	std::strcat(folder_path, session_folder);
	f_mkdir(folder_path);

	// Find an unused filename within this session folder
	int32_t file_num = 0;
	FIL file;

	while (true) {
		// Build filename: "{folder_path}/RETRXXXX.WAV"
		std::strcpy(filename, folder_path);
		std::strcat(filename, "/RETR");
		intToString(file_num, filename + std::strlen(filename), 4);
		std::strcat(filename, ".WAV");

		fres = f_open(&file, filename, FA_READ);
		if (fres == FR_NO_FILE) {
			break; // File doesn't exist, we can use this name
		}
		if (fres == FR_OK) {
			f_close(&file);
		}
		file_num++;
		if (file_num > 9999) {
			enabled_ = was_enabled;
			return Error::UNSPECIFIED; // Too many files
		}
	}

	// Open file for writing
	fres = f_open(&file, filename, FA_CREATE_NEW | FA_WRITE);
	if (fres != FR_OK) {
		enabled_ = was_enabled; // Re-enable before returning
		return Error::SD_CARD;
	}

	// Calculate audio parameters using saved state
	size_t num_samples = (saved_samples_written >= bufferSizeSamples_) ? bufferSizeSamples_ : saved_samples_written;
	size_t bytes_per_frame = numChannels_ * bytesPerSample_;
	size_t audio_data_size = num_samples * bytes_per_frame;
	uint32_t data_rate = kSampleRate * numChannels_ * bytesPerSample_;
	uint16_t block_align = numChannels_ * bytesPerSample_;

	// WAV header buffer (44 bytes for standard WAV)
	uint8_t header[44];
	uint32_t* header32 = reinterpret_cast<uint32_t*>(header);
	uint16_t* header16 = reinterpret_cast<uint16_t*>(header);

	// RIFF chunk
	header32[0] = 0x46464952;           // "RIFF"
	header32[1] = audio_data_size + 36; // File size - 8
	header32[2] = 0x45564157;           // "WAVE"

	// fmt chunk
	header32[3] = 0x20746d66;           // "fmt "
	header32[4] = 16;                   // Chunk size
	header16[10] = 1;                   // Format = PCM
	header16[11] = numChannels_;        // Num channels
	header32[6] = kSampleRate;          // Sample rate
	header32[7] = data_rate;            // Byte rate
	header16[16] = block_align;         // Block align
	header16[17] = bytesPerSample_ * 8; // Bits per sample

	// data chunk
	header32[9] = 0x61746164;       // "data"
	header32[10] = audio_data_size; // Chunk size

	// Write header
	UINT bytes_written;
	fres = f_write(&file, header, 44, &bytes_written);
	if (fres != FR_OK || bytes_written != 44) {
		f_close(&file);
		f_unlink(filename);
		enabled_ = was_enabled; // Re-enable before returning
		return Error::SD_CARD;
	}

	// Write audio data from circular buffer
	// Need to handle the wrap-around: write from saved_write_pos to end, then from 0 to saved_write_pos
	size_t start_pos = buffer_was_full ? saved_write_pos : 0;
	size_t samples_to_write_first = buffer_was_full ? (bufferSizeSamples_ - saved_write_pos) : saved_write_pos;
	size_t samples_to_write_second = buffer_was_full ? saved_write_pos : 0;

	// Track total samples written for fade-in calculation
	size_t total_samples_written_to_file = 0;
	// Fade-in duration: ~1ms at 44.1kHz = 44 samples
	constexpr size_t kFadeInSamples = 44;

	// Pre-compute gain factor in Q16 format (1.0 = 65536) - hoisted out of loop
	int32_t gain_factor_fixed = static_cast<int32_t>(gain_factor * 65536.0);

	// Lambda to write samples with optional normalization and fade-in
	auto write_samples = [&](size_t start_sample, size_t num_samples_to_write) -> bool {
		if (num_samples_to_write == 0) {
			return true;
		}

		// Check if we need processing (fade-in or normalization)
		bool needs_fade_in = total_samples_written_to_file < kFadeInSamples;
		bool needs_processing = normalize || needs_fade_in;

		// If no processing needed, write directly
		if (!needs_processing) {
			size_t bytes = num_samples_to_write * bytes_per_frame;
			size_t offset = start_sample * bytes_per_frame;
			fres = f_write(&file, buffer_ + offset, bytes, &bytes_written);
			if (fres != FR_OK || bytes_written != bytes) {
				return false;
			}
			total_samples_written_to_file += num_samples_to_write;
			return true;
		}

		// Process in chunks - larger chunks reduce SD card write overhead
		constexpr size_t kChunkSamples = 2048;
		uint8_t temp_buffer[kChunkSamples * 6]; // Max: stereo 24-bit = 6 bytes/sample (12KB)

		// For 16-bit, use direct pointer access for reading
		const int16_t* buf16 = (bytesPerSample_ == 2) ? reinterpret_cast<const int16_t*>(buffer_) : nullptr;
		int16_t* tmp_buf16 = (bytesPerSample_ == 2) ? reinterpret_cast<int16_t*>(temp_buffer) : nullptr;

		for (size_t written = 0; written < num_samples_to_write;) {
			size_t chunk_samples = std::min(kChunkSamples, num_samples_to_write - written);
			size_t chunk_bytes = chunk_samples * bytes_per_frame;

			// Check if this chunk still needs processing
			needs_fade_in = total_samples_written_to_file < kFadeInSamples;
			if (!normalize && !needs_fade_in) {
				// No more processing needed, write rest directly
				size_t remaining = num_samples_to_write - written;
				size_t bytes = remaining * bytes_per_frame;
				size_t offset = (start_sample + written) * bytes_per_frame;
				fres = f_write(&file, buffer_ + offset, bytes, &bytes_written);
				if (fres != FR_OK || bytes_written != bytes) {
					return false;
				}
				total_samples_written_to_file += remaining;
				return true;
			}

			// Process samples using fixed-point math
			// Split into fade-in region (first 44 samples) and bulk region
			size_t fade_remaining =
			    (total_samples_written_to_file < kFadeInSamples) ? (kFadeInSamples - total_samples_written_to_file) : 0;
			size_t fade_samples = std::min(fade_remaining, chunk_samples);
			size_t bulk_samples = chunk_samples - fade_samples;

			if (bytesPerSample_ == 2) {
				// 16-bit path: optimized with direct int16_t access
				size_t src_idx = (start_sample + written) * numChannels_;
				size_t dst_idx = 0;

				// Process fade-in samples (with per-sample fade calculation)
				for (size_t s = 0; s < fade_samples; s++) {
					size_t sample_idx = total_samples_written_to_file + s;
					int32_t fade_multiplier = static_cast<int32_t>((sample_idx * 65536) / kFadeInSamples);
					int32_t combined_gain =
					    static_cast<int32_t>((static_cast<int64_t>(gain_factor_fixed) * fade_multiplier) >> 16);

					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample = buf16[src_idx++];
						int32_t processed = static_cast<int32_t>((static_cast<int64_t>(sample) * combined_gain) >> 16);
						if (processed > 32767) {
							processed = 32767;
						}
						else if (processed < -32768) {
							processed = -32768;
						}
						tmp_buf16[dst_idx++] = static_cast<int16_t>(processed);
					}
				}

				// Process bulk samples (constant gain, no fade calculation)
				for (size_t s = 0; s < bulk_samples; s++) {
					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample = buf16[src_idx++];
						int32_t processed =
						    static_cast<int32_t>((static_cast<int64_t>(sample) * gain_factor_fixed) >> 16);
						if (processed > 32767) {
							processed = 32767;
						}
						else if (processed < -32768) {
							processed = -32768;
						}
						tmp_buf16[dst_idx++] = static_cast<int16_t>(processed);
					}
				}
			}
			else {
				// 24-bit path: must use byte access
				size_t src_offset = (start_sample + written) * bytes_per_frame;
				size_t dst_offset = 0;

				// Process fade-in samples
				for (size_t s = 0; s < fade_samples; s++) {
					size_t sample_idx = total_samples_written_to_file + s;
					int32_t fade_multiplier = static_cast<int32_t>((sample_idx * 65536) / kFadeInSamples);
					int32_t combined_gain =
					    static_cast<int32_t>((static_cast<int64_t>(gain_factor_fixed) * fade_multiplier) >> 16);

					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample =
						    buffer_[src_offset] | (buffer_[src_offset + 1] << 8) | (buffer_[src_offset + 2] << 16);
						if (sample & 0x800000) {
							sample |= 0xFF000000;
						}
						int32_t processed = static_cast<int32_t>((static_cast<int64_t>(sample) * combined_gain) >> 16);
						if (processed > 8388607) {
							processed = 8388607;
						}
						else if (processed < -8388608) {
							processed = -8388608;
						}
						temp_buffer[dst_offset] = processed & 0xFF;
						temp_buffer[dst_offset + 1] = (processed >> 8) & 0xFF;
						temp_buffer[dst_offset + 2] = (processed >> 16) & 0xFF;
						src_offset += 3;
						dst_offset += 3;
					}
				}

				// Process bulk samples (constant gain)
				for (size_t s = 0; s < bulk_samples; s++) {
					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample =
						    buffer_[src_offset] | (buffer_[src_offset + 1] << 8) | (buffer_[src_offset + 2] << 16);
						if (sample & 0x800000) {
							sample |= 0xFF000000;
						}
						int32_t processed =
						    static_cast<int32_t>((static_cast<int64_t>(sample) * gain_factor_fixed) >> 16);
						if (processed > 8388607) {
							processed = 8388607;
						}
						else if (processed < -8388608) {
							processed = -8388608;
						}
						temp_buffer[dst_offset] = processed & 0xFF;
						temp_buffer[dst_offset + 1] = (processed >> 8) & 0xFF;
						temp_buffer[dst_offset + 2] = (processed >> 16) & 0xFF;
						src_offset += 3;
						dst_offset += 3;
					}
				}
			}

			fres = f_write(&file, temp_buffer, chunk_bytes, &bytes_written);
			if (fres != FR_OK || bytes_written != chunk_bytes) {
				return false;
			}
			written += chunk_samples;
			total_samples_written_to_file += chunk_samples;
		}
		return true;
	};

	// Write first portion (older samples)
	if (!write_samples(start_pos, samples_to_write_first)) {
		f_close(&file);
		f_unlink(filename);
		enabled_ = was_enabled; // Re-enable before returning
		return Error::SD_CARD;
	}

	// Write second portion (newer samples, after wrap-around)
	if (!write_samples(0, samples_to_write_second)) {
		f_close(&file);
		f_unlink(filename);
		enabled_ = was_enabled; // Re-enable before returning
		return Error::SD_CARD;
	}

	f_close(&file);

	// Re-enable recording now that save is complete
	enabled_ = was_enabled;

	// Return the file path
	if (filePath != nullptr) {
		filePath->set(filename);
	}

	return Error::NONE;
}

size_t RetrospectiveBuffer::calculateBarSyncedSamples() const {
	if (!isBarMode() || currentSong == nullptr) {
		return bufferSizeSamples_;
	}

	// Use the same formula as scatter effect for samples-per-bar calculation
	uint64_t time_per_tick_big = playbackHandler.getTimePerInternalTickBig();
	uint32_t bar_length_in_ticks = currentSong->getBarLength();

	// Formula: samples = (ticks * time_per_tick_big) >> 32
	size_t samples_per_bar = ((uint64_t)bar_length_in_ticks * time_per_tick_big) >> 32;

	uint8_t bars = getBarCount();
	size_t total_samples = samples_per_bar * bars;

	// Clamp to buffer size
	return std::min(total_samples, bufferSizeSamples_);
}

Error RetrospectiveBuffer::requestBarSyncedSave(String* filePath) {
	if (!isBarMode()) {
		// Not in bar mode - fall back to immediate save
		return saveToFile(filePath);
	}

	if (!playbackHandler.isEitherClockActive() || currentSong == nullptr) {
		// Transport not running - fall back to immediate save
		return saveToFile(filePath);
	}

	if (pendingSave_.load(std::memory_order_relaxed)) {
		return Error::UNSPECIFIED; // Already pending
	}

	// Calculate next downbeat tick
	int64_t current_tick = playbackHandler.getActualSwungTickCount();
	uint32_t bar_length = currentSong->getBarLength();

	// Find next bar boundary
	int64_t ticks_into_current_bar = current_tick % bar_length;
	int64_t target_tick = current_tick + (bar_length - ticks_into_current_bar);

	// Capture BPM at trigger time
	float bpm = playbackHandler.calculateBPMForDisplay();

	// Store pending state
	savedBPM_.store(bpm, std::memory_order_relaxed);
	saveTargetTick_.store(target_tick, std::memory_order_relaxed);
	pendingFilePath_ = filePath;
	pendingSave_.store(true, std::memory_order_release);

	return Error::NONE;
}

void RetrospectiveBuffer::cancelPendingSave() {
	pendingSave_.store(false, std::memory_order_release);
	pendingFilePath_ = nullptr;
}

void RetrospectiveBuffer::checkAndExecutePendingSave() {
	if (!pendingSave_.load(std::memory_order_acquire)) {
		return;
	}

	int64_t current_tick = playbackHandler.getActualSwungTickCount();
	int64_t target_tick = saveTargetTick_.load(std::memory_order_relaxed);

	if (current_tick >= target_tick) {
		// Downbeat reached - execute save
		executePendingSave();
	}
}

void RetrospectiveBuffer::executePendingSave() {
	if (!pendingSave_.load(std::memory_order_relaxed)) {
		return;
	}

	// Clear pending flag first to prevent re-entry
	pendingSave_.store(false, std::memory_order_release);

	// Calculate exact samples to save based on bar count
	size_t samples_to_save = calculateBarSyncedSamples();

	// Save with BPM tag
	float bpm = savedBPM_.load(std::memory_order_relaxed);
	Error error = saveToFileWithBPM(pendingFilePath_, samples_to_save, bpm);

	// Show result to user
	if (error == Error::NONE && pendingFilePath_ != nullptr) {
		// Extract just the filename for display
		const char* full_path = pendingFilePath_->get();
		const char* filename = full_path;
		for (const char* p = full_path; *p; p++) {
			if (*p == '/') {
				filename = p + 1;
			}
		}
		display->displayPopup(filename);
	}
	else {
		display->displayPopup("FAIL");
	}

	pendingFilePath_ = nullptr;
}

Error RetrospectiveBuffer::saveToFileWithBPM(String* filePath, size_t maxSamples, float bpm) {
	if (buffer_ == nullptr || samplesWritten_.load(std::memory_order_relaxed) == 0) {
		return Error::UNSPECIFIED;
	}

	// Temporarily disable recording to prevent race conditions while saving
	bool was_enabled = enabled_;
	enabled_ = false;

	// Capture current buffer state
	size_t saved_samples_written = samplesWritten_.load(std::memory_order_acquire);
	size_t saved_write_pos = writePos_.load(std::memory_order_acquire);

	// Validate the captured state
	bool buffer_was_full = saved_samples_written >= bufferSizeSamples_;
	if (!buffer_was_full) {
		if (saved_write_pos > saved_samples_written) {
			saved_write_pos = saved_samples_written;
		}
	}

	if (saved_samples_written == 0) {
		enabled_ = was_enabled;
		return Error::UNSPECIFIED;
	}

	// Limit samples to save based on maxSamples and what's available
	size_t num_samples = buffer_was_full ? bufferSizeSamples_ : saved_samples_written;
	num_samples = std::min(num_samples, maxSamples);

	// Check if normalization is enabled
	bool normalize = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RetrospectiveSamplerNormalize);
	int32_t peak_level = 0;
	int32_t max_level = (bytesPerSample_ == 2) ? 32767 : 8388607;
	double target_level = static_cast<double>(max_level) * 0.95;
	double gain_factor = 1.0;

	if (normalize) {
		peak_level = findPeakLevel(saved_write_pos, saved_samples_written);
		if (peak_level > 0 && static_cast<double>(peak_level) < target_level) {
			gain_factor = target_level / static_cast<double>(peak_level);
			// Cap at 128x gain (~+42dB) for external input which may need significant boost
			if (gain_factor > 128.0) {
				gain_factor = 128.0;
			}
		}
		else {
			normalize = false;
		}
	}

	// Build folder path
	char folder_path[128];
	char filename[160];
	FRESULT fres;

	f_mkdir("SAMPLES");
	f_mkdir("SAMPLES/RETRO");
	std::strcpy(folder_path, "SAMPLES/RETRO");

	if (currentSong != nullptr && !currentSong->name.isEmpty()) {
		std::strcat(folder_path, "/");
		std::strncat(folder_path, currentSong->name.get(), 40);
		f_mkdir(folder_path);
	}

	if (current_session_number == 0) {
		current_session_number = findHighestSessionNumber(folder_path) + 1;
	}

	char session_folder[20];
	std::strcpy(session_folder, "/SESSION");
	intToString(current_session_number, session_folder + 8, 3);
	std::strcat(folder_path, session_folder);
	f_mkdir(folder_path);

	// Find unused filename with BPM tag
	int32_t file_num = 0;
	FIL file;
	uint8_t bars = getBarCount();
	int32_t bpm_int = static_cast<int32_t>(bpm + 0.5f); // Round to integer

	while (true) {
		// Build filename: "{folder_path}/RETR{num}_{bars}BAR_{bpm}BPM.WAV"
		std::strcpy(filename, folder_path);
		std::strcat(filename, "/RETR");
		intToString(file_num, filename + std::strlen(filename), 4);
		std::strcat(filename, "_");
		intToString(bars, filename + std::strlen(filename), 1);
		std::strcat(filename, "BAR_");
		intToString(bpm_int, filename + std::strlen(filename), 3);
		std::strcat(filename, "BPM.WAV");

		fres = f_open(&file, filename, FA_READ);
		if (fres == FR_NO_FILE) {
			break;
		}
		if (fres == FR_OK) {
			f_close(&file);
		}
		file_num++;
		if (file_num > 9999) {
			enabled_ = was_enabled;
			return Error::UNSPECIFIED;
		}
	}

	fres = f_open(&file, filename, FA_CREATE_NEW | FA_WRITE);
	if (fres != FR_OK) {
		enabled_ = was_enabled;
		return Error::SD_CARD;
	}

	// Calculate audio parameters
	size_t bytes_per_frame = numChannels_ * bytesPerSample_;
	size_t audio_data_size = num_samples * bytes_per_frame;
	uint32_t data_rate = kSampleRate * numChannels_ * bytesPerSample_;
	uint16_t block_align = numChannels_ * bytesPerSample_;

	// WAV header (44 bytes)
	uint8_t header[44];
	uint32_t* header32 = reinterpret_cast<uint32_t*>(header);
	uint16_t* header16 = reinterpret_cast<uint16_t*>(header);

	header32[0] = 0x46464952;           // "RIFF"
	header32[1] = audio_data_size + 36; // File size - 8
	header32[2] = 0x45564157;           // "WAVE"
	header32[3] = 0x20746d66;           // "fmt "
	header32[4] = 16;                   // Chunk size
	header16[10] = 1;                   // Format = PCM
	header16[11] = numChannels_;        // Num channels
	header32[6] = kSampleRate;          // Sample rate
	header32[7] = data_rate;            // Byte rate
	header16[16] = block_align;         // Block align
	header16[17] = bytesPerSample_ * 8; // Bits per sample
	header32[9] = 0x61746164;           // "data"
	header32[10] = audio_data_size;     // Chunk size

	UINT bytes_written;
	fres = f_write(&file, header, 44, &bytes_written);
	if (fres != FR_OK || bytes_written != 44) {
		f_close(&file);
		f_unlink(filename);
		enabled_ = was_enabled;
		return Error::SD_CARD;
	}

	// For bar-synced save, we want the LAST N samples (most recent bars)
	// Calculate the start position for the data we want
	size_t data_start_pos;
	if (buffer_was_full) {
		// Buffer is full - oldest data is at saved_write_pos
		// We want the last num_samples, so start from (saved_write_pos - num_samples) mod bufferSize
		// But since we save in time order, calculate properly
		if (saved_samples_written > num_samples) {
			// We have more samples than we need - skip the oldest ones
			size_t samples_to_skip = bufferSizeSamples_ - num_samples;
			data_start_pos = (saved_write_pos + samples_to_skip) % bufferSizeSamples_;
		}
		else {
			data_start_pos = saved_write_pos;
		}
	}
	else {
		// Buffer not full - data starts at 0
		if (saved_samples_written > num_samples) {
			data_start_pos = saved_samples_written - num_samples;
		}
		else {
			data_start_pos = 0;
		}
	}

	// Track for fade-in
	size_t total_samples_written_to_file = 0;
	constexpr size_t kFadeInSamples = 44;
	int32_t gain_factor_fixed = static_cast<int32_t>(gain_factor * 65536.0);

	// Write audio data
	auto write_samples = [&](size_t start_sample, size_t num_samples_to_write) -> bool {
		if (num_samples_to_write == 0) {
			return true;
		}

		bool needs_fade_in = total_samples_written_to_file < kFadeInSamples;
		bool needs_processing = normalize || needs_fade_in;

		if (!needs_processing) {
			size_t bytes = num_samples_to_write * bytes_per_frame;
			size_t offset = start_sample * bytes_per_frame;
			fres = f_write(&file, buffer_ + offset, bytes, &bytes_written);
			if (fres != FR_OK || bytes_written != bytes) {
				return false;
			}
			total_samples_written_to_file += num_samples_to_write;
			return true;
		}

		constexpr size_t kChunkSamples = 2048;
		uint8_t temp_buffer[kChunkSamples * 6];
		const int16_t* buf16 = (bytesPerSample_ == 2) ? reinterpret_cast<const int16_t*>(buffer_) : nullptr;
		int16_t* tmp_buf16 = (bytesPerSample_ == 2) ? reinterpret_cast<int16_t*>(temp_buffer) : nullptr;

		for (size_t written = 0; written < num_samples_to_write;) {
			size_t chunk_samples = std::min(kChunkSamples, num_samples_to_write - written);
			size_t chunk_bytes = chunk_samples * bytes_per_frame;

			needs_fade_in = total_samples_written_to_file < kFadeInSamples;
			if (!normalize && !needs_fade_in) {
				size_t remaining = num_samples_to_write - written;
				size_t bytes = remaining * bytes_per_frame;
				size_t offset = (start_sample + written) * bytes_per_frame;
				fres = f_write(&file, buffer_ + offset, bytes, &bytes_written);
				if (fres != FR_OK || bytes_written != bytes) {
					return false;
				}
				total_samples_written_to_file += remaining;
				return true;
			}

			size_t fade_remaining =
			    (total_samples_written_to_file < kFadeInSamples) ? (kFadeInSamples - total_samples_written_to_file) : 0;
			size_t fade_samples = std::min(fade_remaining, chunk_samples);
			size_t bulk_samples = chunk_samples - fade_samples;

			if (bytesPerSample_ == 2) {
				size_t src_idx = (start_sample + written) * numChannels_;
				size_t dst_idx = 0;

				for (size_t s = 0; s < fade_samples; s++) {
					size_t sample_idx = total_samples_written_to_file + s;
					int32_t fade_multiplier = static_cast<int32_t>((sample_idx * 65536) / kFadeInSamples);
					int32_t combined_gain =
					    static_cast<int32_t>((static_cast<int64_t>(gain_factor_fixed) * fade_multiplier) >> 16);

					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample = buf16[src_idx++];
						int32_t processed = static_cast<int32_t>((static_cast<int64_t>(sample) * combined_gain) >> 16);
						if (processed > 32767) {
							processed = 32767;
						}
						else if (processed < -32768) {
							processed = -32768;
						}
						tmp_buf16[dst_idx++] = static_cast<int16_t>(processed);
					}
				}

				for (size_t s = 0; s < bulk_samples; s++) {
					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample = buf16[src_idx++];
						int32_t processed =
						    static_cast<int32_t>((static_cast<int64_t>(sample) * gain_factor_fixed) >> 16);
						if (processed > 32767) {
							processed = 32767;
						}
						else if (processed < -32768) {
							processed = -32768;
						}
						tmp_buf16[dst_idx++] = static_cast<int16_t>(processed);
					}
				}
			}
			else {
				size_t src_offset = (start_sample + written) * bytes_per_frame;
				size_t dst_offset = 0;

				for (size_t s = 0; s < fade_samples; s++) {
					size_t sample_idx = total_samples_written_to_file + s;
					int32_t fade_multiplier = static_cast<int32_t>((sample_idx * 65536) / kFadeInSamples);
					int32_t combined_gain =
					    static_cast<int32_t>((static_cast<int64_t>(gain_factor_fixed) * fade_multiplier) >> 16);

					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample =
						    buffer_[src_offset] | (buffer_[src_offset + 1] << 8) | (buffer_[src_offset + 2] << 16);
						if (sample & 0x800000) {
							sample |= 0xFF000000;
						}
						int32_t processed = static_cast<int32_t>((static_cast<int64_t>(sample) * combined_gain) >> 16);
						if (processed > 8388607) {
							processed = 8388607;
						}
						else if (processed < -8388608) {
							processed = -8388608;
						}
						temp_buffer[dst_offset] = processed & 0xFF;
						temp_buffer[dst_offset + 1] = (processed >> 8) & 0xFF;
						temp_buffer[dst_offset + 2] = (processed >> 16) & 0xFF;
						src_offset += 3;
						dst_offset += 3;
					}
				}

				for (size_t s = 0; s < bulk_samples; s++) {
					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample =
						    buffer_[src_offset] | (buffer_[src_offset + 1] << 8) | (buffer_[src_offset + 2] << 16);
						if (sample & 0x800000) {
							sample |= 0xFF000000;
						}
						int32_t processed =
						    static_cast<int32_t>((static_cast<int64_t>(sample) * gain_factor_fixed) >> 16);
						if (processed > 8388607) {
							processed = 8388607;
						}
						else if (processed < -8388608) {
							processed = -8388608;
						}
						temp_buffer[dst_offset] = processed & 0xFF;
						temp_buffer[dst_offset + 1] = (processed >> 8) & 0xFF;
						temp_buffer[dst_offset + 2] = (processed >> 16) & 0xFF;
						src_offset += 3;
						dst_offset += 3;
					}
				}
			}

			fres = f_write(&file, temp_buffer, chunk_bytes, &bytes_written);
			if (fres != FR_OK || bytes_written != chunk_bytes) {
				return false;
			}
			written += chunk_samples;
			total_samples_written_to_file += chunk_samples;
		}
		return true;
	};

	// Write data handling circular buffer wrap
	if (buffer_was_full) {
		// Calculate how much data is after data_start_pos and how much wraps around
		size_t samples_after_start = bufferSizeSamples_ - data_start_pos;
		size_t samples_to_write_first = std::min(samples_after_start, num_samples);
		size_t samples_to_write_second = num_samples - samples_to_write_first;

		if (!write_samples(data_start_pos, samples_to_write_first)) {
			f_close(&file);
			f_unlink(filename);
			enabled_ = was_enabled;
			return Error::SD_CARD;
		}

		if (samples_to_write_second > 0 && !write_samples(0, samples_to_write_second)) {
			f_close(&file);
			f_unlink(filename);
			enabled_ = was_enabled;
			return Error::SD_CARD;
		}
	}
	else {
		if (!write_samples(data_start_pos, num_samples)) {
			f_close(&file);
			f_unlink(filename);
			enabled_ = was_enabled;
			return Error::SD_CARD;
		}
	}

	f_close(&file);
	enabled_ = was_enabled;

	if (filePath != nullptr) {
		filePath->set(filename);
	}

	return Error::NONE;
}
