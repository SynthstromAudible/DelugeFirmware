/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "retrospective_buffer.h"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "util/cfunctions.h"
#include <cstring>

extern "C" {
#include "fatfs/ff.h"
}

// Global instance
RetrospectiveBuffer retrospectiveBuffer;

// Session number for this power cycle (0 = not yet determined)
static int32_t currentSessionNumber = 0;

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
	uint32_t durationSetting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerDuration);
	switch (durationSetting) {
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
	uint32_t bitDepthSetting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerBitDepth);
	bytesPerSample_ = (bitDepthSetting == 1) ? 3 : 2; // 0 = 16-bit (2 bytes), 1 = 24-bit (3 bytes)

	// Read channels setting
	uint32_t channelsSetting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerChannels);
	numChannels_ = (channelsSetting == 0) ? 1 : 2; // 0 = mono, 1 = stereo

	// Read source setting
	uint32_t sourceSetting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerSource);
	source_ = (sourceSetting == 0) ? AudioInputChannel::STEREO : AudioInputChannel::MIX;
}

size_t RetrospectiveBuffer::calculateBufferSize() const {
	// duration * sampleRate * channels * bytesPerSample
	return static_cast<size_t>(durationSeconds_) * kSampleRate * numChannels_ * bytesPerSample_;
}

Error RetrospectiveBuffer::init() {
	if (buffer_ != nullptr) {
		return Error::NONE; // Already initialized
	}

	readSettings();

	bufferSizeBytes_ = calculateBufferSize();
	bufferSizeSamples_ = static_cast<size_t>(durationSeconds_) * kSampleRate;

	// Allocate from external SDRAM
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
	uint8_t oldDuration = durationSeconds_;
	uint8_t oldBytesPerSample = bytesPerSample_;
	uint8_t oldNumChannels = numChannels_;

	readSettings();

	size_t newSize = calculateBufferSize();

	// If size hasn't changed, just clear and return
	if (buffer_ != nullptr && newSize == bufferSizeBytes_) {
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
	uint32_t sourceSetting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerSource);
	return (sourceSetting == 0) ? AudioInputChannel::STEREO : AudioInputChannel::MIX;
}

uint8_t RetrospectiveBuffer::getNumChannels() const {
	// Read directly from settings so changes take effect immediately for monitoring
	uint32_t channelsSetting = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RetrospectiveSamplerChannels);
	return (channelsSetting == 0) ? 1 : 2; // 0 = mono, 1 = stereo
}

uint8_t RetrospectiveBuffer::getBytesPerSample() const {
	return bytesPerSample_;
}

uint8_t RetrospectiveBuffer::getDurationSeconds() const {
	return durationSeconds_;
}

bool RetrospectiveBuffer::isEnabled() const {
	return buffer_ != nullptr && runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RetrospectiveSampler);
}

void RetrospectiveBuffer::feedAudio(const deluge::dsp::StereoSample<q31_t>* samples, size_t numSamples) {
	if (!enabled_ || buffer_ == nullptr || numSamples == 0) {
		return;
	}

	const size_t bytesPerFrame = numChannels_ * bytesPerSample_;
	size_t pos = writePos_.load(std::memory_order_relaxed);
	size_t written = samplesWritten_.load(std::memory_order_relaxed);

	// Cache peak tracking state for this batch
	int32_t peak = runningPeak_.load(std::memory_order_relaxed);
	size_t peakPos = peakPosition_.load(std::memory_order_relaxed);
	bool peakIsValid = peakValid_.load(std::memory_order_relaxed);

	for (size_t i = 0; i < numSamples; i++) {
		// Check if we're about to overwrite the peak position
		if (peakIsValid && pos == peakPos) {
			peakIsValid = false;
		}

		size_t byteOffset = pos * bytesPerFrame;
		uint8_t* dest = buffer_ + byteOffset;
		int32_t samplePeak = 0; // Track peak of this sample (at stored bit depth)

		if (bytesPerSample_ == 2) {
			// 16-bit: take the upper 16 bits of the 32-bit sample
			int16_t left = static_cast<int16_t>(samples[i].l >> 16);
			int16_t right = static_cast<int16_t>(samples[i].r >> 16);

			if (numChannels_ == 2) {
				// Stereo
				dest[0] = left & 0xFF;
				dest[1] = (left >> 8) & 0xFF;
				dest[2] = right & 0xFF;
				dest[3] = (right >> 8) & 0xFF;
				samplePeak = std::max(std::abs(static_cast<int32_t>(left)), std::abs(static_cast<int32_t>(right)));
			}
			else {
				// Mono: average L and R
				int16_t mono = static_cast<int16_t>((static_cast<int32_t>(left) + static_cast<int32_t>(right)) >> 1);
				dest[0] = mono & 0xFF;
				dest[1] = (mono >> 8) & 0xFF;
				samplePeak = std::abs(static_cast<int32_t>(mono));
			}
		}
		else {
			// 24-bit: take the upper 24 bits of the 32-bit sample
			int32_t left = samples[i].l >> 8;
			int32_t right = samples[i].r >> 8;

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
				samplePeak = std::max(std::abs(left), std::abs(right));
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
				samplePeak = std::abs(mono);
			}
		}

		// Update peak if this sample is louder
		if (samplePeak >= peak) {
			peak = samplePeak;
			peakPos = pos;
			peakIsValid = true;
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
	peakPosition_.store(peakPos, std::memory_order_relaxed);
	peakValid_.store(peakIsValid, std::memory_order_relaxed);
}

void RetrospectiveBuffer::feedAudioMono(const int32_t* samples, size_t numSamples) {
	if (!enabled_ || buffer_ == nullptr || numSamples == 0) {
		return;
	}

	const size_t bytesPerFrame = numChannels_ * bytesPerSample_;
	size_t pos = writePos_.load(std::memory_order_relaxed);
	size_t written = samplesWritten_.load(std::memory_order_relaxed);

	// Cache peak tracking state for this batch
	int32_t peak = runningPeak_.load(std::memory_order_relaxed);
	size_t peakPos = peakPosition_.load(std::memory_order_relaxed);
	bool peakIsValid = peakValid_.load(std::memory_order_relaxed);

	for (size_t i = 0; i < numSamples; i++) {
		// Check if we're about to overwrite the peak position
		if (peakIsValid && pos == peakPos) {
			peakIsValid = false;
		}

		size_t byteOffset = pos * bytesPerFrame;
		uint8_t* dest = buffer_ + byteOffset;
		int32_t samplePeak = 0;

		if (bytesPerSample_ == 2) {
			// 16-bit: take the upper 16 bits
			int16_t sample = static_cast<int16_t>(samples[i] >> 16);
			samplePeak = std::abs(static_cast<int32_t>(sample));

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
			int32_t sample = samples[i] >> 8;
			// Sign-extend for abs
			int32_t sampleSigned = sample;
			if (sampleSigned & 0x800000) {
				sampleSigned |= 0xFF000000;
			}
			samplePeak = std::abs(sampleSigned);

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
		if (samplePeak >= peak) {
			peak = samplePeak;
			peakPos = pos;
			peakIsValid = true;
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
	peakPosition_.store(peakPos, std::memory_order_relaxed);
	peakValid_.store(peakIsValid, std::memory_order_relaxed);
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
	bool bufferFull = savedSamplesWritten >= bufferSizeSamples_;
	size_t totalSamples = bufferFull ? bufferSizeSamples_ : savedSamplesWritten;

	if (totalSamples == 0) {
		return 0;
	}

	// Skip fade-in region (44 samples) when finding peak, as those will be attenuated
	constexpr size_t kFadeInSamples = 44;

	// Subsample every 8th sample for ~8x speedup. Audio is band-limited so peaks are preserved.
	constexpr size_t kStride = 8;

	int32_t peak = 0;
	size_t peakPos = 0;

	// Determine circular buffer start position
	size_t startPos = bufferFull ? savedWritePos : 0;
	size_t startSample = kFadeInSamples; // Skip fade-in region

	// 16-bit optimized path - direct int16_t access
	if (bytesPerSample_ == 2) {
		const int16_t* buf16 = reinterpret_cast<const int16_t*>(buffer_);
		size_t samplesPerFrame = numChannels_;

		for (size_t i = startSample; i < totalSamples; i += kStride) {
			// Handle circular wrap
			size_t bufIdx = (startPos + i) % bufferSizeSamples_;
			size_t offset = bufIdx * samplesPerFrame;

			// Check all channels at this sample position
			for (size_t ch = 0; ch < numChannels_; ch++) {
				int32_t sample = buf16[offset + ch];
				int32_t absSample = (sample < 0) ? -sample : sample;
				if (absSample > peak) {
					peak = absSample;
					peakPos = bufIdx;
				}
			}
		}
	}
	// 24-bit path - must reconstruct from bytes
	else {
		size_t bytesPerFrame = numChannels_ * 3;

		for (size_t i = startSample; i < totalSamples; i += kStride) {
			// Handle circular wrap
			size_t bufIdx = (startPos + i) % bufferSizeSamples_;
			size_t offset = bufIdx * bytesPerFrame;

			for (size_t ch = 0; ch < numChannels_; ch++) {
				int32_t sample = buffer_[offset] | (buffer_[offset + 1] << 8) | (buffer_[offset + 2] << 16);
				// Sign extend from 24-bit
				if (sample & 0x800000) {
					sample |= 0xFF000000;
				}
				int32_t absSample = (sample < 0) ? -sample : sample;
				if (absSample > peak) {
					peak = absSample;
					peakPos = bufIdx;
				}
				offset += 3;
			}
		}
	}

	// Update cached peak for future calls
	runningPeak_.store(peak, std::memory_order_relaxed);
	peakPosition_.store(peakPos, std::memory_order_relaxed);
	peakValid_.store(true, std::memory_order_relaxed);

	return peak;
}

Error RetrospectiveBuffer::saveToFile(String* filePath) {
	if (buffer_ == nullptr || samplesWritten_.load(std::memory_order_relaxed) == 0) {
		return Error::UNSPECIFIED;
	}

	// Temporarily disable recording to prevent race conditions while saving
	bool wasEnabled = enabled_;
	enabled_ = false;

	// Capture current buffer state with acquire semantics to ensure we see all prior writes
	// from the audio thread. Capture samplesWritten first, then writePos - this ensures we
	// never read unwritten data (if interrupted, we might miss a sample but won't read garbage)
	size_t savedSamplesWritten = samplesWritten_.load(std::memory_order_acquire);
	size_t savedWritePos = writePos_.load(std::memory_order_acquire);

	// Validate the captured state - ensure consistency
	bool bufferWasFull = savedSamplesWritten >= bufferSizeSamples_;
	if (!bufferWasFull) {
		// When buffer not full, writePos should equal samplesWritten
		// If writePos is ahead (due to race), clamp to samplesWritten
		if (savedWritePos > savedSamplesWritten) {
			savedWritePos = savedSamplesWritten;
		}
	}

	// Ensure we have valid data to write
	if (savedSamplesWritten == 0) {
		enabled_ = wasEnabled;
		return Error::UNSPECIFIED;
	}

	// Check if normalization is enabled
	bool normalize = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RetrospectiveSamplerNormalize);
	int32_t peakLevel = 0;
	int32_t maxLevel = (bytesPerSample_ == 2) ? 32767 : 8388607;
	// Target level with headroom (95% of max)
	double targetLevel = static_cast<double>(maxLevel) * 0.95;
	// Use double for gain to avoid fixed-point math issues
	double gainFactor = 1.0;

	if (normalize) {
		peakLevel = findPeakLevel(savedWritePos, savedSamplesWritten);
		if (peakLevel > 0 && static_cast<double>(peakLevel) < targetLevel) {
			gainFactor = targetLevel / static_cast<double>(peakLevel);
			// Cap at 4x gain (~+12dB)
			if (gainFactor > 4.0) {
				gainFactor = 4.0;
			}
		}
		else {
			// Peak is already at or above target, no normalization needed
			normalize = false;
		}
	}

	// Build folder path: SAMPLES/RETRO/{SongName?}/SESSION###/
	char folderPath[128];
	char filename[160];
	FRESULT fres;

	// Create base folders
	f_mkdir("SAMPLES");
	f_mkdir("SAMPLES/RETRO");

	// Start building path
	std::strcpy(folderPath, "SAMPLES/RETRO");

	// Add song name subfolder if song exists and has a name
	if (currentSong != nullptr && !currentSong->name.isEmpty()) {
		std::strcat(folderPath, "/");
		std::strncat(folderPath, currentSong->name.get(), 40); // Limit song name length
		f_mkdir(folderPath);
	}

	// Determine session number on first save this power cycle
	if (currentSessionNumber == 0) {
		currentSessionNumber = findHighestSessionNumber(folderPath) + 1;
	}

	// Add session subfolder
	char sessionFolder[20];
	std::strcpy(sessionFolder, "/SESSION");
	intToString(currentSessionNumber, sessionFolder + 8, 3);
	std::strcat(folderPath, sessionFolder);
	f_mkdir(folderPath);

	// Find an unused filename within this session folder
	int32_t fileNum = 0;
	FIL file;

	while (true) {
		// Build filename: "{folderPath}/RETRXXXX.WAV"
		std::strcpy(filename, folderPath);
		std::strcat(filename, "/RETR");
		intToString(fileNum, filename + std::strlen(filename), 4);
		std::strcat(filename, ".WAV");

		fres = f_open(&file, filename, FA_READ);
		if (fres == FR_NO_FILE) {
			break; // File doesn't exist, we can use this name
		}
		if (fres == FR_OK) {
			f_close(&file);
		}
		fileNum++;
		if (fileNum > 9999) {
			enabled_ = wasEnabled;
			return Error::UNSPECIFIED; // Too many files
		}
	}

	// Open file for writing
	fres = f_open(&file, filename, FA_CREATE_NEW | FA_WRITE);
	if (fres != FR_OK) {
		enabled_ = wasEnabled; // Re-enable before returning
		return Error::SD_CARD;
	}

	// Calculate audio parameters using saved state
	size_t numSamples = (savedSamplesWritten >= bufferSizeSamples_) ? bufferSizeSamples_ : savedSamplesWritten;
	size_t bytesPerFrame = numChannels_ * bytesPerSample_;
	size_t audioDataSize = numSamples * bytesPerFrame;
	uint32_t dataRate = kSampleRate * numChannels_ * bytesPerSample_;
	uint16_t blockAlign = numChannels_ * bytesPerSample_;

	// WAV header buffer (44 bytes for standard WAV)
	uint8_t header[44];
	uint32_t* header32 = reinterpret_cast<uint32_t*>(header);
	uint16_t* header16 = reinterpret_cast<uint16_t*>(header);

	// RIFF chunk
	header32[0] = 0x46464952;         // "RIFF"
	header32[1] = audioDataSize + 36; // File size - 8
	header32[2] = 0x45564157;         // "WAVE"

	// fmt chunk
	header32[3] = 0x20746d66;           // "fmt "
	header32[4] = 16;                   // Chunk size
	header16[10] = 1;                   // Format = PCM
	header16[11] = numChannels_;        // Num channels
	header32[6] = kSampleRate;          // Sample rate
	header32[7] = dataRate;             // Byte rate
	header16[16] = blockAlign;          // Block align
	header16[17] = bytesPerSample_ * 8; // Bits per sample

	// data chunk
	header32[9] = 0x61746164;     // "data"
	header32[10] = audioDataSize; // Chunk size

	// Write header
	UINT bytesWritten;
	fres = f_write(&file, header, 44, &bytesWritten);
	if (fres != FR_OK || bytesWritten != 44) {
		f_close(&file);
		f_unlink(filename);
		enabled_ = wasEnabled; // Re-enable before returning
		return Error::SD_CARD;
	}

	// Write audio data from circular buffer
	// Need to handle the wrap-around: write from savedWritePos to end, then from 0 to savedWritePos
	size_t startPos = bufferWasFull ? savedWritePos : 0;
	size_t samplesToWriteFirst = bufferWasFull ? (bufferSizeSamples_ - savedWritePos) : savedWritePos;
	size_t samplesToWriteSecond = bufferWasFull ? savedWritePos : 0;

	// Track total samples written for fade-in calculation
	size_t totalSamplesWrittenToFile = 0;
	// Fade-in duration: ~1ms at 44.1kHz = 44 samples
	constexpr size_t kFadeInSamples = 44;

	// Pre-compute gain factor in Q16 format (1.0 = 65536) - hoisted out of loop
	int32_t gainFactorFixed = static_cast<int32_t>(gainFactor * 65536.0);

	// Lambda to write samples with optional normalization and fade-in
	auto writeSamples = [&](size_t startSample, size_t numSamplesToWrite) -> bool {
		if (numSamplesToWrite == 0) {
			return true;
		}

		// Check if we need processing (fade-in or normalization)
		bool needsFadeIn = totalSamplesWrittenToFile < kFadeInSamples;
		bool needsProcessing = normalize || needsFadeIn;

		// If no processing needed, write directly
		if (!needsProcessing) {
			size_t bytes = numSamplesToWrite * bytesPerFrame;
			size_t offset = startSample * bytesPerFrame;
			fres = f_write(&file, buffer_ + offset, bytes, &bytesWritten);
			if (fres != FR_OK || bytesWritten != bytes) {
				return false;
			}
			totalSamplesWrittenToFile += numSamplesToWrite;
			return true;
		}

		// Process in chunks - larger chunks reduce SD card write overhead
		constexpr size_t kChunkSamples = 2048;
		uint8_t tempBuffer[kChunkSamples * 6]; // Max: stereo 24-bit = 6 bytes/sample (12KB)

		// For 16-bit, use direct pointer access for reading
		const int16_t* buf16 = (bytesPerSample_ == 2) ? reinterpret_cast<const int16_t*>(buffer_) : nullptr;
		int16_t* tmpBuf16 = (bytesPerSample_ == 2) ? reinterpret_cast<int16_t*>(tempBuffer) : nullptr;

		for (size_t written = 0; written < numSamplesToWrite;) {
			size_t chunkSamples = std::min(kChunkSamples, numSamplesToWrite - written);
			size_t chunkBytes = chunkSamples * bytesPerFrame;

			// Check if this chunk still needs processing
			needsFadeIn = totalSamplesWrittenToFile < kFadeInSamples;
			if (!normalize && !needsFadeIn) {
				// No more processing needed, write rest directly
				size_t remaining = numSamplesToWrite - written;
				size_t bytes = remaining * bytesPerFrame;
				size_t offset = (startSample + written) * bytesPerFrame;
				fres = f_write(&file, buffer_ + offset, bytes, &bytesWritten);
				if (fres != FR_OK || bytesWritten != bytes) {
					return false;
				}
				totalSamplesWrittenToFile += remaining;
				return true;
			}

			// Process samples using fixed-point math
			// Split into fade-in region (first 44 samples) and bulk region
			size_t fadeRemaining =
			    (totalSamplesWrittenToFile < kFadeInSamples) ? (kFadeInSamples - totalSamplesWrittenToFile) : 0;
			size_t fadeSamples = std::min(fadeRemaining, chunkSamples);
			size_t bulkSamples = chunkSamples - fadeSamples;

			if (bytesPerSample_ == 2) {
				// 16-bit path: optimized with direct int16_t access
				size_t srcIdx = (startSample + written) * numChannels_;
				size_t dstIdx = 0;

				// Process fade-in samples (with per-sample fade calculation)
				for (size_t s = 0; s < fadeSamples; s++) {
					size_t sampleIdx = totalSamplesWrittenToFile + s;
					int32_t fadeMultiplier = static_cast<int32_t>((sampleIdx * 65536) / kFadeInSamples);
					int32_t combinedGain =
					    static_cast<int32_t>((static_cast<int64_t>(gainFactorFixed) * fadeMultiplier) >> 16);

					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample = buf16[srcIdx++];
						int32_t processed = static_cast<int32_t>((static_cast<int64_t>(sample) * combinedGain) >> 16);
						if (processed > 32767) {
							processed = 32767;
						}
						else if (processed < -32768) {
							processed = -32768;
						}
						tmpBuf16[dstIdx++] = static_cast<int16_t>(processed);
					}
				}

				// Process bulk samples (constant gain, no fade calculation)
				for (size_t s = 0; s < bulkSamples; s++) {
					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample = buf16[srcIdx++];
						int32_t processed =
						    static_cast<int32_t>((static_cast<int64_t>(sample) * gainFactorFixed) >> 16);
						if (processed > 32767) {
							processed = 32767;
						}
						else if (processed < -32768) {
							processed = -32768;
						}
						tmpBuf16[dstIdx++] = static_cast<int16_t>(processed);
					}
				}
			}
			else {
				// 24-bit path: must use byte access
				size_t srcOffset = (startSample + written) * bytesPerFrame;
				size_t dstOffset = 0;

				// Process fade-in samples
				for (size_t s = 0; s < fadeSamples; s++) {
					size_t sampleIdx = totalSamplesWrittenToFile + s;
					int32_t fadeMultiplier = static_cast<int32_t>((sampleIdx * 65536) / kFadeInSamples);
					int32_t combinedGain =
					    static_cast<int32_t>((static_cast<int64_t>(gainFactorFixed) * fadeMultiplier) >> 16);

					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample =
						    buffer_[srcOffset] | (buffer_[srcOffset + 1] << 8) | (buffer_[srcOffset + 2] << 16);
						if (sample & 0x800000) {
							sample |= 0xFF000000;
						}
						int32_t processed = static_cast<int32_t>((static_cast<int64_t>(sample) * combinedGain) >> 16);
						if (processed > 8388607) {
							processed = 8388607;
						}
						else if (processed < -8388608) {
							processed = -8388608;
						}
						tempBuffer[dstOffset] = processed & 0xFF;
						tempBuffer[dstOffset + 1] = (processed >> 8) & 0xFF;
						tempBuffer[dstOffset + 2] = (processed >> 16) & 0xFF;
						srcOffset += 3;
						dstOffset += 3;
					}
				}

				// Process bulk samples (constant gain)
				for (size_t s = 0; s < bulkSamples; s++) {
					for (size_t ch = 0; ch < numChannels_; ch++) {
						int32_t sample =
						    buffer_[srcOffset] | (buffer_[srcOffset + 1] << 8) | (buffer_[srcOffset + 2] << 16);
						if (sample & 0x800000) {
							sample |= 0xFF000000;
						}
						int32_t processed =
						    static_cast<int32_t>((static_cast<int64_t>(sample) * gainFactorFixed) >> 16);
						if (processed > 8388607) {
							processed = 8388607;
						}
						else if (processed < -8388608) {
							processed = -8388608;
						}
						tempBuffer[dstOffset] = processed & 0xFF;
						tempBuffer[dstOffset + 1] = (processed >> 8) & 0xFF;
						tempBuffer[dstOffset + 2] = (processed >> 16) & 0xFF;
						srcOffset += 3;
						dstOffset += 3;
					}
				}
			}

			fres = f_write(&file, tempBuffer, chunkBytes, &bytesWritten);
			if (fres != FR_OK || bytesWritten != chunkBytes) {
				return false;
			}
			written += chunkSamples;
			totalSamplesWrittenToFile += chunkSamples;
		}
		return true;
	};

	// Write first portion (older samples)
	if (!writeSamples(startPos, samplesToWriteFirst)) {
		f_close(&file);
		f_unlink(filename);
		enabled_ = wasEnabled; // Re-enable before returning
		return Error::SD_CARD;
	}

	// Write second portion (newer samples, after wrap-around)
	if (!writeSamples(0, samplesToWriteSecond)) {
		f_close(&file);
		f_unlink(filename);
		enabled_ = wasEnabled; // Re-enable before returning
		return Error::SD_CARD;
	}

	f_close(&file);

	// Re-enable recording now that save is complete
	enabled_ = wasEnabled;

	// Return the file path
	if (filePath != nullptr) {
		filePath->set(filename);
	}

	return Error::NONE;
}
