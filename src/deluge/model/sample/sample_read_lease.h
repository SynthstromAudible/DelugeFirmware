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

#include "definitions_cxx.hpp"
#include "deluge_storage_sample_source.h"
#include "dsp/fixed/sample_read/read_head.h"

#include <cstdint>
#include <optional>

class Sample;
class SamplePlaybackGuide;

/// @brief The region port's residency tri-state, as a scoped enum: a plain C enum whose READY is 1 would let `if
/// (!state)` compile at every call site and be silently wrong.
enum class RegionOutcome : uint8_t {
	Ready,       ///< The region is resident and pinned.
	Loading,     ///< Reserved and enqueued, not yet filled. Retry later; not a failure.
	Unavailable, ///< Out of range, or the port could not reserve at all.
};

/// @brief The C-ABI tri-state as a RegionOutcome; anything unrecognised is Unavailable, the conservative choice.
constexpr RegionOutcome regionOutcomeFrom(DelugeRegionState state) {
	switch (state) {
	case DELUGE_REGION_READY:
		return RegionOutcome::Ready;
	case DELUGE_REGION_LOADING:
		return RegionOutcome::Loading;
	default:
		return RegionOutcome::Unavailable;
	}
}

/// @brief One read head's lease on the chunk it reads, and its region-port cursor: the app side of a head, the
/// read_source its ReadHead reads through (section 2.8 of docs/dev/time_stretcher_design.md). The sample and guide it
/// reads are bound for each call; it owns its cursor and its one independent lease on the chunk it holds.
class SampleReadLease {
public:
	using Direction = deluge::dsp::fixed::sample_read::Direction;
	using FrameRun = deluge::dsp::fixed::sample_read::FrameRun;
	using Residency = deluge::dsp::fixed::sample_read::Residency;
	using ChunkSpan = deluge::dsp::fixed::sample_read::ChunkSpan;

	SampleReadLease() = default;
	~SampleReadLease();
	SampleReadLease(SampleReadLease&& other) noexcept;
	SampleReadLease& operator=(SampleReadLease&& other) noexcept;
	SampleReadLease(const SampleReadLease&) = delete;
	SampleReadLease& operator=(const SampleReadLease&) = delete;

	/// @brief The sample, guide and priority this call reads with, and whether the head's owner obeys the markers
	/// (which decides the final chunk, R6).
	void bind(Sample& sample, SamplePlaybackGuide& guide, int32_t priority_rating, bool obey_markers);

	// read_source
	[[nodiscard]] std::optional<FrameRun> run(int32_t frame, Direction direction) const;
	[[nodiscard]] int32_t channels() const;
	[[nodiscard]] int32_t length_frames() const;
	[[nodiscard]] int32_t byte_depth() const;
	/// @brief R6's move to the next chunk when @p frame lies in it, else R10's seat on the chunk holding @p frame
	/// (opening the region-port cursor if there is none).
	[[nodiscard]] Residency move_to(int32_t frame, Direction direction);
	/// @brief Lets go of the chunk held (the voice-sample's unassign); the cursor stays open.
	void let_go();
	[[nodiscard]] std::optional<ChunkSpan> span(Direction direction) const;

	// the app's
	/// @brief A level copy's own lease: an independent lease on the same chunk, and no region-port cursor (R10's
	/// quirk). A stretcher's shadow head takes this one: it is read only until its crossfade ends.
	[[nodiscard]] SampleReadLease copy_without_cursor() const;
	/// @brief A level copy's own lease and its own region-port cursor, opened as a seat opens one, so the copy walks on
	/// past the chunk it shares (decision 13: a unison part added to a sounding voice).
	[[nodiscard]] SampleReadLease copy_with_cursor() const;
	/// @brief The lease again on the chunk it holds, or on the final chunk where a cache's read left it past it: what
	/// a change of markers needs, the final chunk being the markers'. False if it is not ready.
	[[nodiscard]] bool reacquire(Direction direction);
	/// @brief The cache replay's resume: the chunk holding @p frame, through this lease's cursor, at the most urgent
	/// priority, skipped while that chunk is still loading for it.
	void resync_to(int32_t chunk_index, Direction direction);
	/// @brief A late start's residency decision: chunk @p index through this lease's cursor (opened if none), taking
	/// no lease of its own; the port keeps a loading chunk's lease pending and prefetches a ready one's neighbour.
	[[nodiscard]] DelugeRegionState probe_chunk(int32_t index, Direction direction, uint32_t priority);
	/// @brief Chunk @p index's residency as this lease's cursor sees it.
	[[nodiscard]] DelugeRegionState chunk_state(int32_t index) const;
	/// @brief Starts fetching @p bytes of sample data from @p frame on.
	void prefetch(int32_t frame, int32_t bytes, Direction direction) const;
	[[nodiscard]] bool holds_chunk() const { return region_.payload_base != nullptr; }
	[[nodiscard]] int32_t chunk_index() const { return static_cast<int32_t>(region_.region_index); }

private:
	[[nodiscard]] int32_t frame_bytes() const;
	[[nodiscard]] int32_t byte_of(int32_t frame) const;
	void ensure_source();
	[[nodiscard]] Residency seat_chunk(int32_t index, Direction direction);

	Sample* sample_ = nullptr;
	SamplePlaybackGuide* guide_ = nullptr;
	int32_t priority_ = 1;
	bool obey_markers_ = false;
	DelugeSampleSource* source_ = nullptr;          ///< The region-port cursor, opened lazily.
	DelugeStreamBacking* source_backing_ = nullptr; ///< The stream it was opened against.
	DelugeSampleRegion region_{};                   ///< The chunk held, and this lease's own pin on it.
};

static_assert(deluge::dsp::fixed::sample_read::read_source<SampleReadLease>);
