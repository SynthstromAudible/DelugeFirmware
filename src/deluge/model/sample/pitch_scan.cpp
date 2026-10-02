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

#include "model/sample/pitch_scan.h"

#include "definitions_cxx.hpp"
#include "libdeluge/clock.h" // deluge_clock_now
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "processing/engines/audio_engine.h"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h" // hanningWindow
#include "util/waves.h"                     // interpolateTableSigned
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace deluge::sample {

void PitchScan::arm(Sample& sample, Limits limits) {
	cancel();
	sample_ = &sample;
	limits_ = limits;
	result_ = 0;
	gaveUp_ = false;
	bytesConsumed_ = 0;
	bytesAtLastProgress_ = 0;
	span_.reset();

	buffer_ = static_cast<int32_t*>(deluge::memory::alloc_fast(Sample::pitchBufferBytes()));
	if (buffer_ == nullptr) {
		finish(0);
		return;
	}

	// If high sample rate, downsample by default
	defaultLengthDoublings_ = (sample.sampleRate >= 88200) ? 1 : 0;
	lengthDoublings_ = defaultLengthDoublings_;
	// If enforced max freq too low, increase doublings
	float maxFreqHere = limits.maxFreqHz;
	while (maxFreqHere < kMinAccurateFrequency) {
		lengthDoublings_++;
		if (lengthDoublings_ >= 10) {
			finish(0); // finish() releases buffer_; returning directly here would leak it.
			return;
		}
		maxFreqHere *= 2;
	}

	secondPass_ = false;
	startValueThreshold_ = 1 << (31 - 4);
	if (!sample.beginningOffsetForPitchDetection) {
		sample.beginningOffsetForPitchDetection = static_cast<int32_t>(sample.audioDataStartPosBytes);
	}
	sourceId_ = source_id_for(sample);
	// One sample-period across all channels, matching the reader's own frame geometry.
	frameSizeBytes_ = static_cast<uint32_t>(sample.numChannels) * sample.byteDepth;
	startPass();
}

void PitchScan::startPass() {
	Sample& s = *sample_;
	currentOffset_ = s.beginningOffsetForPitchDetection;
	writeIndex_ = 0;
	biggestValueFound_ = 0;
	count_ = 0;
	thisValue_ = 0;
	valueIndex_ = 0;
	// A stereo sample blends left and right through the same averaging.
	lengthDoublingsNow_ = lengthDoublings_ + ((s.numChannels == 2) ? 1 : 0);

	const uint32_t audioByteOffset = static_cast<uint32_t>(currentOffset_) - s.audioDataStartPosBytes;
	reader_.reset();
	reader_.emplace(sourceId_, audioByteOffset / frameSizeBytes_, 1, DELUGE_READ_SCAN);
	window_ = {};
	bufPos_ = audioByteOffset % frameSizeBytes_;
	needWindow_ = true;
	phase_ = Phase::Read;
}

PitchScan::ReadOutcome PitchScan::stepRead() {
	Sample& s = *sample_;
	while (true) {
		if (needWindow_) {
			window_ = reader_->window();
			if (!reader_->ok()) {
				return ReadOutcome::Missed; // enqueued and not resident yet, or a failed read: the bound decides
			}
			needWindow_ = false;
			if (window_.frame_count == 0) {
				finish(0); // the audio ended before the window filled ("failed to load")
				return ReadOutcome::Moved;
			}
		}
		// Pull the next window once this one's exhausted - the reader stitches cluster boundaries internally.
		if (bufPos_ >= window_.frame_count * frameSizeBytes_) {
			reader_->advance(window_.frame_count);
			bufPos_ = 0;
			needWindow_ = true;
			continue;
		}

		if (!(count_ & 255)) {
			AudioEngine::routineWithClusterLoading();
		}
		count_++;

		// Left-justify this byteDepth-byte little-endian sample into the top of a 32-bit word.
		int32_t individualSampleValue = 0;
		std::memcpy(reinterpret_cast<std::byte*>(&individualSampleValue) + (4 - s.byteDepth),
		            static_cast<const std::byte*>(window_.frames) + bufPos_, s.byteDepth);
		individualSampleValue &= s.bitMask;
		thisValue_ += (individualSampleValue >> lengthDoublingsNow_);

		currentOffset_ += s.byteDepth;
		bufPos_ += s.byteDepth;
		bytesConsumed_ += s.byteDepth;

		// If reached end of file
		if (currentOffset_ >= s.audioDataLengthBytes + s.audioDataStartPosBytes) {
			finishReading();
			return ReadOutcome::Moved;
		}

		// Rudimentary audio start-detection: percussive sounds' tone is only found with the window where they hit.
		if (!s.beginningOffsetForPitchDetectionFound) {
			const int32_t absoluteValue = (individualSampleValue < 0) ? -individualSampleValue : individualSampleValue;
			if (absoluteValue > biggestValueFound_) {
				biggestValueFound_ = absoluteValue;
			}
			if (absoluteValue < startValueThreshold_) {
				thisValue_ = 0;
				valueIndex_ = 0;
				continue;
			}
			s.beginningOffsetForPitchDetectionFound = true;
			// Start grabbing audio from a quarter of a second after here
			s.beginningOffsetForPitchDetection = currentOffset_ + (s.sampleRate >> 2) * s.numChannels * s.byteDepth;
			// If our grabbed window would end beyond the end of the audio file, shift it left
			s.beginningOffsetForPitchDetection =
			    std::min(s.beginningOffsetForPitchDetection,
			             (int32_t)(s.audioDataStartPosBytes + s.audioDataLengthBytes
			                       - (kPitchDetectWindowSize << lengthDoublings_) * s.numChannels * s.byteDepth));
			// And not further left than where we are right now
			s.beginningOffsetForPitchDetection = std::max(s.beginningOffsetForPitchDetection, currentOffset_);
		}
		if (currentOffset_ < s.beginningOffsetForPitchDetection) {
			thisValue_ = 0;
			valueIndex_ = 0;
			continue;
		}

		if (++valueIndex_ < (1 << lengthDoublingsNow_)) {
			continue;
		}

		// Do hanning window
		const int32_t hanningValue =
		    interpolateTableSigned(writeIndex_, kPitchDetectWindowSizeMagnitude, hanningWindow, 8);
		buffer_[writeIndex_] = multiply_32x32_rshift32_rounded(thisValue_, hanningValue) >> 12;
		writeIndex_++;
		thisValue_ = 0;
		valueIndex_ = 0;
		if (writeIndex_ >= kPitchDetectWindowSize) {
			finishReading();
			return ReadOutcome::Moved;
		}
	}
}

void PitchScan::finishReading() {
	if (!sample_->beginningOffsetForPitchDetectionFound) {
		// No sound found. Try once more with a threshold derived from the sound's actual volume.
		if (!secondPass_ && biggestValueFound_ >= (1 << (31 - 9))) {
			secondPass_ = true;
			startValueThreshold_ = biggestValueFound_ >> 4;
			startPass();
			return;
		}
		finish(0);
		return;
	}
	while (writeIndex_ < kPitchDetectWindowSize) {
		buffer_[writeIndex_++] = 0;
	}
	reader_.reset();
	phase_ = Phase::Analyse;
}

PitchScan::Progress PitchScan::tick() {
	while (true) {
		switch (phase_) {
		case Phase::Idle:
		case Phase::Done:
			return Progress::Done;
		case Phase::Read:
			if (stepRead() == ReadOutcome::Missed) {
				if (bytesConsumed_ > bytesAtLastProgress_) {
					bytesAtLastProgress_ = bytesConsumed_; // frames arrived since the last miss: the bound starts again
					span_.reset();
				}
				span_.note(deluge_clock_now());
				if (span_.elapsedUs > deluge::storage::kMaxUnproductiveUs) {
					gaveUp_ = true;
					finish(0);
					return Progress::Done;
				}
				return Progress::Pending;
			}
			break;
		case Phase::Analyse: {
			const Sample::PitchVerdict verdict =
			    sample_->analysePitchWindow(buffer_, lengthDoublings_, defaultLengthDoublings_, limits_.minFreqHz,
			                                limits_.maxFreqHz, limits_.doPrimeTest);
			if (verdict.kind == Sample::PitchVerdict::Kind::LongerLength) {
				lengthDoublings_++; // one doubling at a time: this can correct an incorrect reading
				startPass();
			}
			else {
				finish(verdict.kind == Sample::PitchVerdict::Kind::Found ? verdict.hz : 0);
			}
			break;
		}
		}
	}
}

void PitchScan::release() {
	reader_.reset();
	if (buffer_ != nullptr) {
		delugeDealloc(buffer_);
		buffer_ = nullptr;
	}
}

void PitchScan::finish(float hz) {
	release();
	result_ = hz;
	phase_ = Phase::Done;
}

void PitchScan::cancel() {
	release();
	phase_ = Phase::Idle;
}

} // namespace deluge::sample
