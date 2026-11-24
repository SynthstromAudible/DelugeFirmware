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
		const Residency residency = place(state, source, frame, direction);
		if (residency == Residency::READY) {
			state.phase = phase;
			state.width_last = 0;
		}
		return residency;
	}

	/// @brief How far a head's reads may go: where a low-level loop restarts (R6), and whether past its end it reads
	/// silence for ever (a cache being written fills to its end).
	struct ReadBounds {
		std::optional<int32_t> loop_start_frame;
		bool endless_silence{};
	};
	/// @brief A window check's outcome: read so many frames, or the head has stopped (at its end, or at a chunk not
	/// ready: every failed walk leaves it holding no chunk).
	struct Window {
		enum class Kind : uint8_t { READ, STOPPED } kind;
		int32_t frames;
	};
	static constexpr int32_t kMaxWalkTurns = 1024;

	/// @brief The chunk walk (R6): while the cursor is at or past its chunk's boundary, the source moves to the next
	/// chunk, or, in the final chunk, the head stops or a low-level loop restarts it at the loop's start plus the
	/// overshoot. False once the head has stopped: a failed move or restart, or 1,024 turns, leaves it holding no
	/// chunk.
	template <read_source S>
	[[nodiscard]] static bool walk(State& state, S& source, const ReadBounds& bounds, Direction direction) {
		const auto d = static_cast<int32_t>(direction);
		for (int32_t turns = 0;;) {
			const auto* reading = std::get_if<Reading>(&state.cursor);
			const std::optional<ChunkSpan> span = source.span(direction);
			if (reading == nullptr || !span) {
				return false;
			}
			const int32_t over = (reading->frame - span->boundary) * d;
			if (over < 0) {
				return true;
			}
			if (!span->final) {
				// The cursor keeps its frame; its chunk moves on one, through the source's region-port cursor
				if (source.move_to(span->boundary, direction) != Residency::READY) {
					source.let_go();
					state.cursor = Ended{0};
					return false;
				}
			}
			else {
				source.let_go();
				if (!bounds.loop_start_frame) {
					state.cursor = Ended{0};
					return false;
				}
				if (place(state, source, *bounds.loop_start_frame + (over * d), direction) != Residency::READY) {
					// A restart that is not ready leaves a stale cursor, which counts past every width
					state.cursor = Ended{kStaleZeroTaps};
					return false;
				}
			}
			if (++turns >= kMaxWalkTurns) {
				source.let_go();
				state.cursor = Ended{0};
				return false;
			}
		}
	}

	/// @brief The window check before a read (R1 to R5): fills or reshapes the window, takes the first output frame's
	/// step, and gives the frames the read may take, at most @p frames. The phase and increment are Q24 words: R3 and
	/// R4 are word arithmetic on them.
	template <read_source S>
	[[nodiscard]] static Window check(State& state, S& source, const ReadBounds& bounds, int32_t frames,
	                                  Increment increment, int32_t width, Direction direction) {
		if (increment.raw() == (1u << 24)) {
			return check_native(state, source, bounds, frames, direction);
		}
		if (!reshape(state, source, bounds, width, direction)
		    || !first_step(state, source, bounds, increment, width, direction)) {
			return {.kind = Window::Kind::STOPPED, .frames = 0};
		}
		return {.kind = Window::Kind::READ,
		        .frames = window_length(state, source, bounds, frames, increment, width, direction)};
	}

	/// @brief The 16-bit tap of @p frame's @p channel: the top word of the 32-bit read ending at its top byte.
	template <read_source S>
	[[nodiscard]] static int16_t tap(const S& source, int32_t frame, int32_t channel, Direction direction) {
		const std::optional<FrameRun> run = source.run(frame, direction);
		return run ? static_cast<int16_t>(frame_word(*run, 0, channel) >> 16) : int16_t{0};
	}

private:
	/// @brief The cursor to @p frame through the source (R10's seat, without H1's phase and width).
	template <read_source S>
	[[nodiscard]] static Residency place(State& state, S& source, int32_t frame, Direction direction) {
		if (frame < 0 || frame >= source.length_frames()) {
			return Residency::UNAVAILABLE;
		}
		const Residency residency = source.move_to(frame, direction);
		if (residency != Residency::READY) {
			state.cursor = Ended{kStaleZeroTaps};
			return residency;
		}
		state.cursor = Reading{frame};
		return residency;
	}

	/// R5: native. A window left from a resampled read steps the cursor back first.
	template <read_source S>
	[[nodiscard]] static Window check_native(State& state, S& source, const ReadBounds& bounds, int32_t frames,
	                                         Direction direction) {
		const int32_t frame_bytes = source.channels() * source.byte_depth();
		if (state.width_last != 0) {
			if (!source.span(direction).has_value()) {
				return {.kind = Window::Kind::STOPPED, .frames = 0};
			}
			step_back(state, source, (state.width_last >> 1) - static_cast<int32_t>(state.phase.raw() >> 23),
			          direction);
			state.width_last = 0;
			state.phase = UQ<24>{};
		}
		if (!walk(state, source, bounds, direction)) {
			return {.kind = Window::Kind::STOPPED, .frames = 0};
		}
		const int32_t bytes = bytes_to_boundary(state, source, direction, frame_bytes);
		if (bytes <= 0) {
			return {.kind = Window::Kind::STOPPED, .frames = 0}; // unreachable: a walk leaves the cursor before it
		}
		if (frames * frame_bytes > bytes) {
			frames = static_cast<int32_t>(static_cast<uint32_t>(bytes) / static_cast<uint8_t>(frame_bytes));
		}
		return {.kind = Window::Kind::READ, .frames = frames};
	}

	/// R1 (interpolating after not) and R2 (the width changed). False once the head has stopped.
	template <read_source S>
	[[nodiscard]] static bool reshape(State& state, S& source, const ReadBounds& bounds, int32_t width,
	                                  Direction direction) {
		const auto channels = static_cast<std::size_t>(source.channels());
		const auto w = static_cast<std::size_t>(width);
		if (state.width_last == 0) {
			state.width_last = static_cast<int8_t>(width);
			const std::size_t half = w >> 1;
			back_fill(state, source, half, w, direction);
			return forward_fill(state, source, bounds, half, w, direction);
		}
		if (state.width_last == width) {
			return true;
		}
		const auto last = static_cast<std::size_t>(static_cast<uint8_t>(state.width_last));
		if (w < last) {
			const std::size_t offset = (last - w) >> 1;
			state.window.narrow(offset, w, channels);
			step_back(state, source, static_cast<int32_t>(offset), direction);
		}
		else {
			const std::size_t offset = (w - last) >> 1;
			state.window.widen(offset, last, channels);
			if (!forward_fill(state, source, bounds, offset, w, direction)) {
				return false;
			}
			state.window.clear(w - offset, w, channels);
		}
		state.width_last = static_cast<int8_t>(width);
		return true;
	}

	/// R3: the window's first output frame's step. False once the head has stopped.
	template <read_source S>
	[[nodiscard]] static bool first_step(State& state, S& source, const ReadBounds& bounds, Increment increment,
	                                     int32_t width, Direction direction) {
		const auto d = static_cast<int32_t>(direction);
		uint32_t phase = state.phase.raw() + increment.raw();
		auto steps = static_cast<int32_t>(phase >> 24);
		state.phase = UQ<24>::from_raw(phase);
		if (steps == 0) {
			return true;
		}
		phase &= (1u << 24) - 1;
		state.phase = UQ<24>::from_raw(phase);
		const bool held = source.span(direction).has_value();
		if (auto* reading = std::get_if<Reading>(&state.cursor); reading != nullptr && held && steps > width) {
			reading->frame += (steps - width) * d;
			steps = width;
		}
		while (steps-- != 0) {
			if (source.span(direction).has_value() && walk(state, source, bounds, direction)) {
				push_frame(state, source, direction);
				continue;
			}
			if (source.span(direction).has_value()) {
				return false;
			}
			if (!push_zero(state, source, width, bounds.endless_silence)) {
				return false;
			}
		}
		return true;
	}

	/// R4: the window's length, at most @p frames.
	template <read_source S>
	[[nodiscard]] static int32_t window_length(const State& state, const S& source, const ReadBounds& bounds,
	                                           int32_t frames, Increment increment, int32_t width,
	                                           Direction direction) {
		if (frames < 2) {
			return frames;
		}
		const int32_t frame_bytes = source.channels() * source.byte_depth();
		const uint32_t phase = state.phase.raw();
		const auto want = static_cast<int32_t>(
		    ((static_cast<uint64_t>(increment.raw()) * static_cast<uint32_t>(frames - 1)) + phase) >> 24);
		uint32_t left = 0;
		bool shorten = false;
		if (const auto* ended = std::get_if<Ended>(&state.cursor);
		    ended != nullptr || !source.span(direction).has_value()) {
			if (bounds.endless_silence) {
				return frames;
			}
			const int32_t zero_taps = ended != nullptr ? ended->zero_taps : 0;
			left = static_cast<uint32_t>(width) - static_cast<uint32_t>(zero_taps);
			shorten = static_cast<uint32_t>(want) > left;
		}
		else {
			const int32_t bytes = bytes_to_boundary(state, source, direction, frame_bytes);
			shorten = want * frame_bytes > bytes;
			if (shorten) {
				// The legacy's 16-bit read of the bytes left: exact, a chunk's bytes (slack included) being fewer than
				// 2^16, and the cursor and every boundary whole frames from the data's start
				left = static_cast<uint16_t>(bytes) / static_cast<uint8_t>(frame_bytes);
			}
		}
		if (!shorten) {
			return frames;
		}
		const int64_t room = static_cast<int64_t>((static_cast<uint64_t>(left) + 1) << 24) - phase - 1;
		return static_cast<int32_t>(static_cast<uint64_t>(room) / increment.raw()) + 1;
	}

	template <read_source S>
	static int32_t bytes_to_boundary(const State& state, const S& source, Direction direction, int32_t frame_bytes) {
		const int32_t frames = (source.span(direction)->boundary - std::get<Reading>(state.cursor).frame)
		                       * static_cast<int32_t>(direction);
		return frames > 0 ? frames * frame_bytes : 0;
	}

	/// R1's back fill: slots @p from to @p to, each a frame further back, 0 before the chunk's floor or with no chunk.
	template <read_source S>
	static void back_fill(State& state, const S& source, std::size_t from, std::size_t to, Direction direction) {
		const std::optional<ChunkSpan> span = source.span(direction);
		const auto* reading = std::get_if<Reading>(&state.cursor);
		if (!span || reading == nullptr) {
			state.window.clear(from, to, 2);
			return;
		}
		const auto d = static_cast<int32_t>(direction);
		auto feed = state.window.fill_tail(from);
		int32_t frame = reading->frame;
		for (std::size_t i = from; i < to; i++) {
			frame -= d;
			if ((frame - span->floor) * d >= 0) {
				if (source.channels() == 2) {
					feed.put(tap(source, frame, 0, direction), tap(source, frame, 1, direction));
				}
				else {
					feed.put(tap(source, frame, 0, direction));
				}
			}
			else {
				feed.silence();
			}
		}
	}

	/// R1's forward fill of @p spaces slots, each after the walk; once it stops, zero taps, failing at @p width of
	/// them.
	template <read_source S>
	[[nodiscard]] static bool forward_fill(State& state, S& source, const ReadBounds& bounds, std::size_t spaces,
	                                       std::size_t width, Direction direction) {
		std::size_t remaining = spaces;
		while (remaining != 0) {
			if (!source.span(direction).has_value() || !walk(state, source, bounds, direction)) {
				return fill_silence(state, remaining, width);
			}
			const int32_t frame = std::get<Reading>(state.cursor).frame;
			auto feed = state.window.fill_head(remaining);
			if (source.channels() == 2) {
				feed.put(tap(source, frame, 0, direction), tap(source, frame, 1, direction));
			}
			else {
				feed.put(tap(source, frame, 0, direction));
			}
			remaining--;
			std::get<Reading>(state.cursor).frame = frame + static_cast<int32_t>(direction);
		}
		return true;
	}

	/// Zero taps into slots @p remaining - 1 down to 0, each counted; false once the count reaches @p width.
	static bool fill_silence(State& state, std::size_t remaining, std::size_t width) {
		int32_t zero_taps = 0;
		if (const auto* ended = std::get_if<Ended>(&state.cursor)) {
			zero_taps = ended->zero_taps;
		}
		auto feed = state.window.fill_head(remaining);
		while (feed.remaining() != 0) {
			feed.silence();
			zero_taps = counted(zero_taps);
			if (static_cast<uint32_t>(zero_taps) >= width) {
				state.cursor = Ended{zero_taps};
				return false;
			}
		}
		state.cursor = Ended{zero_taps};
		return true;
	}

	/// A zero tap counted; a stale count stays past every width.
	static constexpr int32_t counted(int32_t zero_taps) {
		return zero_taps == kStaleZeroTaps ? zero_taps : zero_taps + 1;
	}

	/// The cursor steps back up to @p frames frames, one at a time, stopping at the chunk's floor.
	template <read_source S>
	static void step_back(State& state, const S& source, int32_t frames, Direction direction) {
		auto* reading = std::get_if<Reading>(&state.cursor);
		const std::optional<ChunkSpan> span = source.span(direction);
		if (reading == nullptr || !span) {
			return;
		}
		const auto d = static_cast<int32_t>(direction);
		while (frames-- > 0) {
			if ((reading->frame - d - span->floor) * d < 0) {
				return;
			}
			reading->frame -= d;
		}
	}

	/// R3's zero tap: false once the count reaches @p width, unless the head reads silence for ever.
	template <read_source S>
	static bool push_zero(State& state, const S& source, int32_t width, bool endless) {
		state.window.push_l(0);
		if (source.channels() == 2) {
			state.window.push_r(0);
		}
		int32_t zero_taps = 1;
		if (const auto* ended = std::get_if<Ended>(&state.cursor)) {
			zero_taps = counted(ended->zero_taps);
		}
		state.cursor = Ended{zero_taps};
		return endless || zero_taps < width;
	}

	/// R3's frame: its taps pushed, the cursor on a frame.
	template <read_source S>
	static void push_frame(State& state, const S& source, Direction direction) {
		auto& reading = std::get<Reading>(state.cursor);
		state.window.push_l(tap(source, reading.frame, 0, direction));
		if (source.channels() == 2) {
			state.window.push_r(tap(source, reading.frame, 1, direction));
		}
		reading.frame += static_cast<int32_t>(direction);
	}

public:
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
