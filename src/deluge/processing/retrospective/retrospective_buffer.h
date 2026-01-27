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

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include "util/d_string.h"
#include <atomic>
#include <cstdint>
#include <span>

/// Retrospective sampling buffer that continuously records audio in a rolling buffer.
/// When triggered, the buffer contents can be saved to a sample for editing.
class RetrospectiveBuffer {
public:
	static constexpr uint32_t kSampleRate = 44100;

	RetrospectiveBuffer() = default;
	~RetrospectiveBuffer();

	/// Initialize the buffer based on current runtime feature settings.
	/// Allocates memory in external SDRAM.
	/// @return Error::NONE on success, or an error code if allocation fails.
	Error init();

	/// Free the buffer memory.
	void deinit();

	/// Reinitialize the buffer if settings have changed.
	/// This will clear any existing audio data.
	/// @return Error::NONE on success, or an error code if allocation fails.
	Error reinit();

	/// Feed audio samples into the circular buffer.
	/// This is called from the audio engine during each render window.
	/// Must be lock-free and fast for real-time safety.
	/// @param samples Pointer to stereo samples to record
	/// @param numSamples Number of samples to record
	/// @param skipPendingSaveCheck If true, skip checking for pending bar-synced saves.
	///        Set this when calling from interrupt-disabled context (e.g., focused track path).
	void feedAudio(const StereoSample* samples, size_t numSamples, bool skipPendingSaveCheck = false);

	/// Feed mono audio samples into the buffer (will be duplicated to stereo if buffer is stereo).
	/// @param samples Pointer to mono samples to record
	/// @param numSamples Number of samples to record
	void feedAudioMono(const int32_t* samples, size_t numSamples);

	/// Save the buffer contents to a WAV file.
	/// @param filePath Output parameter to receive the saved file path
	/// @return Error::NONE on success, or an error code on failure
	Error saveToFile(String* filePath);

	/// Check if the buffer is enabled (feature is on and buffer is allocated).
	[[nodiscard]] bool isEnabled() const;

	/// Check if the buffer has any audio data.
	[[nodiscard]] bool hasAudio() const { return samplesWritten_.load(std::memory_order_relaxed) > 0; }

	/// Get the current buffer size in bytes.
	[[nodiscard]] size_t getBufferSizeBytes() const { return bufferSizeBytes_; }

	/// Get the number of samples currently in the buffer.
	[[nodiscard]] size_t getSamplesInBuffer() const;

	/// Get the configured source (input or master output).
	[[nodiscard]] AudioInputChannel getSource() const;

	/// Get the configured number of channels (1 for mono, 2 for stereo).
	[[nodiscard]] uint8_t getNumChannels() const;

	/// Get the configured bit depth (2 for 16-bit, 3 for 24-bit).
	[[nodiscard]] uint8_t getBytesPerSample() const;

	/// Get the configured duration in seconds.
	[[nodiscard]] uint8_t getDurationSeconds() const;

	/// Check if current mode is bar-synced (vs time-based).
	[[nodiscard]] bool isBarMode() const;

	/// Get number of bars for bar mode (1, 2, or 4). Returns 0 if not in bar mode.
	[[nodiscard]] uint8_t getBarCount() const;

	/// Check if focused track mode is active.
	[[nodiscard]] bool isFocusedTrackMode() const;

	/// Request a bar-synced save. Sets up pending state and returns immediately.
	/// When transport is running, save happens at next downbeat.
	/// When transport is stopped, falls back to immediate save.
	/// @param filePath Output parameter to receive the saved file path
	/// @return Error::NONE if save scheduled/completed, error code otherwise
	Error requestBarSyncedSave(String* filePath);

	/// Check if a bar-synced save is pending
	[[nodiscard]] bool hasPendingSave() const { return pendingSave_.load(std::memory_order_relaxed); }

	/// Cancel any pending bar-synced save
	void cancelPendingSave();

	/// Check for pending save and execute if downbeat reached.
	/// Called from audio thread during feedAudio().
	void checkAndExecutePendingSave();

	/// Clear the buffer contents without deallocating.
	void clear();

	/// Enable or disable the buffer. When disabled, audio is not recorded.
	void setEnabled(bool enabled);

private:
	/// Calculate the buffer size based on current settings.
	size_t calculateBufferSize() const;

	/// Read settings from runtime feature settings.
	void readSettings();

	/// Find the peak level in the buffer for normalization.
	/// @param savedWritePos Captured write position to use (for consistency with save)
	/// @param savedSamplesWritten Captured samples written count
	/// @return Peak absolute sample value
	int32_t findPeakLevel(size_t savedWritePos, size_t savedSamplesWritten);

	uint8_t* buffer_ = nullptr;             ///< Circular buffer in external SDRAM
	size_t bufferSizeBytes_ = 0;            ///< Actual allocated buffer size in bytes
	size_t bufferSizeSamples_ = 0;          ///< Buffer capacity in samples
	std::atomic<size_t> writePos_{0};       ///< Current write position in samples
	std::atomic<size_t> samplesWritten_{0}; ///< Total samples written (to know if buffer is full)
	bool enabled_ = false;                  ///< Whether recording is active

	// Incremental peak tracking for fast normalization
	std::atomic<int32_t> runningPeak_{0}; ///< Highest absolute sample value seen
	std::atomic<size_t> peakPosition_{0}; ///< Buffer position where peak occurred
	std::atomic<bool> peakValid_{false};  ///< True if runningPeak_ is valid (not overwritten)

	// Cached settings (read from runtime feature settings)
	uint8_t durationSeconds_ = 30;                         ///< Buffer duration: 15, 30, or 60 seconds
	uint8_t bytesPerSample_ = 2;                           ///< Bytes per sample: 2 (16-bit) or 3 (24-bit)
	uint8_t numChannels_ = 2;                              ///< Number of channels: 1 (mono) or 2 (stereo)
	AudioInputChannel source_ = AudioInputChannel::STEREO; ///< Audio source

	// Bar-sync pending save state
	std::atomic<bool> pendingSave_{false};   ///< True when waiting for downbeat to save
	std::atomic<int64_t> saveTargetTick_{0}; ///< Tick position of target downbeat
	std::atomic<float> savedBPM_{0.0f};      ///< BPM captured when save triggered
	String* pendingFilePath_{nullptr};       ///< File path output pointer for pending save

	/// Execute the pending save (called when downbeat reached)
	void executePendingSave();

	/// Calculate number of samples for bar-synced save based on current tempo
	size_t calculateBarSyncedSamples() const;

	/// Save to file with BPM tag and trimmed to specified sample count
	/// @param filePath Output parameter to receive the saved file path
	/// @param maxSamples Maximum samples to save (trims buffer to this)
	/// @param bpm BPM value to include in filename
	/// @return Error::NONE on success, or an error code on failure
	Error saveToFileWithBPM(String* filePath, size_t maxSamples, float bpm);
};

/// Global instance of the retrospective buffer
extern RetrospectiveBuffer retrospectiveBuffer;
