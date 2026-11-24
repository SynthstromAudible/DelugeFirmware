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

#include "model/sample/sample_read_lease.h"

#include "harness/streaming_underrun.h"
#include "model/sample/sample.h"
#include "model/sample/sample_playback_guide.h"
#include "storage/cluster/cluster.h"

#include <utility>

SampleReadLease::~SampleReadLease() {
	let_go();
	// The cursor last: it holds its own leases on its current and prefetched chunks
	if (source_ != nullptr) {
		deluge_sample_source_close(source_);
	}
}

SampleReadLease::SampleReadLease(SampleReadLease&& other) noexcept
    : sample_(other.sample_), guide_(other.guide_), priority_(other.priority_), obey_markers_(other.obey_markers_),
      source_(std::exchange(other.source_, nullptr)), source_backing_(std::exchange(other.source_backing_, nullptr)),
      region_(std::exchange(other.region_, DelugeSampleRegion{})) {
}

SampleReadLease& SampleReadLease::operator=(SampleReadLease&& other) noexcept {
	if (this == &other) {
		return *this;
	}
	let_go();
	if (source_ != nullptr) {
		deluge_sample_source_close(source_);
	}
	sample_ = other.sample_;
	guide_ = other.guide_;
	priority_ = other.priority_;
	obey_markers_ = other.obey_markers_;
	source_ = std::exchange(other.source_, nullptr);
	source_backing_ = std::exchange(other.source_backing_, nullptr);
	region_ = std::exchange(other.region_, DelugeSampleRegion{});
	return *this;
}

void SampleReadLease::bind(Sample& sample, SamplePlaybackGuide& guide, int32_t priority_rating, bool obey_markers) {
	sample_ = &sample;
	guide_ = &guide;
	priority_ = priority_rating;
	obey_markers_ = obey_markers;
}

int32_t SampleReadLease::frame_bytes() const {
	return sample_->numChannels * sample_->byteDepth;
}

int32_t SampleReadLease::byte_of(int32_t frame) const {
	return static_cast<int32_t>(sample_->audioDataStartPosBytes) + (frame * frame_bytes());
}

int32_t SampleReadLease::channels() const {
	return sample_->numChannels;
}

int32_t SampleReadLease::byte_depth() const {
	return sample_->byteDepth;
}

int32_t SampleReadLease::length_frames() const {
	return static_cast<int32_t>(sample_->audioDataLengthBytes) / frame_bytes();
}

std::optional<SampleReadLease::FrameRun> SampleReadLease::run(int32_t frame, Direction direction) const {
	if (!holds_chunk()) {
		return std::nullopt;
	}
	const int32_t fb = frame_bytes();
	const int32_t offset = byte_of(frame) - (chunk_index() * static_cast<int32_t>(Cluster::size));
	const int32_t origin = offset + sample_->byteDepth - 4;
	// To the chunk's memory's edge, its slack included: a window check has bounded the frames read
	const int32_t frames = direction == Direction::FORWARD
	                           ? ((static_cast<int32_t>(Cluster::size) + 7 - (offset + fb)) / fb) + 1
	                           : ((origin + 4) / fb) + 1;
	return FrameRun{.first = static_cast<const std::byte*>(region_.payload_base) + origin,
	                .frames = frames,
	                .stride = fb * static_cast<int32_t>(direction),
	                .channels = static_cast<int8_t>(sample_->numChannels),
	                .byte_depth = static_cast<int8_t>(sample_->byteDepth)};
}

void SampleReadLease::ensure_source() {
	DelugeStreamBacking* backing = deluge::audio::stream::toAbi(sample_->stream());
	if (source_ != nullptr && source_backing_ != backing) {
		deluge_sample_source_close(source_);
		source_ = nullptr;
		source_backing_ = nullptr;
	}
	if (source_ == nullptr) {
		source_ = deluge_sample_source_open(
		    backing, DelugeSampleGeometry{.audio_data_start_bytes = sample_->audioDataStartPosBytes,
		                                  .audio_data_length_bytes = sample_->audioDataLengthBytes,
		                                  .cluster_size_bytes = static_cast<uint32_t>(Cluster::size),
		                                  .byte_depth = sample_->byteDepth,
		                                  .num_channels = sample_->numChannels,
		                                  .raw_data_format = static_cast<uint8_t>(sample_->rawDataFormat)});
		source_backing_ = backing;
	}
}

SampleReadLease::Residency SampleReadLease::seat_chunk(int32_t index, Direction direction) {
	let_go();
	ensure_source();
	if (source_ == nullptr) {
		return Residency::UNAVAILABLE; // the source pool is exhausted: no cursor to load through
	}
	DelugeSampleRegion region;
	const RegionOutcome outcome = regionOutcomeFrom(
	    deluge_sample_region_acquire_ex(source_, static_cast<uint32_t>(index), static_cast<int8_t>(direction),
	                                    static_cast<uint32_t>(priority_), &region));
	if (outcome != RegionOutcome::Ready) {
		return outcome == RegionOutcome::Loading ? Residency::LOADING : Residency::UNAVAILABLE;
	}
	// This lease's own pin, beside the cursor's
	deluge_sample_region_retain(region.lease);
	region_ = region;
	return Residency::READY;
}

SampleReadLease::Residency SampleReadLease::move_to(int32_t frame, Direction direction) {
	const int32_t index = byte_of(frame) >> Cluster::size_magnitude;
	const auto d = static_cast<int32_t>(direction);
	if (!holds_chunk() || index != chunk_index() + d) {
		return seat_chunk(index, direction);
	}
	// The boundary crossing advances through the cursor, its prefetch promoted to current; a lease with no cursor (a
	// level copy) cannot (R10's quirk)
	deluge_sample_region_release(region_.lease);
	DelugeSampleRegion region;
	if (!deluge_sample_region_acquire(source_, static_cast<uint32_t>(index), static_cast<int8_t>(direction),
	                                  static_cast<uint32_t>(priority_), &region)) {
		region_ = {};
#if DELUGE_UNDERRUN_COUNTERS
		deluge::harness::noteUnderrunUnassign();
#endif
		return Residency::UNAVAILABLE;
	}
	deluge_sample_region_retain(region.lease);
	region_ = region;
	return Residency::READY;
}

void SampleReadLease::let_go() {
	if (region_.lease != 0) {
		deluge_sample_region_release(region_.lease);
	}
	region_ = {};
}

std::optional<SampleReadLease::ChunkSpan> SampleReadLease::span(Direction direction) const {
	if (!holds_chunk()) {
		return std::nullopt;
	}
	const int32_t index = chunk_index();
	const auto cs = static_cast<int32_t>(Cluster::size);
	const auto ds = static_cast<int32_t>(sample_->audioDataStartPosBytes);
	const int32_t fb = frame_bytes();
	int32_t end_byte = 0;
	const int32_t final_index = guide_->getFinalClusterIndex(sample_, obey_markers_, &end_byte);
	ChunkSpan span{};
	if (index == final_index) {
		span.boundary = (end_byte - ds) / fb;
		span.final = true;
	}
	else if (direction == Direction::FORWARD) {
		auto e = static_cast<int32_t>(static_cast<uint32_t>(((index + 1) * cs) - ds) % static_cast<uint8_t>(fb));
		e = e == 0 ? fb : e;
		span.boundary = (((index + 1) * cs) - ds + fb - e) / fb;
	}
	else {
		auto e = static_cast<int32_t>(static_cast<uint32_t>((index * cs) - ds) % static_cast<uint8_t>(fb));
		e = e == 0 ? fb : e;
		span.boundary = ((index * cs) - ds - e) / fb;
	}
	if (direction == Direction::FORWARD) {
		span.floor = index == sample_->getFirstClusterIndexWithAudioData() ? 0 : (((index * cs) - ds) + fb - 1) / fb;
	}
	else {
		const auto stop = static_cast<int32_t>(sample_->audioDataStartPosBytes + sample_->audioDataLengthBytes);
		span.floor = index == (stop >> Cluster::size_magnitude)
		                 ? static_cast<int32_t>(sample_->audioDataLengthBytes - 1) / fb
		                 : ((index * cs) + cs - 1 - ds) / fb;
	}
	return span;
}

SampleReadLease SampleReadLease::copy_without_cursor() const {
	SampleReadLease copy;
	copy.sample_ = sample_;
	copy.guide_ = guide_;
	copy.priority_ = priority_;
	copy.obey_markers_ = obey_markers_;
	copy.region_ = region_;
	// Its own independent pin on the shared chunk: its only one, with no cursor
	if (copy.region_.lease != 0) {
		deluge_sample_region_retain(copy.region_.lease);
	}
	return copy;
}

SampleReadLease SampleReadLease::copy_with_cursor() const {
	SampleReadLease copy = copy_without_cursor();
	if (copy.sample_ != nullptr) {
		copy.ensure_source();
	}
	return copy;
}

bool SampleReadLease::reacquire(Direction direction) {
	if (!holds_chunk()) {
		return true;
	}
	int32_t index = chunk_index();
	const int32_t final_index = guide_->getFinalClusterIndex(sample_, obey_markers_);
	if ((index - final_index) * static_cast<int32_t>(direction) > 0) {
		// A cache's read left the head past the final chunk: the final chunk is taken through the cursor before
		// this lease's chunk is let go, at priority 1
		DelugeSampleRegion final_region;
		if (!deluge_sample_region_acquire(source_, static_cast<uint32_t>(final_index), static_cast<int8_t>(direction),
		                                  1u, &final_region)) {
			return false;
		}
		deluge_sample_region_release(region_.lease);
		region_ = final_region;
		deluge_sample_region_retain(region_.lease);
		index = final_index;
	}
	return seat_chunk(index, direction) == Residency::READY;
}

void SampleReadLease::resync_to(int32_t chunk_index, Direction direction) {
	let_go();
	ensure_source();
	// Skipped while the port holds a reservation in flight for this chunk; once it lands this reports ready
	if (deluge_sample_region_state(source_, static_cast<uint32_t>(chunk_index)) == DELUGE_REGION_LOADING) {
		return;
	}
	// The most urgent priority: this feeds the render in progress
	DelugeSampleRegion region;
	if (deluge_sample_region_acquire_ex(source_, static_cast<uint32_t>(chunk_index), static_cast<int8_t>(direction), 0u,
	                                    &region)
	    == DELUGE_REGION_READY) {
		deluge_sample_region_retain(region.lease);
		region_ = region;
	}
}

DelugeRegionState SampleReadLease::probe_chunk(int32_t index, Direction direction, uint32_t priority) {
	ensure_source();
	DelugeSampleRegion region{};
	return deluge_sample_region_acquire_ex(source_, static_cast<uint32_t>(index), static_cast<int8_t>(direction),
	                                       priority, &region);
}

DelugeRegionState SampleReadLease::chunk_state(int32_t index) const {
	return deluge_sample_region_state(source_, static_cast<uint32_t>(index));
}

void SampleReadLease::unseat(bool /*wont_be_used_again*/) {
	let_go();
}

void SampleReadLease::prefetch(int32_t frame, int32_t bytes, Direction direction) const {
	if (!holds_chunk()) {
		return;
	}
	const char* at = static_cast<const char*>(region_.payload_base) + byte_of(frame)
	                 - (chunk_index() * static_cast<int32_t>(Cluster::size)) - 4 + sample_->byteDepth;
	for (int32_t done = 0; done < bytes; done += 32) {
		__builtin_prefetch(at);
		at += 32 * static_cast<int32_t>(direction);
	}
}
