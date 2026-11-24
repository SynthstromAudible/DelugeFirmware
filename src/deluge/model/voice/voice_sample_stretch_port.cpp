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

#include "model/voice/voice_sample_stretch_port.h"

#include "deluge_storage_sample_reader.h"
#include "model/sample/sample.h"
#include "model/sample/sample_playback_guide.h"
#include "model/sample/sample_reader_bridge.h"
#include "model/voice/voice_sample.h"
#include "playback/playback_handler.h"
#include "storage/cluster/cluster.h"

#include <algorithm>

using deluge::dsp::fixed::sample_read::FrameRun;
using deluge::synth::HeadGain;
using deluge::synth::HeadMark;
using deluge::synth::PercChunk;
using deluge::synth::PercFill;
using deluge::synth::Reservation;
using deluge::synth::SeatResult;
using deluge::synth::SourceFacts;

namespace {
int32_t frameBytes(const Sample& sample) {
	return sample.numChannels * sample.byteDepth;
}

/// A byte's frame from the data's start, read unsigned, as the legacy's frame counts are.
int32_t frameOf(const Sample& sample, int32_t byte) {
	return static_cast<int32_t>(static_cast<uint32_t>(byte - sample.audioDataStartPosBytes)
	                            / static_cast<uint8_t>(frameBytes(sample)));
}

DelugeSampleReservation* reservationOf(const Reservation& reservation) {
	return static_cast<DelugeSampleReservation*>(reservation.held);
}
} // namespace

SourceFacts VoiceSampleStretchPort::facts() const {
	const Sample& s = *sample_;
	const int32_t bytes = frameBytes(s);
	const int32_t direction = guide_->playDirection;
	SourceFacts f{.length_frames = static_cast<int32_t>(s.lengthInSamples),
	              .channels = s.numChannels,
	              .byte_depth = s.byteDepth,
	              .data_start_byte = static_cast<int32_t>(s.audioDataStartPosBytes),
	              .start_marker_frame = frameOf(s, static_cast<int32_t>(guide_->startPlaybackAtByte)),
	              .start_byte = guide_->getBytePosToStartPlayback(true),
	              .end_or_loop_byte = guide_->getBytePosToEndOrLoopPlayback(),
	              .loop_ahead = std::nullopt,
	              .sample_rate = static_cast<int32_t>(s.sampleRate)};
	// The stretcher-made loop crossfades only into a pre-margin: waveform before the start marker in the direction
	int32_t waveformStartByte = static_cast<int32_t>(s.audioDataStartPosBytes);
	if (direction != 1) {
		waveformStartByte += static_cast<int32_t>(s.audioDataLengthBytes) - bytes;
	}
	if ((f.start_byte - waveformStartByte) * direction > 0) {
		f.loop_ahead = deluge::dsp::fixed::timestretch::LoopAhead{.loop_end_frame = frameOf(s, f.end_or_loop_byte),
		                                                          .loop_start_frame = frameOf(s, f.start_byte),
		                                                          .cache_in_use = voice_->cache != nullptr};
	}
	return f;
}

std::optional<int64_t> VoiceSampleStretchPort::synced_position() const {
	if (guide_->sequenceSyncLengthTicks && playbackHandler.isEitherClockActive()) {
		return static_cast<int64_t>(guide_->getSyncedNumSamplesIn());
	}
	return std::nullopt;
}

std::optional<FrameRun> VoiceSampleStretchPort::run(int32_t frame, Direction direction) const {
	const Sample& s = *sample_;
	const DelugeFrameWindow window = deluge_sample_peek(deluge::sample::source_id_for(s), static_cast<uint64_t>(frame),
	                                                    static_cast<int8_t>(direction));
	if (window.frames == nullptr) {
		return std::nullopt;
	}
	// The frames to the chunk's edge, a frame straddling it included (read from the chunk's trailing slack), as the
	// legacy's averages counted them
	const int32_t bytes = frameBytes(s);
	const int32_t byte = static_cast<int32_t>(s.audioDataStartPosBytes) + (frame * bytes);
	const int32_t within = byte & (Cluster::size - 1);
	const int32_t left = direction == Direction::REVERSE ? within + bytes : Cluster::size - within + bytes - 1;
	return FrameRun{.first = static_cast<const std::byte*>(window.frames) + s.byteDepth - 4,
	                .frames =
	                    std::max(static_cast<int32_t>(static_cast<uint32_t>(left) / static_cast<uint8_t>(bytes)), 1),
	                .stride = bytes * static_cast<int32_t>(direction),
	                .channels = static_cast<int8_t>(s.numChannels),
	                .byte_depth = static_cast<int8_t>(s.byteDepth)};
}

std::optional<deluge::dsp::fixed::timestretch::PercWindow> VoiceSampleStretchPort::perc_at(int32_t pixel,
                                                                                           Direction direction) {
	return sample_->perc_at(pixel, direction);
}

PercFill VoiceSampleStretchPort::fill_perc(int32_t from, int32_t to, Direction direction, int32_t max_frames) {
	return sample_->fillPercCache(from, to, static_cast<int32_t>(direction), max_frames);
}

void VoiceSampleStretchPort::shadow() {
	// A level copy with its own lease on the shared chunk and no region-port cursor (R10), its width last time 0 (H1)
	voice_->spare_ = SampleLowLevelReader(*voice_, false);
	voice_->spare_.interpolationBufferSizeLastTime = 0;
}

SeatResult VoiceSampleStretchPort::seat_and_swap(int32_t frame, deluge::dsp::UQ<24> phase, LoopType loop) {
	// The newer head becomes the older, stealing its leases; the voice's reader is seated at the start (H1)
	voice_->spare_ = SampleLowLevelReader(*voice_, true);
	const int32_t byte = static_cast<int32_t>(sample_->audioDataStartPosBytes) + (frame * frameBytes(*sample_));
	voice_->unassignAllReasons(false);
	bool started =
	    voice_->setupClustersForPlayFromByte(guide_, sample_, byte, params_.priority_rating) == RegionOutcome::Ready;
	if (started) {
		started =
		    voice_->changeClusterIfNecessary(guide_, sample_, loop == LoopType::LOW_LEVEL, params_.priority_rating);
	}
	if (!started) {
		// The roles swap only on success: the newer steals its leases back. Through the base, so the voice-sample's
		// own members stay
		static_cast<SampleLowLevelReader&>(*voice_) = SampleLowLevelReader(voice_->spare_, true);
		return SeatResult::NOT_READY;
	}
	voice_->interpolationBufferSizeLastTime = 0;
	voice_->oscPos = phase.raw();
	return voice_->hasCurrentRegion() ? SeatResult::STARTED : SeatResult::STARTED_EMPTY;
}

bool VoiceSampleStretchPort::read(Head head, std::span<deluge::dsp::Q<31>> out, int32_t frames, HeadGain gain) {
	// The legacy reader takes the gain and the phase as words
	SampleLowLevelReader& reader = head == Head::NEWER ? static_cast<SampleLowLevelReader&>(*voice_) : voice_->spare_;
	const Sample& s = *sample_;
	return reader.readSamplesForTimeStretching(
	    out.data(), guide_, sample_, frames, s.numChannels, params_.channels_after_condensing, params_.phase_increment,
	    gain.level.raw(), gain.step.raw(), params_.loop == LoopType::LOW_LEVEL, frameBytes(s) * guide_->playDirection,
	    params_.interpolation_width, params_.kernel, params_.priority_rating);
}

HeadMark VoiceSampleStretchPort::mark(Head head) const {
	SampleLowLevelReader& reader = head == Head::NEWER ? static_cast<SampleLowLevelReader&>(*voice_) : voice_->spare_;
	return HeadMark{.play_byte = reader.getPlayByteLowLevel(sample_, guide_, true),
	                .phase = deluge::dsp::UQ<24>::from_raw(reader.oscPos)};
}

bool VoiceSampleStretchPort::cursors_meet() const {
	return voice_->currentPlayPos == voice_->spare_.currentPlayPos;
}

void VoiceSampleStretchPort::keep(Reservation& reservation, int32_t frame, Direction direction) {
	const uint32_t sourceId = deluge::sample::source_id_for(*sample_);
	if (reservation.held == nullptr) {
		reservation.held = deluge_sample_reserve_open(sourceId, static_cast<uint64_t>(static_cast<uint32_t>(frame)),
		                                              static_cast<int8_t>(direction), DELUGE_LOAD_ENQUEUE);
	}
	else {
		deluge_sample_reserve_move(reservationOf(reservation), sourceId,
		                           static_cast<uint64_t>(static_cast<uint32_t>(frame)), static_cast<int8_t>(direction),
		                           DELUGE_LOAD_ENQUEUE);
	}
}

void VoiceSampleStretchPort::let_go(Reservation& reservation) {
	if (reservation.held != nullptr) {
		deluge_sample_reserve_close(reservationOf(reservation));
		reservation.held = nullptr;
	}
}

void VoiceSampleStretchPort::hold(PercChunk chunk) {
	deluge::cluster::add_lease(sample_->percCacheClusters[chunk.reversed ? 1 : 0][chunk.index]);
}

void VoiceSampleStretchPort::drop(PercChunk chunk) {
	ComputedChunk* held = sample_->percCacheClusters[chunk.reversed ? 1 : 0][chunk.index];
	if (held != nullptr) {
		deluge::cluster::remove_reason(*held, "E133");
	}
}
