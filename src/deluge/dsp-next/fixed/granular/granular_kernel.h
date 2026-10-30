/*
 * Copyright © 2024 Mark Adams and Alter-Alter
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

#include "definitions_cxx.hpp" // kSampleRate
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/curves/quick_log.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/noise/cong.h"
#include "dsp-next/fixed/vca/pan.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

/// @file
/// @brief Granular: the granular effect as one processor kernel of stereo frames.
namespace deluge::dsp::fixed {

/// @brief The granular effect as one processor kernel of stereo frames, rendered in place as `Mono<Granular<>>`.
/// @details Every frame is written, with the grains' feedback, into an external ring of @ref kRingFrames frames that
/// the owner hands over with @ref set_ring and keeps across restarts. Up to @ref kGrains grains read the ring back,
/// each under a triangle window at its own pitch and pan; a grain starts every `period` frames once the ring is
/// @ref State::full. The grains' sum, scaled by the wet volume and low-passed, is added to the dry frame and, scaled
/// by the send amount, to the reverb send.
///
/// A slot the ring has not had written since it was taken up (@ref set_ring) or restarted (@ref restart) reads zero:
/// a tap at or past @ref State::written reads nothing, so the ring is never zeroed. A @ref clear keeps the slots
/// written before it.
///
/// The knobs act as they always have: each derived value is recomputed only when its own knob moves, and the grain
/// size from the period before the rate's own update, so the derived values live in the State (@ref State::levels)
/// and @ref prepare updates them once per block.
/// @tparam Noise The noise stream the block draws from (a @ref noise_stream): the grains' spray, direction, pitch
/// class, first-pass start and pan, drawn in that order as each grain starts.
template <noise_stream Noise = Cong>
struct Granular {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.
	using Context = Noise;           ///< The noise stream a starting grain draws from.

	static constexpr int32_t kRingFrames = 65536;         ///< The ring's length, frames.
	static constexpr int32_t kRingMask = kRingFrames - 1; ///< Wraps a ring index.
	static constexpr std::size_t kGrains = 8;             ///< Grains sounding at once.
	static constexpr int32_t kShift = 44 * 300;           ///< How far behind the write index a grain starts: 300 ms.
	/// @brief The wet signal's low-pass, one pole at about 4.5 kHz: a moveability of 1/4, Q31 (tan(f) = 1/3).
	static constexpr OnePole::Coefficients kWetLowPass = OnePole::coefficients({.moveability = 1 << 29});

	/// @brief The knobs, set each block.
	struct Settings {
		int32_t rate = 0;       ///< The rate knob, Q31 signed.
		int32_t mix = 0;        ///< The mix knob, Q31 signed.
		int32_t density = 0;    ///< The density knob, Q31 signed.
		int32_t randomness = 0; ///< The pitch-randomness knob, Q31 signed.
		bool operator==(const Settings&) const = default;
	};

	/// @brief The knobs as @ref prepare reads them.
	struct Coefficients {
		int32_t rate = 0;       ///< The rate knob, Q31 signed.
		int32_t mix = 0;        ///< The mix knob, Q31 signed.
		int32_t density = 0;    ///< The density knob, Q31 signed.
		int32_t randomness = 0; ///< The pitch-randomness knob made positive, Q31 from 0 to 1.
	};

	/// @brief What a block's steps share: the knobs' derived values.
	struct Block {
		int32_t size = 13230;         ///< Grain length, frames.
		int32_t period = 1260;        ///< Frames between grain starts.
		int32_t wet = 0;              ///< The wet volume, Q31.
		int32_t dry = 2147483647;     ///< The dry volume, Q31.
		int32_t feedback = 161061273; ///< The grains' feedback into the ring, Q31.
		int32_t randomness = 0;       ///< The pitch randomness, Q31 from 0 to 1.
	};

	/// @brief The knob positions the derived values were last computed from.
	struct Knobs {
		int32_t rate = 0;    ///< The rate knob, Q31 signed.
		int32_t mix = 0;     ///< The mix knob, Q31 signed.
		int32_t density = 0; ///< The density knob, Q31 signed.
	};

	/// @brief One grain: where it reads, how far it has gone, and its window, pitch and pan.
	struct Grain {
		int32_t length = 0;        ///< Frames; 0 is silent.
		int32_t start = 0;         ///< The ring index of its first read.
		int32_t counter = 0;       ///< Frames read so far.
		uint16_t pitch = 1024;     ///< The read speed, 1024 = unity (UQ6.10).
		int32_t vol_scale = 0;     ///< The window's rise per frame, Q31.
		int32_t vol_scale_max = 0; ///< The window's peak, Q31.
		bool reverse = false;      ///< Whether it reads backwards.
		int32_t pan_l = 0;         ///< Left amplitude, Q30 (1073741823 is unity).
		int32_t pan_r = 0;         ///< Right amplitude, Q30.
	};

	/// @brief The ring's cursor, the grains, the wet low-pass and the knobs' derived values.
	struct State {
		StereoSample* ring = nullptr; ///< The owner's ring of kRingFrames frames, or null.
		uint32_t write = 0;           ///< The ring index the next frame is written to; wraps lazily, at the next frame.
		uint32_t written = 0; ///< Ring slots [0, written) were written since the ring was taken up or restarted.
		/// Frames until the write index is next a multiple of the period: zero exactly at one. Set by prepare and at
		/// the wrap.
		int32_t until_start = 0;
		bool full = false;        ///< Whether grains may start: the write index passed half the ring.
		bool initialized = false; ///< Whether grains are free of the first pass: a block started at the ring's end.
		int32_t wraps_to_shutdown = 0; ///< Ring laps left before the owner restarts the effect; below zero, it is idle.
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		Grain grains[kGrains]{}; ///< The grains.
		OnePole::State lpf_l{};  ///< The left wet low-pass memory, Q31.
		OnePole::State lpf_r{};  ///< The right wet low-pass memory, Q31.
		q31* send = nullptr;     ///< This block's reverb send cursor, set by set_send; advanced per frame.
		q31 send_amount = 0;     ///< The reverb send amount, Q31.
		Block levels{};          ///< The knobs' derived values; the randomness is refreshed each block.
		Knobs knobs{};           ///< The knob positions @ref levels came from.
	};

	/// @brief The State lives in its wrapper through the block: eight grains and the knob values, about 330 bytes.
	static constexpr bool kStateInPlace = true;

	/// @return The knobs, the randomness made positive.
	[[nodiscard]] static Coefficients coefficients(const Settings& s) {
		return {.rate = s.rate, .mix = s.mix, .density = s.density, .randomness = toPositive(s.randomness)};
	}

	/// @return @p volume (Q31) divided by √2: the owner's post-FX volume while the effect renders.
	[[nodiscard]] [[gnu::always_inline]] static int32_t post_fx_volume(int32_t volume) {
		return multiply_32x32_rshift32(volume, ONE_OVER_SQRT2_Q31) << 1;
	}

	/// @brief Frees the grains of the first pass when the block starts at the ring's end, updates the derived values of
	/// the knobs that moved, and counts the frames to the next grain start.
	/// @return The block's derived values.
	[[nodiscard]] static Block prepare(const Coefficients& c, State& s, std::size_t /*n*/) {
		if (!s.initialized && s.write >= static_cast<uint32_t>(kRingFrames)) {
			s.initialized = true;
		}
		Block& l = s.levels;
		// The size depends on both the density and the period, so it follows this block's rate.
		const bool size_changed = s.knobs.density != c.density || s.knobs.rate != c.rate;
		if (s.knobs.rate != c.rate) {
			s.knobs.rate = c.rate;
			const int32_t raw = std::clamp<int32_t>((quick_log(c.rate) - 364249088) >> 21, 0, 256);
			const int32_t hz = std::max<int32_t>(1, ((360 * raw >> 8) * raw >> 8)); // 0 - 180 Hz, half-Hz units
			l.period = (kSampleRate << 1) / hz;
		}
		if (size_changed) {
			s.knobs.density = c.density;
			const q31 density = (c.density / 2) + 1073741824; // 0 to 1, Q31
			// Grains up to 8 periods long; past that, grains are stolen for new ones.
			l.size = 1760 + q31_mult(l.period << 3, density);
		}
		if (s.knobs.mix != c.mix) {
			s.knobs.mix = c.mix;
			const auto centred = static_cast<int32_t>(int64_t{c.mix} - 2147483648LL);
			// A cubic: at the top of the knob the offset leaves 2^31, which wraps to -2^31 and clamps to dry.
			const int32_t cubed =
			    multiply_32x32_rshift32_rounded(multiply_32x32_rshift32_rounded(centred, centred), centred) << 2;
			const auto cubic = static_cast<int32_t>(int64_t{cubed} + 2147483648LL);
			l.wet = std::max<int32_t>(0, cubic);
			l.dry = static_cast<int32_t>(std::clamp<int64_t>((2147483648LL - l.wet) << 3, 0, 2147483647));
			l.feedback = l.wet >> 1;
		}
		l.randomness = c.randomness;
		const uint32_t into_period = s.write % static_cast<uint32_t>(l.period);
		s.until_start = into_period == 0 ? 0 : l.period - static_cast<int32_t>(into_period);
		return l;
	}

	/// @brief One frame: starts a grain when one is due, sums the grains, writes the frame and their feedback to the
	/// ring, and mixes the wet sum into the frame and the send.
	/// @param x The frame coming in, Q31.
	/// @return The dry frame plus the low-passed wet sum, saturating, Q31.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& /*c*/, const Block& b, State& s,
	                                                              StereoSample x, Noise& noise) {
		if (s.write >= static_cast<uint32_t>(kRingFrames)) {
			s.write = 0;
			s.wraps_to_shutdown -= 1;
			s.until_start = 0;
		}
		const auto write_index = static_cast<int32_t>(s.write);
		if (s.until_start == 0) {
			if (s.full) [[unlikely]] {
				start_grain(b, s, write_index, noise);
			}
			s.until_start = b.period;
		}
		s.until_start -= 1;

		int32_t grains_l = 0;
		int32_t grains_r = 0;
		const StereoSample* const ring = s.ring;
		const uint32_t written = s.written;
		for (Grain& g : s.grains) {
			if (g.length > 0) {
				const int32_t half = g.length >> 1;
				// Triangle window.
				const int32_t vol =
				    g.counter <= half ? g.counter * g.vol_scale : g.vol_scale_max - (g.counter - half) * g.vol_scale;
				int32_t delta = g.reverse ? -g.counter : g.counter;
				if (g.pitch != 1024) {
					delta = (delta * g.pitch) >> 10;
				}
				const int32_t pos = (g.start + delta + kRingFrames) & kRingMask;
				const StereoSample tap =
				    static_cast<uint32_t>(pos) < written ? ring[pos] : StereoSample{.l = 0, .r = 0};
				grains_l =
				    multiply_accumulate_32x32_rshift32_rounded(grains_l, multiply_32x32_rshift32(tap.l, vol), g.pan_l);
				grains_r =
				    multiply_accumulate_32x32_rshift32_rounded(grains_r, multiply_32x32_rshift32(tap.r, vol), g.pan_r);
				g.counter++;
				if (g.counter >= g.length) {
					g.length = 0;
				}
			}
		}
		grains_l <<= 3;
		grains_r <<= 3;

		StereoSample& slot = s.ring[write_index];
		slot.l = multiply_accumulate_32x32_rshift32_rounded(x.l, grains_l, b.feedback);
		slot.r = multiply_accumulate_32x32_rshift32_rounded(x.r, grains_r, b.feedback);
		s.write += 1;
		// Not std::max: an out-of-line call in the frame loop at -fno-inline-functions.
		s.written = written > s.write ? written : s.write;

		const q31 wet_l = OnePole::step<OnePole::Mode::LOW_PASS>(kWetLowPass, {}, s.lpf_l, q31_mult(grains_l, b.wet));
		const q31 wet_r = OnePole::step<OnePole::Mode::LOW_PASS>(kWetLowPass, {}, s.lpf_r, q31_mult(grains_r, b.wet));
		// A little reverb covers much of the granular artifacts.
		*s.send = wrapping_add(*s.send, q31_mult(wrapping_add(wet_l, wet_r), s.send_amount));
		++s.send;
		return {.l = add_saturate(q31_mult(x.l, b.dry), wet_l), .r = add_saturate(q31_mult(x.r, b.dry), wet_r)};
	}

	/// @brief Event: the owner's ring, kRingFrames frames, taken up afresh: none of it reads until written.
	static void set_ring(State& s, StereoSample* ring) {
		s.ring = ring;
		s.written = 0;
	}
	/// @brief Event: the owner takes the ring back.
	static void clear_ring(State& s) { s.ring = nullptr; }
	/// @brief Event: this block's reverb send, a frame per frame rendered, and its amount (Q31).
	static void set_send(State& s, q31* send, q31 amount) {
		s.send = send;
		s.send_amount = amount;
	}
	/// @brief Event: the ring released and taken up again: writing from its start, none of it reading until written,
	/// no grains starting until it is half full again. The grains sounding go on.
	static void restart(State& s) {
		s.write = 0;
		s.written = 0;
		s.full = false;
	}
	/// @brief Event: the grains stopped and the first pass begun again, writing from the ring's start; the slots
	/// written before still read.
	static void clear(State& s) {
		for (Grain& g : s.grains) {
			g.length = 0;
		}
		s.initialized = false;
		s.write = 0;
		s.full = false;
	}
	/// @brief Event: sound is coming in; the effect runs on for as many ring laps as the last block's feedback takes
	/// to die away.
	static void arm_shutdown(State& s) {
		const int32_t feedback = s.levels.feedback;
		if (feedback < 33554432) {
			s.wraps_to_shutdown = 1;
		}
		else if (feedback <= 100663296) {
			s.wraps_to_shutdown = 2;
		}
		else if (feedback <= 218103808) {
			s.wraps_to_shutdown = 3;
		}
		else {
			s.wraps_to_shutdown = 4;
		}
	}
	/// @brief Event, after each block whether or not it rendered: grains may start once the write index has passed
	/// half the ring.
	static void note_full(State& s) {
		if (s.write > static_cast<uint32_t>(kRingFrames / 2)) {
			s.full = true;
		}
	}

private:
	/// @return The top 8 bits of a draw.
	[[nodiscard]] [[gnu::always_inline]] static uint8_t random255(Noise& noise) {
		return static_cast<uint8_t>(noise.next() >> 24);
	}
	/// @return A draw from 0 to @p upper, from its top 16 bits.
	[[nodiscard]] [[gnu::always_inline]] static int32_t random(Noise& noise, int32_t upper) {
		return static_cast<uint16_t>(noise.next() >> 16) % (upper + 1);
	}
	/// @return A triangle distribution from -1 to 1, Q31 (Irwin–Hall of two draws).
	[[nodiscard]] [[gnu::always_inline]] static q31 triangle(Noise& noise) {
		const auto u1 = static_cast<q31>(noise.next());
		const auto u2 = static_cast<q31>(noise.next());
		return add_saturate(u1, u2);
	}

	/// @brief Starts the first silent grain, if any, at @p write_index: its spray, direction, pitch, length, window and
	/// pan.
	[[gnu::noinline]] static void start_grain(const Block& b, State& s, int32_t write_index, Noise& noise) {
		const uint32_t write = s.write;
		for (Grain& g : s.grains) {
			if (g.length > 0) {
				continue;
			}
			g.length = b.size;
			const int32_t spray = random(noise, kRingFrames >> 1) - (kRingFrames >> 2);
			g.start = static_cast<int32_t>((write + kRingFrames - kShift + spray) & kRingMask);
			g.counter = 0;
			g.reverse = random255(noise) < 76;

			// The pitch class: more randomness reaches further from unison.
			const auto pitch_class =
			    static_cast<int8_t>(multiply_32x32_rshift32(q31_mult(triangle(noise), b.randomness), 7));
			switch (pitch_class) {
			case -3:
				g.pitch = 512; // octave down
				g.reverse = true;
				break;
			case -2:
				g.pitch = 767; // fourth down
				g.reverse = true;
				break;
			case -1:
				g.pitch = 1024; // unison reverse
				g.reverse = true;
				break;
			case 0:
				g.pitch = 1024; // unison
				break;
			case 1:
				g.pitch = 2048; // octave
				break;
			case 2:
				g.pitch = 1534; // fifth
				break;
			case 3:
				g.pitch = 2048; // octave reverse
				g.reverse = true;
				break;
			default:
				g.pitch = 3072; // octave and a fifth, reverse: rare even at full randomness
				g.reverse = true;
				break;
			}
			if (g.reverse) {
				g.start = (write_index + kRingFrames - 1) & kRingMask;
				g.length = g.pitch > 1024 ? std::min<int32_t>(g.length, 21659)  // the ring × 0.3305
				                          : std::min<int32_t>(g.length, 30251); // 1.48 s - 0.8 s
			}
			else if (g.pitch != 1024) {
				const int32_t start_max =
				    (write_index + g.length - ((g.length * g.pitch) >> 10) + kRingFrames) & kRingMask;
				if (g.pitch > 1024) {
					if (!(g.start < start_max && g.start > write_index)) {
						g.start = (start_max + kRingFrames - 1) & kRingMask;
					}
				}
				else if (!(g.start > start_max && g.start < write_index)) {
					g.start = (write_index + kRingFrames - 1) & kRingMask;
				}
			}
			if (!s.initialized) {
				// The first pass: read only what has been written.
				if (!g.reverse) {
					g.pitch = 1024;
					if (write > 13231) {
						const int32_t back = std::max<int32_t>(440, random(noise, static_cast<int32_t>(write - 2)));
						g.start = (write_index - back + kRingFrames) & kRingMask;
					}
					else {
						g.length = 0;
					}
				}
				else {
					g.pitch = std::min<uint16_t>(g.pitch, 1024);
					if (write > 13231) {
						g.length = std::min<int32_t>(g.length, static_cast<int32_t>(write - 2));
						g.start = (write_index - 1 + kRingFrames) & kRingMask;
					}
					else {
						g.length = 0;
					}
				}
			}
			if (g.length > 0) {
				g.vol_scale = 2147483647 / (g.length >> 1);
				g.vol_scale_max = g.vol_scale * (g.length >> 1);
				const PanAmplitudes pan = pan_amplitudes((random255(noise) - 128) << 23);
				g.pan_l = pan.left;
				g.pan_r = pan.right;
			}
			return;
		}
	}
};

} // namespace deluge::dsp::fixed
