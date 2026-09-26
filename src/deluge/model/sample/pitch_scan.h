/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#pragma once

#include "definitions_cxx.hpp"                  // kPitchDetectWindowSizeMagnitude
#include "libdeluge/sample_reader.h"            // DelugeFrameWindow
#include "model/sample/recorder_write_policy.h" // UnproductiveSpan, kMaxUnproductiveUs
#include "model/sample/sample_reader_bridge.h"  // SampleFrameReader
#include <cstdint>
#include <optional>

class Sample;

namespace deluge::sample {

/// Below this (Hz) a detection runs again on a longer, downsampled window.
constexpr int32_t kMinAccurateFrequency = (1638400 >> (kPitchDetectWindowSizeMagnitude));
/// How many times past the default a window may be lengthened.
constexpr int32_t kMaxLengthDoublings = (16 - kPitchDetectWindowSizeMagnitude);

/// @brief One pitch detection of a Sample, a tick at a time, never waiting for the card.
///
/// The read fills an FFT window through a SampleFrameReader. Off the worker fiber a window not yet resident is enqueued
/// for the streaming loader and reported missing; the scan then saves its whole cursor and resumes at that frame on the
/// next tick. A miss and a failed read look alike, so the bound tells them apart: kMaxUnproductiveUs with no frame
/// consumed ends the scan as a failed detection (0 Hz). The analysis is Sample::analysePitchWindow().
class PitchScan {
public:
	enum class Progress : uint8_t { Pending, Done };
	struct Limits {
		float minFreqHz = 20;    ///< Lowest fundamental searched, Hz.
		float maxFreqHz = 10000; ///< Highest fundamental searched, Hz.
		bool doPrimeTest = true;
	};

	PitchScan() = default;
	PitchScan(const PitchScan&) = delete;
	PitchScan& operator=(const PitchScan&) = delete;
	~PitchScan() { cancel(); }

	/// @brief Start a detection of @p sample, dropping any in progress. The sample must outlive the scan or its cancel.
	void arm(Sample& sample, Limits limits);
	/// @brief Advance as far as it can without waiting. Done at once when idle.
	Progress tick();
	/// @brief Drop the detection: close the reader, free the buffer. The result is left as it was.
	void cancel();
	/// @brief Whether a detection is in progress.
	[[nodiscard]] bool active() const { return phase_ == Phase::Read || phase_ == Phase::Analyse; }
	/// @brief The detected fundamental in Hz once Done; 0 = failed.
	[[nodiscard]] float result() const { return result_; }
	/// @brief Whether the last detection ended on the bound.
	[[nodiscard]] bool gaveUp() const { return gaveUp_; }

private:
	enum class Phase : uint8_t { Idle, Read, Analyse, Done };
	enum class ReadOutcome : uint8_t { Missed, Moved };

	void startPass();
	ReadOutcome stepRead();
	void finishReading();
	void finish(float hz);
	void release();

	Phase phase_{Phase::Idle};
	Sample* sample_{nullptr};
	Limits limits_{};
	int32_t* buffer_{nullptr};
	float result_{0}; ///< Hz.
	bool gaveUp_{false};

	int32_t defaultLengthDoublings_{0};
	int32_t lengthDoublings_{0};
	bool secondPass_{false};
	int32_t startValueThreshold_{0};
	uint32_t sourceId_{0};
	uint32_t frameSizeBytes_{0};

	/// @name The read cursor: everything determinePitch kept on its stack between two samples.
	/// @{
	std::optional<SampleFrameReader> reader_{};
	DelugeFrameWindow window_{};
	bool needWindow_{false};
	uint32_t bufPos_{0};       ///< Bytes into window_.
	int32_t currentOffset_{0}; ///< Byte offset in the file.
	int32_t writeIndex_{0};    ///< Next FFT input slot.
	int32_t biggestValueFound_{0};
	int32_t count_{0}; ///< Samples read this pass; paces the audio yields.
	int32_t lengthDoublingsNow_{0};
	int32_t valueIndex_{0}; ///< Samples averaged into thisValue_ so far.
	int32_t thisValue_{0};
	/// @}

	uint64_t bytesConsumed_{0};
	uint64_t bytesAtLastProgress_{0};
	deluge::storage::UnproductiveSpan span_{};
};

} // namespace deluge::sample
