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

namespace {
using deluge::dsp::fixed::sample_read::Increment;
using deluge::dsp::fixed::sample_read::ReadHead;
using deluge::dsp::fixed::sample_read::Residency;
} // namespace

uint8_t VoiceSampleStretchPort::slot(Head head) const {
	return head == Head::NEWER ? voice_->newer_ : static_cast<uint8_t>(1 - voice_->newer_);
}

SampleReadLease& VoiceSampleStretchPort::bound(uint8_t which) {
	voice_->leases_[which].bind(*sample_, *guide_, params_.priority_rating, voice_->shouldObeyMarkers());
	return voice_->leases_[which];
}

void VoiceSampleStretchPort::shadow() {
	// A level copy with its own lease on the shared chunk and no region-port cursor (R10), its width last time 0 (H1)
	const uint8_t spare = slot(Head::OLDER);
	voice_->heads_[spare] = ReadHead::level_copy(voice_->heads_[voice_->newer_]);
	voice_->heads_[spare].width_last = 0;
	voice_->leases_[spare] = voice_->leases_[voice_->newer_].copy_without_cursor();
}

SeatResult VoiceSampleStretchPort::seat_and_swap(int32_t frame, deluge::dsp::UQ<24> phase, LoopType loop) {
	// The spare is seated at the start (H1); the roles swap only once it has started, so a failed seat leaves the
	// newer untouched (S4). The spare's lease starts afresh, its old chunk let go and its cursor closed, and opens a
	// cursor of its own on the seat, as the legacy's hop handed the newer's cursor to the older and opened another
	const uint8_t spare = slot(Head::OLDER);
	voice_->leases_[spare] = SampleReadLease{};
	SampleReadLease& lease = bound(spare);
	const Direction direction = guide_->playDirection == 1 ? Direction::FORWARD : Direction::REVERSE;
	ReadHead::State& head = voice_->heads_[spare];
	if (ReadHead::seat(head, lease, frame, phase, direction) != Residency::READY) {
		voice_->leases_[spare] = SampleReadLease{};
		return SeatResult::NOT_READY;
	}
	const ReadHead::ReadBounds bounds{.loop_start_frame =
	                                      loop == LoopType::LOW_LEVEL
	                                          ? std::optional<int32_t>(VoiceSample::loopStartFrame(guide_, sample_))
	                                          : std::nullopt,
	                                  .endless_silence = false};
	if (!ReadHead::walk(head, lease, bounds, direction)) {
		voice_->leases_[spare] = SampleReadLease{};
		return SeatResult::NOT_READY;
	}
	voice_->newer_ = spare;
	return lease.holds_chunk() ? SeatResult::STARTED : SeatResult::STARTED_EMPTY;
}

bool VoiceSampleStretchPort::read(Head head, std::span<deluge::dsp::Q<31>> out, int32_t frames, HeadGain gain) {
	const uint8_t which = slot(head);
	const Direction direction = guide_->playDirection == 1 ? Direction::FORWARD : Direction::REVERSE;
	const ReadHead::ReadBounds bounds{.loop_start_frame =
	                                      params_.loop == LoopType::LOW_LEVEL
	                                          ? std::optional<int32_t>(VoiceSample::loopStartFrame(guide_, sample_))
	                                          : std::nullopt,
	                                  .endless_silence = false};
	ReadHead::Output output{.out = out,
	                        .out_channels = static_cast<int8_t>(params_.channels_after_condensing),
	                        .amplitude = gain.level,
	                        .step = gain.step};
	// The legacy reader took the increment as a word
	return ReadHead::read(voice_->heads_[which], bound(which), bounds, output, frames,
	                      Increment::from_raw(static_cast<uint32_t>(params_.phase_increment)),
	                      params_.interpolation_width, params_.kernel, nullptr, direction);
}

HeadMark VoiceSampleStretchPort::mark(Head head) const {
	const ReadHead::State& state = voice_->heads_[slot(head)];
	return HeadMark{.play_byte =
	                    ReadHead::play_byte(state, VoiceSample::readFacts(*sample_, *guide_),
	                                        guide_->playDirection == 1 ? Direction::FORWARD : Direction::REVERSE, true),
	                .phase = state.phase};
}

bool VoiceSampleStretchPort::cursors_meet() const {
	// As the legacy compared its readers' cursors: the same frame, or both ended at the same count
	const auto& newer = voice_->heads_[voice_->newer_].cursor;
	const auto& older = voice_->heads_[1 - voice_->newer_].cursor;
	if (newer.index() != older.index()) {
		return false;
	}
	if (const auto* reading = std::get_if<ReadHead::Reading>(&newer)) {
		return reading->frame == std::get<ReadHead::Reading>(older).frame;
	}
	return std::get<ReadHead::Ended>(newer).zero_taps == std::get<ReadHead::Ended>(older).zero_taps;
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
