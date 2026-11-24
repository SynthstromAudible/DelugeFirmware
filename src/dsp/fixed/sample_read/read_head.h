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
#include "dsp/fixed/sample_read/mixed_in.h"

#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
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

	/// @brief Where a read mixes its frames: the output, its channels (1 condenses a stereo source), and the amplitude
	/// and its step, stepped before each frame.
	struct Output {
		std::span<Q<31>> out;
		int8_t out_channels;
		Q<31> amplitude;
		Q<31> step;
	};

	/// The reads are the legacy's word arithmetic: a sample's bytes are a word masked and condensed, the taps the top
	/// halfwords of words, the phase a Q24 word, the linear read's products integer words read as Q31 samples. So the
	/// crossings below are those words.
	/// @brief A native read of @p frames frames (R7): each sample its bytes masked from the 32-bit read ending at its
	/// top byte, mixed in at the stepped amplitude, the cursor on a frame each.
	template <read_source S>
	static void read_native(State& state, const S& source, Output& output, int32_t frames, Direction direction) {
		const int32_t depth = source.byte_depth();
		const uint32_t mask = 0xFFFFFFFFu << (32 - (8 * depth));
		const bool stereo = source.channels() == 2;
		auto& reading = std::get<Reading>(state.cursor);
		std::size_t at = 0;
		while (frames > 0) {
			const std::optional<FrameRun> run = source.run(reading.frame, direction);
			if (!run) {
				return; // a window check has said these frames are readable
			}
			const int32_t take = std::min(frames, run->frames);
			for (int32_t k = 0; k < take; k++) {
				auto l = static_cast<int32_t>(static_cast<uint32_t>(frame_word(*run, k, 0)) & mask);
				int32_t r = 0;
				if (stereo) {
					r = static_cast<int32_t>(static_cast<uint32_t>(frame_word(*run, k, 1)) & mask);
					if (output.out_channels == 1) {
						l = (l >> 1) + (r >> 1);
					}
				}
				mix(output, at, Q<31>::from_raw(l), Q<31>::from_raw(r));
			}
			reading.frame += take * static_cast<int32_t>(direction);
			frames -= take;
		}
	}

	/// @brief A resampled read of @p frames frames (R8, R9), its first output skipping its step (R3 took it): the
	/// windowed sinc for a width over 2, writing the cache's top three bytes of each channel where @p cache_write is
	/// set; else the 2-tap linear read.
	template <read_source S>
	static void read_resampled(State& state, const S& source, Output& output, int32_t frames, Increment increment,
	                           int32_t width, int32_t kernel, std::byte*& cache_write, Direction direction) {
		const auto channels = static_cast<std::size_t>(source.channels());
		const bool condense = channels == 2 && output.out_channels == 1;
		std::size_t at = 0;
		if (width > 2) {
			for (int32_t i = 0; i < frames; i++) {
				if (i != 0) {
					sinc_step(state, source, increment, direction);
				}
				StereoSample sample = state.window.interpolate(channels, kernel, state.phase.raw());
				if (cache_write != nullptr) {
					cache_word(cache_write, sample.l);
					if (channels == 2) {
						cache_word(cache_write, sample.r);
					}
				}
				if (condense) {
					sample.l = add_wrap(halve_toward_zero(sample.l), halve_toward_zero(sample.r));
				}
				mix(output, at, sample.l, sample.r);
			}
			return;
		}
		if (source.span(direction).has_value()) {
			read_linear(state, source, output, frames, increment, direction);
			return;
		}
		for (int32_t i = 0; i < frames; i++) {
			if (i != 0) {
				zero_step(state, source, increment);
			}
			StereoSample sample = state.window.interpolate_linear(channels, state.phase.raw());
			if (condense) {
				sample.l = add_wrap(halve_floor(sample.l), halve_floor(sample.r));
			}
			mix(output, at, sample.l, sample.r);
		}
	}

	/// @brief A head's read of @p frames frames (R11): window checks and the reads they allow until all are rendered,
	/// each window's first output skipping its step afresh. False once the head has stopped (every failed check leaves
	/// it holding no chunk). @p cache_write null writes no cache.
	template <read_source S>
	[[nodiscard]] static bool read(State& state, S& source, const ReadBounds& bounds, Output& output, int32_t frames,
	                               Increment increment, int32_t width, int32_t kernel, std::byte* cache_write,
	                               Direction direction) {
		std::size_t done = 0;
		while (frames != 0) {
			const Window window = check(state, source, bounds, frames, increment, width, direction);
			if (window.kind == Window::Kind::STOPPED) {
				return false;
			}
			Output part{.out = output.out.subspan(done * static_cast<std::size_t>(output.out_channels)),
			            .out_channels = output.out_channels,
			            .amplitude = output.amplitude,
			            .step = output.step};
			if (increment.raw() == (1u << 24)) {
				read_native(state, source, part, window.frames, direction);
			}
			else {
				read_resampled(state, source, part, window.frames, increment, width, kernel, cache_write, direction);
			}
			output.amplitude = part.amplitude;
			done += static_cast<std::size_t>(window.frames);
			frames -= window.frames;
		}
		return true;
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

	/// The amplitude stepped, then one frame mixed into the output at @p at, which moves on.
	static void mix(Output& output, std::size_t& at, Q<31> l, Q<31> r) {
		output.amplitude = add_wrap(output.amplitude, output.step);
		output.out[at] = mixed_in(output.out[at], l, output.amplitude);
		at++;
		if (output.out_channels == 2) {
			output.out[at] = mixed_in(output.out[at], r, output.amplitude);
			at++;
		}
	}

	/// The top three bytes of @p word, as the cache stores a sample.
	static void cache_word(std::byte*& to, Q<31> word) {
		const auto w = static_cast<uint32_t>(word.raw());
		*to++ = static_cast<std::byte>(w >> 8);
		*to++ = static_cast<std::byte>(w >> 16);
		*to++ = static_cast<std::byte>(w >> 24);
	}

	/// R8's step for a later output: holding a chunk, the window advances by the frames the phase passes (at most 16,
	/// the cursor jumping the rest unread); holding none, zero taps.
	template <read_source S>
	static void sinc_step(State& state, const S& source, Increment increment, Direction direction) {
		auto* reading = std::get_if<Reading>(&state.cursor);
		if (reading == nullptr || !source.span(direction).has_value()) {
			zero_step(state, source, increment);
			return;
		}
		uint32_t phase = state.phase.raw() + increment.raw();
		auto steps = static_cast<int32_t>(phase >> 24);
		if (steps != 0) {
			phase &= (1u << 24) - 1;
			const auto d = static_cast<int32_t>(direction);
			if (steps > kInterpolationMaxNumSamples) {
				reading->frame += (steps - kInterpolationMaxNumSamples) * d;
				steps = kInterpolationMaxNumSamples;
			}
			auto feed = state.window.advance(static_cast<std::size_t>(steps));
			while (feed.remaining() != 0) {
				if (source.channels() == 2) {
					feed.put(tap(source, reading->frame, 0, direction), tap(source, reading->frame, 1, direction));
				}
				else {
					feed.put(tap(source, reading->frame, 0, direction));
				}
				reading->frame += d;
			}
		}
		state.phase = UQ<24>::from_raw(phase);
	}

	/// The phase stepped with zero taps, each counted (jumpForwardZeroes).
	template <read_source S>
	static void zero_step(State& state, const S& source, Increment increment) {
		uint32_t phase = state.phase.raw() + increment.raw();
		auto steps = static_cast<int32_t>(phase >> 24);
		if (steps != 0) {
			phase &= (1u << 24) - 1;
			while (steps-- != 0) {
				state.window.push_l(0);
				if (source.channels() == 2) {
					state.window.push_r(0);
				}
				int32_t zero_taps = 1;
				if (const auto* ended = std::get_if<Ended>(&state.cursor)) {
					zero_taps = counted(ended->zero_taps);
				}
				// A head reading but holding no chunk counts as the legacy's cursor-as-count did
				state.cursor = Ended{zero_taps};
			}
		}
		state.phase = UQ<24>::from_raw(phase);
	}

	/// R9 over a held chunk: the two taps in hand, the second the cursor's frame.
	template <read_source S>
	static void read_linear(State& state, const S& source, Output& output, int32_t frames, Increment increment,
	                        Direction direction) {
		const bool stereo = source.channels() == 2;
		const bool condense = stereo && output.out_channels == 1;
		const auto d = static_cast<int32_t>(direction);
		auto& reading = std::get<Reading>(state.cursor);
		int16_t newest_l = state.window.window_l()[0];
		int16_t previous_l = state.window.window_l()[1];
		int16_t newest_r = stereo ? state.window.window_r()[0] : int16_t{0};
		int16_t previous_r = stereo ? state.window.window_r()[1] : int16_t{0};
		uint32_t phase = state.phase.raw();
		std::size_t at = 0;
		// A later output's step (jumpForwardLinear): a step of one moves the newest tap to the previous; a longer one
		// reads both afresh, jumping the frames before them unread
		auto step = [&] {
			phase += increment.raw();
			const auto steps = static_cast<int32_t>(phase >> 24);
			if (steps == 0) {
				return;
			}
			phase &= (1u << 24) - 1;
			if (steps > 2) {
				reading.frame += (steps - 2) * d;
			}
			if (steps >= 2) {
				previous_l = tap(source, reading.frame, 0, direction);
				previous_r = stereo ? tap(source, reading.frame, 1, direction) : int16_t{0};
				reading.frame += d;
			}
			else {
				previous_l = newest_l;
				previous_r = newest_r;
			}
			newest_l = tap(source, reading.frame, 0, direction);
			newest_r = stereo ? tap(source, reading.frame, 1, direction) : int16_t{0};
			reading.frame += d;
		};
		for (int32_t i = 0; i < frames; i++) {
			if (i != 0) {
				step();
			}
			// The 16-bit taps times their Q15 strengths, integer products read as the Q31 sample
			const auto strength2 = static_cast<int16_t>(phase >> 9);
			const auto strength1 = static_cast<int16_t>(32767 - strength2);
			auto l = Q<31>::from_raw((previous_l * strength1) + (newest_l * strength2));
			const auto r = Q<31>::from_raw((previous_r * strength1) + (newest_r * strength2));
			if (condense) {
				l = add_wrap(halve_floor(l), halve_floor(r));
			}
			mix(output, at, l, r);
		}
		auto taps = state.window.fill_head(2);
		if (stereo) {
			taps.put(previous_l, previous_r);
			taps.put(newest_l, newest_r);
		}
		else {
			taps.put(previous_l);
			taps.put(newest_l);
		}
		state.phase = UQ<24>::from_raw(phase);
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
