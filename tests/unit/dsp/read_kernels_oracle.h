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
// The read head's scalar kernels as they were before the NEON data plane: the oracle every new kernel is judged
// against, word for word. Oracle derives from ReadHead so the control plane (check, walk, reshape) is the real one;
// only the reads and the helpers they use are these copies, which shadow ReadHead's names.

#include "dsp/fixed/sample_read/read_head.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <variant>

namespace read_kernels_oracle {

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;
using namespace deluge::dsp::fixed::sample_read;

// NOLINTBEGIN
struct Oracle : ReadHead {
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
	                           int32_t width, int32_t kernel, std::byte*& cache_write, Direction direction) {
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
				read_sinc_as<2>(state, *run, output, frames, increment, kernel, cache_write, direction);
			}
			else {
				read_sinc_as<1>(state, *run, output, frames, increment, kernel, cache_write, direction);
			}
			return;
		}
		if (run) {
			by_layout(source.channels(), output.out_channels, [&]<int Channels, int OutChannels> {
				read_linear_as<Channels, OutChannels>(state, *run, output, frames, increment, direction);
			});
			return;
		}
		read_linear_zeros(state, source, output, frames, increment);
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
	/// @brief A later sinc output's step (R8): the phase on by @p inc, and its whole part's frames fed to the window
	/// from @p at, a step longer than the window jumping the frames before the last ones unread.
	template <int Channels>
	[[gnu::always_inline]] static void sinc_feed(State& state, const std::byte*& at, uint32_t& phase, uint32_t inc,
	                                             int32_t stride, int32_t depth) {
		phase += inc;
		auto steps = static_cast<int32_t>(phase >> 24);
		if (steps == 0) {
			return;
		}
		phase &= (1u << 24) - 1;
		if (steps > kInterpolationMaxNumSamples) {
			at += static_cast<std::ptrdiff_t>(steps - kInterpolationMaxNumSamples) * stride;
			steps = kInterpolationMaxNumSamples;
		}
		auto feed = state.window.advance(static_cast<std::size_t>(steps));
		while (feed.remaining() != 0) {
			if constexpr (Channels == 2) {
				feed.put(top_word(at), top_word(at + depth));
			}
			else {
				feed.put(top_word(at));
			}
			at += stride;
		}
	}
	/// @brief A sinc read over @p run, which starts at the cursor (R8): its first output skips its step (R3 took it).
	template <int Channels>
	static void read_sinc_as(State& state, const FrameRun& run, Output& output, int32_t frames, Increment increment,
	                         int32_t kernel, std::byte*& cache_write, Direction direction) {
		if (frames <= 0) {
			return;
		}
		Reading& reading = *std::get_if<Reading>(&state.cursor);
		const std::byte* at = run.first + 2;
		const int32_t stride = run.stride;
		const auto depth = static_cast<int32_t>(static_cast<uint8_t>(run.byte_depth));
		uint32_t phase = state.phase.raw();
		const uint32_t inc = increment.raw();
		const int32_t stepped = frames_stepped(phase, inc, frames);
		const bool stereo_out = Channels == 2 && output.out_channels == 2;
		Q<31>* __restrict out = output.out.data();
		const Q<31>* const end = out + (static_cast<std::ptrdiff_t>(frames) * (stereo_out ? 2 : 1));
		Q<31> amplitude = output.amplitude;
		const Q<31> step = output.step;
		std::byte* cache = cache_write;
		auto emit = [&] [[gnu::always_inline]] {
			StereoSample sample = state.window.interpolate(Channels, kernel, phase);
			if (cache != nullptr) {
				cache_word(cache, sample.l);
				if constexpr (Channels == 2) {
					cache_word(cache, sample.r);
				}
			}
			if constexpr (Channels == 2) {
				if (stereo_out) {
					put<2>(out, amplitude, step, sample.l, sample.r);
					return;
				}
				sample.l = add_wrap(halve_toward_zero(sample.l), halve_toward_zero(sample.r));
			}
			put<1>(out, amplitude, step, sample.l, sample.r);
		};
		emit(); // the first output's step was the window check's
		while (out != end) {
			sinc_feed<Channels>(state, at, phase, inc, stride, depth);
			emit();
		}
		reading.frame += stepped * static_cast<int32_t>(direction);
		state.phase = UQ<24>::from_raw(phase);
		output.amplitude = amplitude;
		cache_write = cache;
	}
	/// @brief A linear read's two taps per channel, the previous and the newest.
	struct LinearTaps {
		int16_t newest_l;
		int16_t previous_l;
		int16_t newest_r;
		int16_t previous_r;

		/// @brief A later output's step (jumpForwardLinear): the phase on by @p inc; a step of one moves the newest tap
		/// to the previous, a longer one reads both afresh from @p at, jumping the frames before them unread.
		template <int Channels>
		[[gnu::always_inline]] void step(const std::byte*& at, uint32_t& phase, uint32_t inc, int32_t stride,
		                                 int32_t depth) {
			phase += inc;
			const auto steps = static_cast<int32_t>(phase >> 24);
			if (steps == 0) {
				return;
			}
			phase &= (1u << 24) - 1;
			if (steps > 2) {
				at += static_cast<std::ptrdiff_t>(steps - 2) * stride;
			}
			if (steps >= 2) {
				previous_l = top_word(at);
				if constexpr (Channels == 2) {
					previous_r = top_word(at + depth);
				}
				at += stride;
			}
			else {
				previous_l = newest_l;
				previous_r = newest_r;
			}
			newest_l = top_word(at);
			if constexpr (Channels == 2) {
				newest_r = top_word(at + depth);
			}
			at += stride;
		}
	};
	/// @brief A linear read over @p run, which starts at the cursor (R9): its first output skips its step (R3 took
	/// it).
	template <int Channels, int OutChannels>
	static void read_linear_as(State& state, const FrameRun& run, Output& output, int32_t frames, Increment increment,
	                           Direction direction) {
		if (frames <= 0) {
			return;
		}
		Reading& reading = *std::get_if<Reading>(&state.cursor);
		const std::byte* at = run.first + 2;
		const int32_t stride = run.stride;
		const auto depth = static_cast<int32_t>(static_cast<uint8_t>(run.byte_depth));
		LinearTaps taps{.newest_l = state.window.window_l()[0],
		                .previous_l = state.window.window_l()[1],
		                .newest_r = Channels == 2 ? state.window.window_r()[0] : int16_t{0},
		                .previous_r = Channels == 2 ? state.window.window_r()[1] : int16_t{0}};
		uint32_t phase = state.phase.raw();
		const uint32_t inc = increment.raw();
		const int32_t stepped = frames_stepped(phase, inc, frames);
		Q<31>* __restrict out = output.out.data();
		const Q<31>* const end = out + (static_cast<std::ptrdiff_t>(frames) * OutChannels);
		Q<31> amplitude = output.amplitude;
		const Q<31> step = output.step;
		auto emit = [&] [[gnu::always_inline]] {
			// The 16-bit taps times their Q15 strengths, integer products read as the Q31 sample
			const auto strength2 = static_cast<int16_t>(phase >> 9);
			const auto strength1 = static_cast<int16_t>(32767 - strength2);
			auto l = Q<31>::from_raw((taps.previous_l * strength1) + (taps.newest_l * strength2));
			Q<31> r{};
			if constexpr (Channels == 2) {
				r = Q<31>::from_raw((taps.previous_r * strength1) + (taps.newest_r * strength2));
			}
			if constexpr (Channels == 2 && OutChannels == 1) {
				l = add_wrap(halve_floor(l), halve_floor(r));
			}
			put<OutChannels>(out, amplitude, step, l, r);
		};
		emit(); // the first output's step was the window check's
		while (out != end) {
			taps.step<Channels>(at, phase, inc, stride, depth);
			emit();
		}
		auto tail = state.window.fill_head(2);
		if constexpr (Channels == 2) {
			tail.put(taps.previous_l, taps.previous_r);
			tail.put(taps.newest_l, taps.newest_r);
		}
		else {
			tail.put(taps.previous_l);
			tail.put(taps.newest_l);
		}
		reading.frame += stepped * static_cast<int32_t>(direction);
		state.phase = UQ<24>::from_raw(phase);
		output.amplitude = amplitude;
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

	/// A zero tap counted; a stale count stays past every width.
	static constexpr int32_t counted(int32_t zero_taps) {
		return zero_taps == kStaleZeroTaps ? zero_taps : zero_taps + 1;
	}
};
// NOLINTEND

} // namespace read_kernels_oracle
