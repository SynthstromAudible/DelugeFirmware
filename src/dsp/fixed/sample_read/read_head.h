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

#include "dsp/core/fixed_point.h"
#include "dsp/fixed/interpolation/interpolator.h"
#include "dsp/fixed/sample_read/frame_source.h"

#include <concepts>
#include <cstdint>
#include <optional>
#include <variant>

namespace deluge::dsp::fixed::sample_read {

/// @file
/// @brief A sample's read head (section 2.8 of docs/dev/time_stretcher_design.md; R1 to R11 and A0 of
/// docs/dev/time_stretcher_contracts.md): a cursor a frame from the data's start, its phase and its interpolation
/// window, reading frames through a source that holds one chunk at a time. It holds no pointer and no lease: the
/// source does.

/// @brief Whether a chunk a source moved to is in memory: ready, still loading, or not to be had.
enum class Residency : uint8_t { READY, LOADING, UNAVAILABLE };

/// @brief The chunk a source holds, in frames, in one direction: the frame the walk next acts at (R6's boundary), the
/// last frame a look back may read (its floor), and whether reaching the boundary ends or loops the waveform.
struct ChunkSpan {
	int32_t boundary;
	int32_t floor;
	bool final;
};

/// @brief A source of frames that holds one chunk at a time, which a head reads through.
template <class S>
concept read_source = frame_source<S> && requires(S& s, const S& cs, int32_t frame, Direction d) {
	{ s.move_to(frame, d) } -> std::same_as<Residency>;       // R6, R10: the lease moves to the chunk holding frame
	{ s.let_go() } -> std::same_as<void>;                     // the chunk goes
	{ cs.span(d) } -> std::same_as<std::optional<ChunkSpan>>; // nullopt: no chunk held
	{ cs.byte_depth() } -> std::same_as<int32_t>;
};

/// @brief What A0 reads of a sample: where its data starts, where playback ends, its layout.
struct ReadFacts {
	int32_t data_start_byte;
	int32_t end_byte; ///< The guide's end byte.
	int8_t channels;
	int8_t byte_depth;
};

/// @brief A read head's state and the operations on it, over any read_source.
struct ReadHead {
	/// @brief Reading: the next frame the window takes.
	struct Reading {
		int32_t frame;
	};
	/// @brief Past the end, or holding no chunk: the zero taps fed since.
	struct Ended {
		int32_t zero_taps;
	};
	struct State {
		std::variant<Reading, Ended> cursor{Ended{0}};
		UQ<24> phase{};
		int8_t width_last = 0; ///< At the last resampled check; 0 after a native one.
		Interpolator window{};
	};
	/// A seat that is not ready leaves the head ended with a count past every width, as the legacy's stale cursor did.
	static constexpr int32_t kStaleZeroTaps = INT32_MAX;

	/// @brief Seats @p state at @p frame (R10): outside the data unavailable, the state unchanged; else the source
	/// moves to the frame's chunk, answering its residency, and a ready one places the cursor there with @p phase and
	/// width 0 (H1).
	template <read_source S>
	[[nodiscard]] static Residency seat(State& state, S& source, int32_t frame, UQ<24> phase, Direction direction) {
		if (frame < 0 || frame >= source.length_frames()) {
			return Residency::UNAVAILABLE;
		}
		const Residency residency = source.move_to(frame, direction);
		if (residency != Residency::READY) {
			state.cursor = Ended{kStaleZeroTaps};
			return residency;
		}
		state.cursor = Reading{frame};
		state.phase = phase;
		state.width_last = 0;
		return residency;
	}

	/// @brief A level copy (R10): the cursor, phase, width and window. Its source takes its own lease on the same
	/// chunk, and no region-port cursor, so it can never move to the next chunk (R10's quirk).
	[[nodiscard]] static State level_copy(const State& state) { return state; }

	/// @brief The head's reference byte (A0): its cursor's byte, less half its last window against the play direction
	/// when @p compensate; past its end the end byte moved on by its zero taps.
	[[nodiscard]] static int32_t play_byte(const State& state, const ReadFacts& facts, Direction direction,
	                                       bool compensate) {
		const auto d = static_cast<int32_t>(direction);
		if (const auto* ended = std::get_if<Ended>(&state.cursor)) {
			// A stale count runs past every byte; the legacy's sum wrapped as a word
			return static_cast<int32_t>(static_cast<uint32_t>(facts.end_byte)
			                            + static_cast<uint32_t>(ended->zero_taps * d));
		}
		const int32_t frame_bytes = facts.channels * facts.byte_depth;
		int32_t byte = facts.data_start_byte + (std::get<Reading>(state.cursor).frame * frame_bytes);
		if (compensate && state.width_last != 0) {
			byte -= (state.width_last >> 1) * frame_bytes * d;
		}
		return byte;
	}
};

static_assert(std::is_trivially_copyable_v<ReadHead::State>);

} // namespace deluge::dsp::fixed::sample_read
