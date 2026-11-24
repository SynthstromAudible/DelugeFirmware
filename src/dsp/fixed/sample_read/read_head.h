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
#include "dsp/fixed/sample_read/gather.h"
#include "dsp/fixed/sample_read/mixed_in.h"
#include "dsp/fixed/sample_read/quad_mix.h"
#include "dsp/fixed/sample_read/stage.h"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstring>
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
		by_layout(source.channels(), output.out_channels, [&]<int Channels, int OutChannels> {
			read_native_as<Channels, OutChannels>(state, source, output, frames, direction);
		});
	}

	/// @brief A resampled read of @p frames frames (R8, R9), its first output skipping its step (R3 took it): the
	/// windowed sinc for a width over 2, writing the cache's top three bytes of each channel where @p cache_write is
	/// set; else the 2-tap linear read.
	template <read_source S>
	static void read_resampled(State& state, const S& source, Output& output, int32_t frames, Increment increment,
	                           int32_t width, int32_t kernel, std::byte*& cache_write, Staging& staging,
	                           Direction direction) {
		// The window check bounded the read's frames to the chunk held: one run serves the whole read
		const auto* reading = std::get_if<Reading>(&state.cursor);
		const std::optional<FrameRun> run = reading != nullptr && source.span(direction).has_value()
		                                        ? source.run(reading->frame, direction)
		                                        : std::nullopt;
		if (width > 2) {
			if (!run) {
				read_sinc_zeros(state, source, output, frames, increment, kernel, cache_write);
				return;
			}
			// Templated on the source's channels only: the filter's cost dwarfs the per-frame test of the output's
			if (source.channels() == 2) {
				read_sinc_as<2>(state, *run, output, frames, increment, kernel, cache_write, staging, direction);
			}
			else {
				read_sinc_as<1>(state, *run, output, frames, increment, kernel, cache_write, staging, direction);
			}
			return;
		}
		if (run) {
			by_layout(source.channels(), output.out_channels, [&]<int Channels, int OutChannels> {
				read_linear_as<Channels, OutChannels>(state, *run, output, frames, increment, staging, direction);
			});
			return;
		}
		read_linear_zeros(state, source, output, frames, increment);
	}

	/// @brief A head's read of @p frames frames (R11): window checks and the reads they allow until all are rendered,
	/// each window's first output skipping its step afresh. False once the head has stopped (every failed check leaves
	/// it holding no chunk). @p cache_write null writes no cache. @p staging holds the taps the reads interpolate.
	template <read_source S>
	[[nodiscard]] static bool read(State& state, S& source, const ReadBounds& bounds, Output& output, int32_t frames,
	                               Increment increment, int32_t width, int32_t kernel, std::byte* cache_write,
	                               Staging& staging, Direction direction) {
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
				read_resampled(state, source, part, window.frames, increment, width, kernel, cache_write, staging,
				               direction);
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

	/// @brief The cursor to @p frame through the source (R10's seat, without H1's phase and width): an initial play's
	/// seat, and a low-level loop's restart.
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

private:
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

	/// @brief Calls @p f's template for the source's channels and the output's: each read's per-frame loop is
	/// compiled for its layout, as the legacy's linear read was, with no branch on it per frame. An output has no more
	/// channels than its source (a stereo source may be condensed to one; a mono one is never spread to two), as the
	/// legacy's linear read asserted and every caller renders.
	template <typename F>
	[[gnu::always_inline]] static void by_layout(int32_t channels, int32_t out_channels, const F& f) {
		if (channels == 2) {
			if (out_channels == 2) {
				f.template operator()<2, 2>();
			}
			else {
				f.template operator()<2, 1>();
			}
		}
		else {
			f.template operator()<1, 1>();
		}
	}
	/// @brief The 16-bit tap at @p at: the top word of the 32-bit read ending at a sample's top byte, @p at being two
	/// bytes into that read.
	[[nodiscard, gnu::always_inline]] static int16_t top_word(const std::byte* at) {
		// One halfword load at the same address as the legacy's int16_t cast. The build forbids unaligned access
		// (-mno-unaligned-access: unaligned doubleword and multiple loads fault), so a plain copy loads byte by byte. A
		// 24-bit sample's top word can sit at an odd byte, where the legacy's halfword loads ran on every 24-bit sample
		// read, so an unaligned halfword load is one the device already makes
		int16_t word = 0;
		std::memcpy(&word, __builtin_assume_aligned(at, 2), sizeof word);
		return word;
	}
	/// @brief The source frames a resampled read of @p frames outputs steps past its first: its phase's whole part
	/// after the later outputs' steps, a long step's jump included.
	[[nodiscard, gnu::always_inline]] static int32_t frames_stepped(uint32_t phase, uint32_t increment,
	                                                                int32_t frames) {
		return static_cast<int32_t>(
		    (static_cast<uint64_t>(phase) + (static_cast<uint64_t>(increment) * static_cast<uint32_t>(frames - 1)))
		    >> 24);
	}
	/// @brief Mixes one output frame at @p out, stepping the amplitude first, and moves @p out on.
	template <int OutChannels>
	[[gnu::always_inline]] static void put(Q<31>* __restrict& out, Q<31>& amplitude, Q<31> step, Q<31> l, Q<31> r) {
		amplitude = add_wrap(amplitude, step);
		out[0] = mixed_in(out[0], l, amplitude);
		if constexpr (OutChannels == 2) {
			out[1] = mixed_in(out[1], r, amplitude);
		}
		out += OutChannels;
	}
	// The kernels keep the cursor, phase, amplitude and output position in locals and write them back once: in State
	// and Output they would be reloaded after every output store, which the compiler cannot prove does not alias them.
	template <int Channels, int OutChannels, read_source S>
	static void read_native_as(State& state, const S& source, Output& output, int32_t frames, Direction direction) {
		const uint32_t mask = 0xFFFFFFFFu << (32 - (8 * source.byte_depth()));
		auto& reading = std::get<Reading>(state.cursor);
		Q<31>* __restrict out = output.out.data();
		Q<31> amplitude = output.amplitude;
		const Q<31> step = output.step;
		while (frames > 0) {
			const std::optional<FrameRun> run = source.run(reading.frame, direction);
			if (!run) {
				break; // a window check has said these frames are readable
			}
			const int32_t take = std::min(frames, run->frames);
			const std::byte* at = run->first;
			for (int32_t k = 0; k < take; k++, at += run->stride) {
				uint32_t word = 0;
				std::memcpy(&word, at, sizeof word);
				auto l = static_cast<int32_t>(word & mask);
				int32_t r = 0;
				if constexpr (Channels == 2) {
					std::memcpy(&word, at + run->byte_depth, sizeof word);
					r = static_cast<int32_t>(word & mask);
					if constexpr (OutChannels == 1) {
						l = (l >> 1) + (r >> 1);
					}
				}
				put<OutChannels>(out, amplitude, step, Q<31>::from_raw(l), Q<31>::from_raw(r));
			}
			reading.frame += take * static_cast<int32_t>(direction);
			frames -= take;
		}
		output.amplitude = amplitude;
	}
	/// @brief A sinc read over @p run, which starts at the cursor (R8): its first output skips its step (R3 took it).
	/// The run's frames are decoded into @p staging after the window's taps, the outputs gathered from it and mixed
	/// four at a time, and the window takes staging's taps at the last output.
	template <int Channels>
	static void read_sinc_as(State& state, const FrameRun& run, Output& output, int32_t frames, Increment increment,
	                         int32_t kernel, std::byte*& cache_write, Staging& staging, Direction direction) {
		if constexpr (Channels == 2) {
			if (output.out_channels == 1) {
				read_gathered<QuadMix<2, 1, Condense::TOWARD_ZERO>, 2, false>(state, run, output, frames, increment,
				                                                              kernel, cache_write, staging, direction);
				return;
			}
			read_gathered<QuadMix<2, 2, Condense::NONE>, 2, false>(state, run, output, frames, increment, kernel,
			                                                       cache_write, staging, direction);
		}
		else {
			read_gathered<QuadMix<1, 1, Condense::NONE>, 1, false>(state, run, output, frames, increment, kernel,
			                                                       cache_write, staging, direction);
		}
	}

	/// @brief Staging's frames past the history, which one sub-read of a sinc read decodes at most.
	static constexpr int32_t kStageRoom = kStageTaps - kHistoryTaps;

	/// @brief The outputs a sub-read from @p phase may gather, at most @p left: those whose newest tap is within
	/// staging's room. A read's first output takes no step; a later sub-read's first does. Not the last sub-read, a
	/// multiple of four, so the mix stays whole.
	[[nodiscard]] static int32_t sub_read_outputs(uint32_t phase, uint32_t inc, int32_t left, bool step_first) {
		// The newest tap after n outputs is (phase + (n or n − 1) · inc) >> 24 frames past the history's
		const uint64_t room = ((static_cast<uint64_t>(kStageRoom) + 1) << 24) - 1 - phase;
		const uint64_t steps = room / inc;
		const uint64_t fit = step_first ? steps : steps + 1;
		if (fit >= static_cast<uint64_t>(left)) {
			return left;
		}
		// At least one: a step can pass at most 255 frames, so three outputs always fit after a step
		const int32_t whole = static_cast<int32_t>(fit) & ~3;
		return whole > 0 ? whole : static_cast<int32_t>(fit);
	}

	/// @brief A resampled read through staging: the run's frames decoded after the window's taps, sub-read by sub-read,
	/// the outputs gathered (the windowed sinc, or with @p Linear the two newest taps) and mixed by @p Mix, then the
	/// window's taps from staging at the last output (all sixteen after a sinc read, the two a linear read uses after
	/// one, which leaves the rest as they were).
	template <class Mix, int Channels, bool Linear>
	static void read_gathered(State& state, const FrameRun& run, Output& output, int32_t frames, Increment increment,
	                          int32_t kernel, std::byte*& cache_write, Staging& staging, Direction direction) {
		if (frames <= 0) {
			return;
		}
		Reading& reading = *std::get_if<Reading>(&state.cursor);
		const uint32_t inc = increment.raw();
		const int32_t stepped = frames_stepped(state.phase.raw(), inc, frames);
		Mix mix(output.out.data(), output.amplitude, output.step, cache_write);
		load_history(staging, state.window, Channels);
		GatherPos pos{.newest = kStageRoom, .phase = state.phase.raw()};
		int32_t decoded = 0;
		bool step_first = false;
		for (int32_t left = frames; left > 0;) {
			const int32_t outputs = sub_read_outputs(pos.phase, inc, left, step_first);
			// The frames this sub-read's last output reaches, decoded into the slots below the history
			const auto steps = static_cast<uint64_t>(step_first ? outputs : outputs - 1);
			const auto needed = static_cast<int32_t>((pos.phase + (static_cast<uint64_t>(inc) * steps)) >> 24);
			decode_taps(run, decoded, needed, staging.l.data() + (kStageRoom - needed),
			            Channels == 2 ? staging.r.data() + (kStageRoom - needed) : nullptr);
			if constexpr (Linear) {
				pos = linear_gather<Channels>(staging, pos, inc, outputs, step_first, mix);
			}
			else {
				pos = sinc_gather<Channels>(staging, pos, inc, kernel, outputs, step_first, mix);
			}
			decoded += needed;
			left -= outputs;
			if (left > 0) {
				// The next sub-read's history is the window at this one's last output
				std::memmove(staging.l.data() + kStageRoom, staging.l.data() + pos.newest,
				             kHistoryTaps * sizeof(int16_t));
				if constexpr (Channels == 2) {
					std::memmove(staging.r.data() + kStageRoom, staging.r.data() + pos.newest,
					             kHistoryTaps * sizeof(int16_t));
				}
				pos.newest = kStageRoom;
				step_first = true;
			}
		}
		store_history(staging, pos.newest, state.window, Channels, Linear ? 2 : kHistoryTaps);
		reading.frame += stepped * static_cast<int32_t>(direction);
		state.phase = UQ<24>::from_raw(pos.phase);
		output.amplitude = mix.amplitude();
		cache_write = mix.cache();
	}

	/// @brief A linear read over @p run, which starts at the cursor (R9): its first output skips its step (R3 took
	/// it). Through staging as the sinc read is, with no cache.
	template <int Channels, int OutChannels>
	static void read_linear_as(State& state, const FrameRun& run, Output& output, int32_t frames, Increment increment,
	                           Staging& staging, Direction direction) {
		std::byte* no_cache = nullptr;
		if constexpr (Channels == 2 && OutChannels == 1) {
			read_gathered<QuadMix<2, 1, Condense::FLOOR>, 2, true>(state, run, output, frames, increment, 0, no_cache,
			                                                       staging, direction);
		}
		else {
			read_gathered<QuadMix<Channels, OutChannels, Condense::NONE>, Channels, true>(
			    state, run, output, frames, increment, 0, no_cache, staging, direction);
		}
	}

	/// @brief A sinc read of a head feeding zeros. The legacy's sinc read stepped a copy of its cursor and wrote the
	/// copy back, so the read ends with the count it began with.
	template <read_source S>
	static void read_sinc_zeros(State& state, const S& source, Output& output, int32_t frames, Increment increment,
	                            int32_t kernel, std::byte*& cache_write) {
		const auto channels = static_cast<std::size_t>(source.channels());
		const bool condense = channels == 2 && output.out_channels == 1;
		const std::variant<Reading, Ended> before = state.cursor;
		std::size_t at = 0;
		for (int32_t i = 0; i < frames; i++) {
			if (i != 0) {
				zero_step(state, source, increment);
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
		state.cursor = before;
	}
	/// @brief A linear read of a head feeding zeros: each zero counted, as the legacy's linear read counted them.
	template <read_source S>
	static void read_linear_zeros(State& state, const S& source, Output& output, int32_t frames, Increment increment) {
		const auto channels = static_cast<std::size_t>(source.channels());
		const bool condense = channels == 2 && output.out_channels == 1;
		std::size_t at = 0;
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

	/// The bytes from the cursor's frame to its chunk's boundary in @p direction, or 0 at or past it.
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
	/// chunk; with no region-port cursor of its own it can never move to the next chunk (R10's quirk), and the unison
	/// clone pairs it with a lease that opens one.
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
