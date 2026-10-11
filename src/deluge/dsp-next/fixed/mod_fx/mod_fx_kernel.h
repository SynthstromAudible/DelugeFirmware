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

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/bandlimited/waves.h"
#include "dsp-next/fixed/osc/sine_table.h"
#include "util/fixedpoint.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>

/// @file
/// @brief ModFx: chorus, dimension, flanger, warble and the phaser as one moded kernel of stereo frames.
namespace deluge::dsp::fixed {

/// @brief The mod FX as one processor kernel of stereo frames, rendered in place as `Mono<ModFx<Noise>>`.
/// @details The ring modes read an interpolated tap that an LFO moves through an external ring of @ref kRingFrames
/// frames, which the owner hands over with @ref set_ring and takes back with @ref clear_ring; the phaser runs six
/// stereo allpass stages. Every mode steps one LFO state, so its phase carries across a mode change; a change of the
/// LFO's wave zeroes the main warbler (@ref prepare). Warble steps a second warbler at 0.97 of the rate, every frame,
/// in stereo and in mono alike.
/// @tparam Noise A policy with `static q31 render()`: the warblers' random targets, main then second in each frame.
template <class Noise>
struct ModFx {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	/// @brief The arithmetic each loop runs: the type, and whether the right tap has its own position.
	enum class Mode : uint8_t {
		CHORUS,       ///< Chorus or stereo chorus, the right tap at the negated LFO.
		CHORUS_MONO,  ///< Chorus or stereo chorus, both taps at the left's position.
		DIMENSION,    ///< Dimension, stereo and mono alike: replaces the frame.
		FLANGER,      ///< Flanger, the right tap at the negated LFO.
		FLANGER_MONO, ///< Flanger, both taps at the left's position.
		WARBLE,       ///< Warble, the right tap at the second warbler.
		WARBLE_MONO,  ///< Warble, the right tap at the negated main LFO; the second warbler still steps.
		PHASER,       ///< Six stereo allpass stages with feedback; no ring.
	};
	/// @brief Every mode: one loop each.
	using Modes = ModeList<Mode::CHORUS, Mode::CHORUS_MONO, Mode::DIMENSION, Mode::FLANGER, Mode::FLANGER_MONO,
	                       Mode::WARBLE, Mode::WARBLE_MONO, Mode::PHASER>;

	static constexpr int32_t kRingFrames = 512;                                   ///< The ring's length.
	static constexpr int32_t kRingMask = kRingFrames - 1;                         ///< Wraps a ring index.
	static constexpr int32_t kMaxDelay = (kRingFrames - 1) << 16;                 ///< 16.16 frames.
	static constexpr int32_t kFlangerMinTime = 3 << 16;                           ///< 16.16 frames.
	static constexpr int32_t kFlangerAmplitude = kMaxDelay - kFlangerMinTime;     ///< 16.16 frames.
	static constexpr int32_t kFlangerOffset = (kMaxDelay + kFlangerMinTime) >> 1; ///< 16.16 frames.
	static constexpr std::size_t kAllpassStages = 6;                              ///< The phaser's stages.
	static constexpr q31 kWarbleWidth = 0.97 * ONE_Q31;                           ///< The second warbler's rate.

	/// @brief The LFO's wave in each mode.
	enum class Wave : uint8_t { SINE, TRIANGLE, WARBLER };

	/// @brief What the owner sets each block.
	struct Settings {
		Mode mode = Mode::CHORUS; ///< The loop.
		int32_t rate = 0;         ///< The LFO's phase increment per frame, UQ0.32 as a signed word.
		int32_t depth = 0;        ///< The depth param's final value, Q31.
		int32_t offset = 0;       ///< The offset knob, Q31 signed.
		int32_t feedback = 0;     ///< The feedback knob, Q31 signed.
		bool operator==(const Settings&) const = default;
	};

	/// @brief The block's tap, feedback and volume arithmetic, from the settings.
	struct Coefficients {
		uint32_t rate;            ///< The main LFO's increment, UQ0.32.
		uint32_t rate2;           ///< The second warbler's increment, UQ0.32.
		int32_t delay_offset;     ///< The tap's centre, 16.16 frames.
		int32_t delay_depth;      ///< The tap's swing, 16.16 frames per unit LFO.
		int32_t feedback;         ///< Q31.
		int32_t depth;            ///< The phaser's depth, Q31.
		int32_t volume_gain;      ///< The post-FX volume's multiplier, Q31.
		int32_t volume_shift;     ///< The post-FX volume's shift after the multiply: 0 or 1.
		Mode mode = Mode::CHORUS; ///< The loop.
	};

	/// @brief A warbler's held value, target and approach speed.
	struct Warbler {
		int32_t held = 0;   ///< Q31.
		int32_t target = 0; ///< Q31.
		int32_t speed = 0;  ///< Q31 per frame.
	};

	/// @brief The ring, the LFOs and the phaser's memories.
	struct State {
		StereoSample* ring = nullptr; ///< The owner's ring of kRingFrames frames, or null.
		int32_t write = 0;            ///< The ring index the next frame is written to.
		uint32_t phase = 0;           ///< The main LFO's phase, UQ0.32.
		uint32_t phase2 = 0;          ///< The second warbler's phase, UQ0.32.
		Warbler warbler{};            ///< The main LFO's warbler.
		Warbler warbler2{};           ///< The second warbler.
		Wave wave = Wave::SINE;       ///< The wave the main LFO last stepped.
		StereoSample phaser{};        ///< The phaser's feedback memory, Q31.
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		StereoSample allpass[kAllpassStages]{}; ///< The allpass stages' memories, Q31.
	};

	/// @return The coefficients for @p s: today's setup arithmetic for the mode's type.
	[[nodiscard]] static Coefficients coefficients(const Settings& s) {
		Coefficients c{
		    .rate = static_cast<uint32_t>(s.rate),
		    .rate2 = static_cast<uint32_t>(multiply_32x32_rshift32(s.rate, kWarbleWidth) << 1),
		    .delay_offset = 0,
		    .delay_depth = 0,
		    .feedback = 0,
		    .depth = s.depth,
		    .volume_gain = 1518500250, // 1/sqrt(2)
		    .volume_shift = 1,
		    .mode = s.mode,
		};
		if (is_chorus_like(s.mode)) {
			c.delay_offset = multiply_32x32_rshift32(kMaxDelay, (s.offset >> 1) + 1073741824);
			c.delay_depth = multiply_32x32_rshift32(c.delay_offset, s.depth) << 2;
			return c;
		}
		const int32_t a = s.feedback >> 1;
		const int32_t b = 2147483647 - ((a + 1073741824) >> 2) * 3;
		const int32_t b2 = multiply_32x32_rshift32(b, b);
		const int32_t b3 = multiply_32x32_rshift32(b, b2);
		c.feedback = static_cast<int32_t>(2147483648LL - (b3 << 2));
		// The volume cut at high feedback.
		const int32_t squared = multiply_32x32_rshift32(c.feedback, c.feedback) << 1;
		int32_t squared2 = multiply_32x32_rshift32(squared, squared) << 1;
		squared2 = multiply_32x32_rshift32(squared2, squared) << 1;
		squared2 = (multiply_32x32_rshift32(squared2, squared2) >> 4) * 23;
		c.volume_gain = 2147483647 - squared2;
		if (s.mode == Mode::FLANGER || s.mode == Mode::FLANGER_MONO) {
			c.delay_offset = kFlangerOffset;
			c.delay_depth = kFlangerAmplitude;
		}
		else if (s.mode == Mode::WARBLE || s.mode == Mode::WARBLE_MONO) {
			c.delay_offset = kFlangerOffset + multiply_32x32_rshift32(kFlangerOffset, s.offset);
			c.delay_depth = multiply_32x32_rshift32(c.delay_offset, s.depth) << 1;
		}
		else { // PHASER
			c.volume_shift = 0;
		}
		return c;
	}

	/// @return @p volume after the mode's gain: what the owner multiplies its post-FX volume by, whether or not the
	/// block renders.
	[[nodiscard]] [[gnu::always_inline]] static int32_t post_fx_volume(const Coefficients& c, int32_t volume) {
		return multiply_32x32_rshift32(volume, c.volume_gain) << c.volume_shift;
	}

	/// @brief Zeroes the main warbler when the mode's wave is not the one the main LFO last stepped.
	[[nodiscard]] static EmptyBlock prepare(const Coefficients& c, State& s, std::size_t /*n*/) {
		const Wave w = wave_of(c.mode);
		if (w != s.wave) {
			s.warbler = {};
			s.wave = w;
		}
		return {};
	}

	/// @brief One frame at mode @p M.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& c, const EmptyBlock& /*b*/,
	                                                              State& s, StereoSample x) {
		if constexpr (M == Mode::PHASER) {
			return phaser_step(c, s, x);
		}
		else {
			return ring_step<M>(c, s, x);
		}
	}

	/// @brief Event: the owner's ring, kRingFrames frames, for the ring modes.
	static void set_ring(State& s, StereoSample* ring) { s.ring = ring; }
	/// @brief Event: the owner takes the ring back; the ring modes no longer run.
	static void clear_ring(State& s) { s.ring = nullptr; }
	/// @brief Event: zeroes the ring, keeping the write index.
	static void zero_ring(State& s) {
		if (s.ring != nullptr) {
			std::memset(s.ring, 0, kRingFrames * sizeof(StereoSample));
		}
	}
	/// @brief Event: zeroes the ring when one is held, otherwise the phaser's memories.
	static void clear_memory(State& s) {
		if (s.ring != nullptr) {
			zero_ring(s);
		}
		else {
			s.phaser = {};
			for (StereoSample& a : s.allpass) {
				a = {};
			}
		}
	}
	/// @brief Event: advances the main LFO's phase by @p n frames at @p rate (UQ0.32), as while rendering is skipped.
	static void tick(State& s, uint32_t n, uint32_t rate) { s.phase += rate * n; }

private:
	/// @return Whether @p m runs the chorus setup: chorus or dimension.
	[[nodiscard]] static constexpr bool is_chorus_like(Mode m) {
		return m == Mode::CHORUS || m == Mode::CHORUS_MONO || m == Mode::DIMENSION;
	}

	/// @return The main LFO's wave in mode @p m.
	[[nodiscard]] static constexpr Wave wave_of(Mode m) {
		switch (m) {
		case Mode::DIMENSION:
		case Mode::FLANGER:
		case Mode::FLANGER_MONO:
			return Wave::TRIANGLE;
		case Mode::WARBLE:
		case Mode::WARBLE_MONO:
			return Wave::WARBLER;
		default:
			return Wave::SINE;
		}
	}

	/// @return The LFO's value at @p phase, then advances it one frame at @p inc.
	template <Wave W>
	[[nodiscard]] [[gnu::always_inline]] static int32_t lfo(uint32_t& phase, Warbler& w, uint32_t inc) {
		if constexpr (W == Wave::SINE) {
			const int32_t v = sine_table.value(phase);
			phase += inc;
			return v;
		}
		else if constexpr (W == Wave::TRIANGLE) {
			const int32_t v = triangle(phase);
			phase += inc;
			return v;
		}
		else {
			inc *= 2; // the warbler sweeps at twice the rate
			if (phase + inc < phase) {
				w.target = Noise::render();
				w.speed = 0;
			}
			const int32_t distance = wrapping_sub(w.target, w.held);
			w.speed = wrapping_add(w.speed, multiply_32x32_rshift32(distance, static_cast<int32_t>(inc >> 8)));
			w.held = add_saturate(w.held, w.speed);
			phase += inc;
			return w.held;
		}
	}

	/// @brief The tap at @p delay (16.16 frames behind the write index) on one channel, interpolated.
	struct Tap {
		int32_t strength1; ///< Q31 weight of the nearer slot.
		int32_t strength2; ///< Q31 weight of the farther slot.
		int32_t pos;       ///< The nearer slot's unmasked index.
	};
	[[nodiscard]] [[gnu::always_inline]] static Tap tap_at(const State& s, int32_t delay) {
		const int32_t strength2 = (delay & 65535) << 15;
		return {.strength1 = (65535 << 15) - strength2, .strength2 = strength2, .pos = s.write - (delay >> 16)};
	}

	/// @brief One ring-mode frame: LFOs, taps, the ring write, the mix.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample ring_step(const Coefficients& c, State& s,
	                                                                   StereoSample x) {
		constexpr bool kWarble = M == Mode::WARBLE || M == Mode::WARBLE_MONO;
		constexpr bool kOwnRightTap = M == Mode::CHORUS || M == Mode::FLANGER || M == Mode::DIMENSION || kWarble;
		const int32_t lfo1 = lfo<wave_of(M)>(s.phase, s.warbler, c.rate);
		const int32_t lfo2 = [&] [[gnu::always_inline]] () {
			if constexpr (M == Mode::WARBLE) {
				return lfo<Wave::WARBLER>(s.phase2, s.warbler2, c.rate2);
			}
			else {
				if constexpr (M == Mode::WARBLE_MONO) {
					// The second warbler steps in mono too, so the noise draws match stereo's.
					std::ignore = lfo<Wave::WARBLER>(s.phase2, s.warbler2, c.rate2);
				}
				return wrapping_sub(0, lfo1);
			}
		}();
		StereoSample* const ring = s.ring;
		Tap t = tap_at(s, multiply_32x32_rshift32(lfo1, c.delay_depth) + c.delay_offset);
		int32_t out_l = multiply_32x32_rshift32_rounded(ring[t.pos & kRingMask].l, t.strength1)
		                + multiply_32x32_rshift32_rounded(ring[(t.pos - 1) & kRingMask].l, t.strength2);
		if constexpr (kOwnRightTap) {
			t = tap_at(s, multiply_32x32_rshift32(lfo2, c.delay_depth) + c.delay_offset);
		}
		int32_t out_r = multiply_32x32_rshift32_rounded(ring[t.pos & kRingMask].r, t.strength1)
		                + multiply_32x32_rshift32_rounded(ring[(t.pos - 1) & kRingMask].r, t.strength2);

		StereoSample& slot = ring[s.write];
		if constexpr (M == Mode::FLANGER || M == Mode::FLANGER_MONO) {
			out_l = multiply_32x32_rshift32_rounded(out_l, c.feedback) << 2;
			slot.l = wrapping_add(out_l, x.l);
			out_r = multiply_32x32_rshift32_rounded(out_r, c.feedback) << 2;
			slot.r = wrapping_add(out_r, x.r);
		}
		else if constexpr (kWarble) {
			slot.l = wrapping_add(multiply_32x32_rshift32_rounded(out_l, c.feedback), x.l);
			slot.r = wrapping_add(multiply_32x32_rshift32_rounded(out_r, c.feedback), x.r);
			out_l <<= 1;
			out_r <<= 1;
		}
		else {
			out_l <<= 1;
			slot.l = x.l;
			out_r <<= 1;
			slot.r = x.r;
		}
		s.write = (s.write + 1) & kRingMask;
		if constexpr (M == Mode::DIMENSION || kWarble) {
			return {.l = out_l << 1, .r = out_r << 1};
		}
		else {
			return {.l = wrapping_add(x.l, out_l), .r = wrapping_add(x.r, out_r)};
		}
	}

	/// @brief One phaser frame.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample phaser_step(const Coefficients& c, State& s,
	                                                                     StereoSample x) {
		const int32_t lfo1 = lfo<Wave::SINE>(s.phase, s.warbler, c.rate);
		// "1" is about 2^30 here.
		const int32_t a1 = wrapping_sub(
		    1073741824, multiply_32x32_rshift32_rounded(
		                    static_cast<int32_t>((static_cast<uint32_t>(lfo1) + 2147483648U) >> 1), c.depth));
		const int32_t minus_a1 = wrapping_sub(0, a1);
		s.phaser.l = wrapping_add(x.l, multiply_32x32_rshift32_rounded(s.phaser.l, c.feedback) << 1);
		s.phaser.r = wrapping_add(x.r, multiply_32x32_rshift32_rounded(s.phaser.r, c.feedback) << 1);
		for (StereoSample& stage : s.allpass) {
			const StereoSample in = s.phaser;
			s.phaser.l = wrapping_add(multiply_32x32_rshift32_rounded(s.phaser.l, minus_a1) << 2, stage.l);
			stage.l = wrapping_add(multiply_32x32_rshift32_rounded(s.phaser.l, a1) << 2, in.l);
			s.phaser.r = wrapping_add(multiply_32x32_rshift32_rounded(s.phaser.r, minus_a1) << 2, stage.r);
			stage.r = wrapping_add(multiply_32x32_rshift32_rounded(s.phaser.r, a1) << 2, in.r);
		}
		return {.l = wrapping_add(x.l, s.phaser.l), .r = wrapping_add(x.r, s.phaser.r)};
	}
};

} // namespace deluge::dsp::fixed
