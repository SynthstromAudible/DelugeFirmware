/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include <utility>

#include "definitions_cxx.hpp"
#include "libdeluge/sample_reader.h"
#include "model/sample/sample_playback_guide.h"
#include "storage/audio/audio_file_holder.h"
#include "util/c_string.h"

class Sample;

class SampleHolder : public AudioFileHolder {
public:
	SampleHolder();

	SampleHolder(SampleHolder&& other) noexcept
	    : AudioFileHolder(std::move(other)), startPos(other.startPos), endPos(other.endPos),
	      waveformViewScroll(other.waveformViewScroll), waveformViewZoom(other.waveformViewZoom),
	      neutralPhaseIncrement(other.neutralPhaseIncrement),
	      clustersForStart_(std::exchange(other.clustersForStart_, nullptr)) {}
	SampleHolder& operator=(SampleHolder&& other) noexcept {
		AudioFileHolder::operator=(std::move(other));
		startPos = other.startPos;
		endPos = other.endPos;
		waveformViewScroll = other.waveformViewScroll;
		waveformViewZoom = other.waveformViewZoom;
		neutralPhaseIncrement = other.neutralPhaseIncrement;
		clustersForStart_ = std::exchange(other.clustersForStart_, nullptr);
		return *this;
	}
	~SampleHolder() override;
	void unassignAllClusterReasons(bool beingDestructed = false) override;
	int64_t getEndPos(bool forTimeStretching = false);
	int64_t getDurationInSamples(bool forTimeStretching = false);
	void beenClonedFrom(SampleHolder const* other, bool reversed);
	virtual void claimClusterReasons(bool reversed, int32_t clusterLoadInstruction = CLUSTER_ENQUEUE);
	int32_t getLengthInSamplesAtSystemSampleRate(bool forTimeStretching = false);
	int32_t getLoopLengthAtSystemSampleRate(bool forTimeStretching = false);
	void setAudioFile(AudioFile* newAudioFile, bool reversed = false, bool manuallySelected = false,
	                  int32_t clusterLoadInstruction = CLUSTER_ENQUEUE) override;

	// In samples.
	uint64_t startPos;
	uint64_t endPos; // Don't access this directly. Call getPos(). This variable may be beyond the end of the sample

	int32_t waveformViewScroll{};
	int32_t waveformViewZoom; // 0 means neither of these vars set up yet

	int32_t neutralPhaseIncrement{};

	/// Passive lookahead reservation anchored at this holder's start marker; pins a small window of
	/// cluster residency ahead of/behind the start playback position. `nullptr` when not yet opened
	/// (or after being released). Opened/moved by claimClusterReasonsForMarker(), closed by
	/// unassignAllClusterReasons().
	DelugeSampleReservation* clustersForStart_ = nullptr;

protected:
	/// Byte offset of the start marker within the sample file, as last claimed by claimClusterReasons().
	uint32_t startPlaybackAtByte_ = 0;

	/// @brief Open or re-anchor a passive lookahead reservation at a playback marker.
	///
	/// If @p reservation is null, opens a new one; otherwise re-anchors the existing one to the new
	/// marker position.
	/// @param reservation          The reservation to open/move, by reference so a fresh open can
	///                             write the new handle back into the caller's storage.
	/// @param startPlaybackAtByte  Byte offset of the marker within the sample's audio data.
	/// @param playDirection        +1 forward, -1 reverse.
	/// @param clusterLoadInstruction One of the CLUSTER_* load modes, translated to a DelugeLoadMode.
	void claimClusterReasonsForMarker(DelugeSampleReservation*& reservation, uint32_t startPlaybackAtByte,
	                                  int32_t playDirection, int32_t clusterLoadInstruction);
	virtual void sampleBeenSet(bool reversed, bool manuallySelected) {}
};
