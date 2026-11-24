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

// The read-head specs' source: a read_source over a recorded ReadSourceRecord, its frames read from the chunk images
// the legacy's call touched, its chunk geometry (R6's boundary and floor) from the record's layout.

#include "dsp/fixed/sample_read/read_head.h"

#include <cstdint>
#include <optional>

#include "time_stretcher_records.h"

namespace read_head_chunks {

namespace r = time_stretcher_records;
using deluge::dsp::fixed::sample_read::ChunkSpan;
using deluge::dsp::fixed::sample_read::Direction;
using deluge::dsp::fixed::sample_read::FrameRun;
using deluge::dsp::fixed::sample_read::ReadHead;
using deluge::dsp::fixed::sample_read::Residency;

class RecordChunks {
public:
	/// @param held The chunk the head holds when the record begins, or none.
	RecordChunks(const r::ReadSourceRecord& record, std::optional<int32_t> held) : record_(&record), held_(held) {}

	[[nodiscard]] int32_t channels() const { return record_->channels; }
	[[nodiscard]] int32_t byte_depth() const { return record_->byte_depth; }
	[[nodiscard]] int32_t frame_bytes() const { return record_->channels * record_->byte_depth; }
	[[nodiscard]] int32_t length_frames() const { return record_->data_length / frame_bytes(); }
	[[nodiscard]] int32_t byte_of(int32_t frame) const { return record_->data_start + (frame * frame_bytes()); }
	[[nodiscard]] std::optional<int32_t> held() const { return held_; }
	[[nodiscard]] int32_t misses() const { return misses_; }

	/// The frames from @p frame to its image's edge in @p direction, in the chunk held.
	[[nodiscard]] std::optional<FrameRun> run(int32_t frame, Direction direction) const {
		if (!held_) {
			return std::nullopt;
		}
		const int32_t fb = frame_bytes();
		const int32_t offset = byte_of(frame) - (*held_ * record_->chunk_size);
		const int32_t origin = offset + record_->byte_depth - 4;
		for (uint32_t i = 0; i < record_->chunks.count; i++) {
			const r::ChunkImage& image = r::kChunkImages[record_->chunks.offset + i];
			const int32_t end = image.first_offset + static_cast<int32_t>(image.bytes.count);
			if (image.index != *held_ || image.residency != 0 || origin < image.first_offset || offset + fb > end) {
				continue;
			}
			const int32_t frames = direction == Direction::FORWARD ? ((end - (offset + fb)) / fb) + 1
			                                                       : ((origin - image.first_offset) / fb) + 1;
			return FrameRun{.first = reinterpret_cast<const std::byte*>(&r::kReadBytes[image.bytes.offset])
			                         + (origin - image.first_offset),
			                .frames = frames,
			                .stride = fb * static_cast<int32_t>(direction),
			                .channels = record_->channels,
			                .byte_depth = record_->byte_depth};
		}
		misses_++;
		return std::nullopt;
	}

	/// The chunk holding @p frame, as the record's images answer it; past the first chunk with no region-port cursor, a
	/// move is unavailable (R10's quirk).
	Residency move_to(int32_t frame, Direction /*direction*/) {
		const int32_t index = byte_of(frame) / record_->chunk_size;
		if (held_ && *held_ != index && !record_->has_cursor) {
			held_.reset();
			return Residency::UNAVAILABLE;
		}
		for (uint32_t i = 0; i < record_->chunks.count; i++) {
			const r::ChunkImage& image = r::kChunkImages[record_->chunks.offset + i];
			if (image.index == index) {
				const auto residency = static_cast<Residency>(image.residency);
				if (residency == Residency::READY) {
					held_ = index;
				}
				else {
					held_.reset();
				}
				return residency;
			}
		}
		misses_++;
		held_.reset();
		return Residency::UNAVAILABLE;
	}

	void let_go() { held_.reset(); }

	/// R6's boundary and floor for the chunk held, in frames.
	[[nodiscard]] std::optional<ChunkSpan> span(Direction direction) const {
		if (!held_) {
			return std::nullopt;
		}
		return chunk_span(*record_, *held_, direction);
	}

	/// R6's boundary and floor of chunk @p index of a source laid out as @p geometry, in frames.
	[[nodiscard]] static ChunkSpan chunk_span(const r::ReadSourceRecord& geometry, int32_t index, Direction direction) {
		const int32_t cs = geometry.chunk_size;
		const int32_t ds = geometry.data_start;
		const int32_t fb = geometry.channels * geometry.byte_depth;
		const bool forward = direction == Direction::FORWARD;
		const int32_t final_index = forward ? (geometry.end_byte - 1) / cs : (geometry.end_byte + fb) / cs;
		ChunkSpan span{};
		if (index == final_index) {
			span.boundary = (geometry.end_byte - ds) / fb;
			span.final = true;
		}
		else if (forward) {
			auto e = static_cast<int32_t>(static_cast<uint32_t>(((index + 1) * cs) - ds) % static_cast<uint8_t>(fb));
			e = e == 0 ? fb : e;
			span.boundary = (((index + 1) * cs) - ds + fb - e) / fb;
		}
		else {
			auto e = static_cast<int32_t>(static_cast<uint32_t>((index * cs) - ds) % static_cast<uint8_t>(fb));
			e = e == 0 ? fb : e;
			span.boundary = ((index * cs) - ds - e) / fb;
		}
		if (forward) {
			span.floor = index == ds / cs ? 0 : (((index * cs) - ds) + fb - 1) / fb;
		}
		else {
			const int32_t stop = ds + geometry.data_length;
			span.floor = index == stop / cs ? (geometry.data_length - 1) / fb : ((index * cs) + cs - 1 - ds) / fb;
		}
		return span;
	}

private:
	const r::ReadSourceRecord* record_;
	std::optional<int32_t> held_;
	mutable int32_t misses_ = 0;
};

/// A recorded head as the read head's state: its cursor a frame from the data's start, its window loaded.
inline ReadHead::State state_of(const r::HeadRecord& head, const r::ReadSourceRecord& source) {
	ReadHead::State state;
	if (head.ended) {
		state.cursor = ReadHead::Ended{head.zero_taps};
	}
	else {
		const int32_t fb = source.channels * source.byte_depth;
		state.cursor =
		    ReadHead::Reading{((head.chunk_index * source.chunk_size) + head.chunk_offset - source.data_start) / fb};
	}
	state.phase = deluge::dsp::UQ<24>::from_raw(head.phase);
	state.width_last = head.width_last;
	auto feed = state.window.fill_tail(0);
	for (uint32_t i = 0; i < 16; i++) {
		feed.put(r::kTaps[head.taps.offset + i], r::kTaps[head.taps.offset + 16 + i]);
	}
	return state;
}

/// Whether @p state is the recorded head @p head: its cursor, phase, width and window.
inline bool same(const ReadHead::State& state, const r::HeadRecord& head, const r::ReadSourceRecord& source) {
	const ReadHead::State want = state_of(head, source);
	if (state.cursor.index() != want.cursor.index()) {
		return false;
	}
	if (const auto* reading = std::get_if<ReadHead::Reading>(&state.cursor)) {
		if (reading->frame != std::get<ReadHead::Reading>(want.cursor).frame) {
			return false;
		}
	}
	return state.phase.raw() == want.phase.raw() && state.width_last == want.width_last
	       && state.window.window_l() == want.window.window_l() && state.window.window_r() == want.window.window_r();
}

static_assert(deluge::dsp::fixed::sample_read::read_source<RecordChunks>);

} // namespace read_head_chunks
