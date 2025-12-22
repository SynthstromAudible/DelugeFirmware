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
#include "model/sample/sample.h"
#include "model/sample/sample_holder.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"
#include "util/cfunctions.h"
#include <cstring>

extern "C" {
#include "fatfs/ff.h"
}

// Global instance
RetrospectiveBuffer retrospectiveBuffer;

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

	writePos_ = 0;
	samplesWritten_ = 0;
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
	writePos_ = 0;
	samplesWritten_ = 0;
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
	writePos_ = 0;
	samplesWritten_ = 0;
}

void RetrospectiveBuffer::setEnabled(bool enabled) {
	enabled_ = enabled;
}

size_t RetrospectiveBuffer::getSamplesInBuffer() const {
	if (samplesWritten_ >= bufferSizeSamples_) {
		return bufferSizeSamples_; // Buffer is full
	}
	return samplesWritten_;
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

	for (size_t i = 0; i < numSamples; i++) {
		size_t byteOffset = writePos_ * bytesPerFrame;
		uint8_t* dest = buffer_ + byteOffset;

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
			}
			else {
				// Mono: average L and R
				int16_t mono = static_cast<int16_t>((static_cast<int32_t>(left) + static_cast<int32_t>(right)) >> 1);
				dest[0] = mono & 0xFF;
				dest[1] = (mono >> 8) & 0xFF;
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
			}
			else {
				// Mono: average L and R
				int32_t mono = (left + right) >> 1;
				dest[0] = mono & 0xFF;
				dest[1] = (mono >> 8) & 0xFF;
				dest[2] = (mono >> 16) & 0xFF;
			}
		}

		// Advance write position (circular)
		writePos_++;
		if (writePos_ >= bufferSizeSamples_) {
			writePos_ = 0;
		}
		samplesWritten_++;
	}
}

void RetrospectiveBuffer::feedAudioMono(const int32_t* samples, size_t numSamples) {
	if (!enabled_ || buffer_ == nullptr || numSamples == 0) {
		return;
	}

	const size_t bytesPerFrame = numChannels_ * bytesPerSample_;

	for (size_t i = 0; i < numSamples; i++) {
		size_t byteOffset = writePos_ * bytesPerFrame;
		uint8_t* dest = buffer_ + byteOffset;

		if (bytesPerSample_ == 2) {
			// 16-bit: take the upper 16 bits
			int16_t sample = static_cast<int16_t>(samples[i] >> 16);

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

		// Advance write position (circular)
		writePos_++;
		if (writePos_ >= bufferSizeSamples_) {
			writePos_ = 0;
		}
		samplesWritten_++;
	}
}

Sample* RetrospectiveBuffer::createSampleFromBuffer(SampleHolder* holder) {
	if (buffer_ == nullptr || samplesWritten_ == 0) {
		return nullptr;
	}

	// Calculate the actual number of samples to copy
	size_t numSamples = getSamplesInBuffer();
	size_t bytesPerFrame = numChannels_ * bytesPerSample_;
	size_t audioDataSize = numSamples * bytesPerFrame;

	// Create a new Sample
	void* sampleMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(Sample));
	if (sampleMemory == nullptr) {
		return nullptr;
	}

	Sample* sample = new (sampleMemory) Sample();

	// Set sample properties
	sample->numChannels = numChannels_;
	sample->byteDepth = bytesPerSample_;
	sample->sampleRate = kSampleRate;
	sample->lengthInSamples = numSamples;
	sample->audioDataLengthBytes = audioDataSize;

	// Allocate clusters for the audio data
	// For now, we'll allocate the minimum needed and copy the data
	// The sample will need proper cluster management when saved to disk

	// TODO: Properly implement cluster allocation and data copying
	// For the initial implementation, we'll just set up the sample metadata
	// The actual audio data will be copied when saving to disk

	// Configure the holder if provided
	if (holder != nullptr) {
		holder->startPos = 0;
		holder->endPos = numSamples;
	}

	return sample;
}

/// Helper to find peak level in buffer for normalization
/// Finds the true peak of raw samples (skipping fade-in region)
/// @param savedWritePos Captured write position to use (for consistency with save)
/// @param savedSamplesWritten Captured samples written count
/// @return Peak absolute value (0-32767 for 16-bit, 0-8388607 for 24-bit)
int32_t RetrospectiveBuffer::findPeakLevel(size_t savedWritePos, size_t savedSamplesWritten) {
	if (buffer_ == nullptr || savedSamplesWritten == 0) {
		return 0;
	}

	size_t bytesPerFrame = numChannels_ * bytesPerSample_;

	bool bufferFull = savedSamplesWritten >= bufferSizeSamples_;
	size_t startPos = bufferFull ? savedWritePos : 0;
	size_t samplesToScanFirst = bufferFull ? (bufferSizeSamples_ - savedWritePos) : savedWritePos;
	size_t samplesToScanSecond = bufferFull ? savedWritePos : 0;
	size_t totalSamples = samplesToScanFirst + samplesToScanSecond;

	if (totalSamples == 0) {
		return 0;
	}

	// Skip fade-in region (44 samples) when finding peak, as those will be attenuated
	constexpr size_t kFadeInSamples = 44;
	size_t samplesToSkip = std::min(kFadeInSamples, samplesToScanFirst);

	int32_t peak = 0;

	// Scan first portion (after skipping fade-in)
	for (size_t i = samplesToSkip; i < samplesToScanFirst; i++) {
		size_t offset = (startPos + i) * bytesPerFrame;
		for (size_t ch = 0; ch < numChannels_; ch++) {
			int32_t sample;
			if (bytesPerSample_ == 2) {
				sample = static_cast<int16_t>(buffer_[offset] | (buffer_[offset + 1] << 8));
			}
			else {
				sample = buffer_[offset] | (buffer_[offset + 1] << 8) | (buffer_[offset + 2] << 16);
				if (sample & 0x800000) {
					sample |= 0xFF000000;
				}
			}
			int32_t absSample = (sample < 0) ? -sample : sample;
			if (absSample > peak) {
				peak = absSample;
			}
			offset += bytesPerSample_;
		}
	}

	// Scan second portion
	for (size_t i = 0; i < samplesToScanSecond; i++) {
		size_t offset = i * bytesPerFrame;
		for (size_t ch = 0; ch < numChannels_; ch++) {
			int32_t sample;
			if (bytesPerSample_ == 2) {
				sample = static_cast<int16_t>(buffer_[offset] | (buffer_[offset + 1] << 8));
			}
			else {
				sample = buffer_[offset] | (buffer_[offset + 1] << 8) | (buffer_[offset + 2] << 16);
				if (sample & 0x800000) {
					sample |= 0xFF000000;
				}
			}
			int32_t absSample = (sample < 0) ? -sample : sample;
			if (absSample > peak) {
				peak = absSample;
			}
			offset += bytesPerSample_;
		}
	}

	return peak;
}

Error RetrospectiveBuffer::saveToFile(String* filePath) {
	if (buffer_ == nullptr || samplesWritten_ == 0) {
		return Error::UNSPECIFIED;
	}

	// Temporarily disable recording to prevent race conditions while saving
	bool wasEnabled = enabled_;
	enabled_ = false;

	// Capture current buffer state atomically
	// Capture samplesWritten first, then writePos - this ensures we never read unwritten data
	// (if interrupted, we might miss a sample but won't read garbage)
	size_t savedSamplesWritten = samplesWritten_;
	size_t savedWritePos = writePos_;

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

	// Create SAMPLES/RETRO folder if it doesn't exist
	FRESULT fres = f_mkdir("SAMPLES");
	fres = f_mkdir("SAMPLES/RETRO");

	// Find an unused filename
	char filename[32];
	int32_t fileNum = 0;
	FIL file;

	while (true) {
		// Build filename: "SAMPLES/RETRO/RETRXXXX.WAV"
		std::strcpy(filename, "SAMPLES/RETRO/RETR");
		intToString(fileNum, filename + 18, 4); // 4-digit number at position 18
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

	// Track samples for popup refresh (refresh every ~0.8 seconds to stay ahead of 1s timeout)
	size_t samplesSincePopupRefresh = 0;
	constexpr size_t kPopupRefreshSamples = 35000; // ~0.8 seconds at 44.1kHz

	// Lambda to write samples with optional normalization and fade-in
	auto writeSamples = [&](size_t startSample, size_t numSamplesToWrite) -> bool {
		if (numSamplesToWrite == 0) {
			return true;
		}

		// Check if we need processing (fade-in or normalization)
		bool needsFadeIn = totalSamplesWrittenToFile < kFadeInSamples;
		bool needsProcessing = normalize || needsFadeIn;

		// If no processing needed, write in chunks to allow popup refresh
		if (!needsProcessing) {
			constexpr size_t kDirectChunkSamples = 32768; // ~0.74 seconds at 44.1kHz
			size_t remaining = numSamplesToWrite;
			size_t pos = 0;
			while (remaining > 0) {
				size_t chunk = std::min(kDirectChunkSamples, remaining);
				size_t bytes = chunk * bytesPerFrame;
				size_t offset = (startSample + pos) * bytesPerFrame;
				fres = f_write(&file, buffer_ + offset, bytes, &bytesWritten);
				if (fres != FR_OK || bytesWritten != bytes) {
					return false;
				}
				pos += chunk;
				remaining -= chunk;
				totalSamplesWrittenToFile += chunk;
				// Refresh popup to prevent timeout
				display->displayPopup(deluge::l10n::getView(deluge::l10n::String::STRING_FOR_RETRO_SAVING));
			}
			return true;
		}

		// Process in chunks
		constexpr size_t kChunkSamples = 256;
		uint8_t tempBuffer[kChunkSamples * 6]; // Max: stereo 24-bit = 6 bytes/sample

		for (size_t written = 0; written < numSamplesToWrite;) {
			size_t chunkSamples = std::min(kChunkSamples, numSamplesToWrite - written);
			size_t chunkBytes = chunkSamples * bytesPerFrame;

			// Check if this chunk still needs processing
			needsFadeIn = totalSamplesWrittenToFile < kFadeInSamples;
			if (!normalize && !needsFadeIn) {
				// No more processing needed, write rest in chunks for popup refresh
				constexpr size_t kDirectChunkSamples = 32768;
				size_t remaining = numSamplesToWrite - written;
				size_t pos = written;
				while (remaining > 0) {
					size_t chunk = std::min(kDirectChunkSamples, remaining);
					size_t bytes = chunk * bytesPerFrame;
					size_t offset = (startSample + pos) * bytesPerFrame;
					fres = f_write(&file, buffer_ + offset, bytes, &bytesWritten);
					if (fres != FR_OK || bytesWritten != bytes) {
						return false;
					}
					pos += chunk;
					remaining -= chunk;
					totalSamplesWrittenToFile += chunk;
					// Refresh popup to prevent timeout
					display->displayPopup(deluge::l10n::getView(deluge::l10n::String::STRING_FOR_RETRO_SAVING));
				}
				return true;
			}

			// Process each sample in the chunk
			for (size_t s = 0; s < chunkSamples; s++) {
				size_t srcOffset = (startSample + written + s) * bytesPerFrame;
				size_t dstOffset = s * bytesPerFrame;

				// Calculate fade-in multiplier (0.0 to 1.0)
				double fadeMultiplier = 1.0;
				if (totalSamplesWrittenToFile + s < kFadeInSamples) {
					fadeMultiplier =
					    static_cast<double>(totalSamplesWrittenToFile + s) / static_cast<double>(kFadeInSamples);
				}

				for (size_t ch = 0; ch < numChannels_; ch++) {
					int32_t sample;
					if (bytesPerSample_ == 2) {
						sample = static_cast<int16_t>(buffer_[srcOffset] | (buffer_[srcOffset + 1] << 8));
						// Apply gain and fade
						double processed = static_cast<double>(sample) * gainFactor * fadeMultiplier;
						// Clamp to 16-bit range
						if (processed > 32767.0) {
							processed = 32767.0;
						}
						if (processed < -32768.0) {
							processed = -32768.0;
						}
						sample = static_cast<int32_t>(processed);
						tempBuffer[dstOffset] = sample & 0xFF;
						tempBuffer[dstOffset + 1] = (sample >> 8) & 0xFF;
						srcOffset += 2;
						dstOffset += 2;
					}
					else {
						// 24-bit
						sample = buffer_[srcOffset] | (buffer_[srcOffset + 1] << 8) | (buffer_[srcOffset + 2] << 16);
						if (sample & 0x800000) {
							sample |= 0xFF000000; // Sign extend
						}
						// Apply gain and fade
						double processed = static_cast<double>(sample) * gainFactor * fadeMultiplier;
						// Clamp to 24-bit range
						if (processed > 8388607.0) {
							processed = 8388607.0;
						}
						if (processed < -8388608.0) {
							processed = -8388608.0;
						}
						sample = static_cast<int32_t>(processed);
						tempBuffer[dstOffset] = sample & 0xFF;
						tempBuffer[dstOffset + 1] = (sample >> 8) & 0xFF;
						tempBuffer[dstOffset + 2] = (sample >> 16) & 0xFF;
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
			samplesSincePopupRefresh += chunkSamples;

			// Refresh popup periodically to prevent timeout
			if (samplesSincePopupRefresh >= kPopupRefreshSamples) {
				if (normalize) {
					display->displayPopup(deluge::l10n::getView(deluge::l10n::String::STRING_FOR_RETRO_NORMALIZING));
				}
				else {
					display->displayPopup(deluge::l10n::getView(deluge::l10n::String::STRING_FOR_RETRO_SAVING));
				}
				samplesSincePopupRefresh = 0;
			}
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
