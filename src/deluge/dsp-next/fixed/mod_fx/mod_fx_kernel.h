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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/q31_ops.h" // kOneQ31
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/bandlimited/waves.h"
#include "dsp-next/fixed/noise/cong.h"
#include "dsp-next/fixed/osc/sine_table.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>

/// @file
/// @brief ModFx: chorus, dimension, flanger, warble and the phaser as one moded kernel of stereo frames.
namespace deluge::dsp::fixed {

/// @brief The mod FX as one processor kernel of stereo frames, rendered in place as `Mono<ModFx<>>`.
/// @details The ring modes read an interpolated tap that an LFO moves through an external ring of @ref kRingFrames
/// frames, which the owner hands over with @ref set_ring and takes back with @ref clear_ring; the phaser runs six
/// stereo allpass stages. Every mode steps one LFO state, so its phase carries across a mode change; a change of the
/// LFO's wave zeroes the main warbler (@ref prepare). Warble steps a second warbler at 0.97 of the rate, every frame,
/// in stereo and in mono alike.
/// @tparam Noise The noise stream the block draws from (a @ref noise_stream): the warblers' random targets, main then
/// second in each frame.
template <noise_stream Noise = Cong>
struct ModFx {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.
	using Context = Noise;           ///< The noise stream the warblers draw their targets from.

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
	static constexpr Q<31> kWarbleWidth =
	    Q<31>::from_raw(static_cast<int32_t>(0.97 * kOneQ31)); ///< The second warbler's rate.

	/// @brief The LFO's wave in each mode.
	enum class Wave : uint8_t { SINE, TRIANGLE, WARBLER };

	/// @brief What the owner sets each block.
	struct Settings {
		Mode mode = Mode::CHORUS; ///< The loop.
		UQ<32> rate{};            ///< The LFO's phase increment per frame.
		Q<31> depth{};            ///< The depth param's final value.
		Q<31> offset{};           ///< The offset knob.
		Q<31> feedback{};         ///< The feedback knob.
		bool operator==(const Settings&) const = default;
	};

	/// @brief The block's tap, feedback and volume arithmetic, from the settings.
	struct Coefficients {
		uint32_t rate;            ///< The main LFO's increment, UQ0.32.
		uint32_t rate2;           ///< The second warbler's increment, UQ0.32.
		Q<16> delay_offset;       ///< The tap's centre, in frames.
		Q<17> delay_depth;        ///< The tap's swing: the Q31 LFO times it is 16.16 frames.
		Q<31> feedback;           ///< The feedback.
		Q<31> depth;              ///< The phaser's depth.
		Q<31> volume_gain;        ///< The post-FX volume's multiplier.
		int32_t volume_shift;     ///< The post-FX volume's shift after the multiply: 0 or 1.
		Mode mode = Mode::CHORUS; ///< The loop.
	};

	/// @brief A warbler's held value, target and approach speed.
	struct Warbler {
		Q<31> held{};   ///< The value held.
		Q<31> target{}; ///< The value it springs toward.
		Q<31> speed{};  ///< Per frame.
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
		// The rate's bits read as Q31 (it is below 2^31), times 0.97; the product's bits as the UQ0.32 increment.
		const auto rate2 = reinterpret_bits<UQ<32>>(mul_as_wrap<Q<31>>(reinterpret_bits<Q<31>>(s.rate), kWarbleWidth));
		Coefficients c{
		    .rate = s.rate.raw(),
		    .rate2 = rate2.raw(),
		    .delay_offset = Q<16>{},
		    .delay_depth = Q<17>{},
		    .feedback = Q<31>{},
		    .depth = s.depth,
		    .volume_gain = Q<31>::from_raw(1518500250), // 1/sqrt(2)
		    .volume_shift = 1,
		    .mode = s.mode,
		};
		const Q<31> depth = s.depth;
		if (is_chorus_like(s.mode)) {
			// The offset knob as [0, 1); the Q15 product with the 16.16 longest delay is read as 16.16, so the centre
			// runs to half the ring. The swing is the centre times the depth, at Q17.
			const Q<31> knob = add_wrap(halve_floor(s.offset), Q<31>::from_raw(1 << 30));
			const auto centre = reinterpret_bits<Q<16>>(mul(Q<16>::from_raw(kMaxDelay), knob));
			c.delay_offset = centre;
			c.delay_depth = as_wrap<Q<17>>(mul(centre, depth));
			return c;
		}
		// The feedback 1 − b³, b running from 1 down to a quarter as the knob rises: 2^31 less the cube at Q31, as
		// min less it wrapping.
		// Raw: an integer scale of the knob's quarter by 3.
		const auto b = Q<31>::from_raw(2147483647 - (((s.feedback.raw() >> 1) + 1073741824) >> 2) * 3);
		const Q<29> b3 = mul(b, mul(b, b));
		const Q<31> feedback = sub_wrap(Q<31>::min(), as_wrap<Q<31>>(b3));
		c.feedback = feedback;
		// The volume cut at high feedback: 1 − 23/32 × the feedback's twelfth power.
		const auto squared = mul_as_wrap<Q<31>>(feedback, feedback);
		const auto fourth = mul_as_wrap<Q<31>>(squared, squared);
		const auto sixth = mul_as_wrap<Q<31>>(fourth, squared);
		const Q<30> twelfth = mul(sixth, sixth);
		// Raw: an integer scale of the Q30 power's bits by 23/16, read as Q31 (so 23/32 of it).
		c.volume_gain = sub_wrap(Q<31>::max(), Q<31>::from_raw((twelfth.raw() >> 4) * 23));
		if (s.mode == Mode::FLANGER || s.mode == Mode::FLANGER_MONO) {
			// The 16.16 amplitude read as Q17, half of it, so the LFO's ±1 swings from the shortest delay to the
			// longest.
			c.delay_offset = Q<16>::from_raw(kFlangerOffset);
			c.delay_depth = Q<17>::from_raw(kFlangerAmplitude);
		}
		else if (s.mode == Mode::WARBLE || s.mode == Mode::WARBLE_MONO) {
			// The Q15 product read as 16.16: the offset knob moves the centre by up to half of it either way. The
			// swing is the centre times the depth at Q16, read as Q17: half the chorus's.
			const auto base = Q<16>::from_raw(kFlangerOffset);
			const Q<16> centre = add_wrap(base, reinterpret_bits<Q<16>>(mul(base, s.offset)));
			c.delay_offset = centre;
			// The swing at Q16, its bits read as Q17: half the chorus's
			c.delay_depth = reinterpret_bits<Q<17>>(as_wrap<Q<16>>(mul(centre, depth)));
		}
		else { // PHASER
			c.volume_shift = 0;
		}
		return c;
	}

	/// @return @p volume after the mode's gain: what the owner multiplies its post-FX volume by, whether or not the
	/// block renders.
	[[nodiscard]] [[gnu::always_inline]] static Q<27> post_fx_volume(const Coefficients& c, Q<27> volume) {
		// The Q27 volume times the Q31 gain is Q26 kept in the Q27 word, half the product; the shift of 1 restores
		// it, and the phaser's 0 keeps it halved. A run-time shift, so raw.
		return Q<27>::from_raw(mul(volume, c.volume_gain).raw() << c.volume_shift);
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
	                                                              State& s, StereoSample x, Noise& noise) {
		if constexpr (M == Mode::PHASER) {
			return phaser_step(c, s, x, noise);
		}
		else {
			return ring_step<M>(c, s, x, noise);
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
	/// @brief Event: zeroes the phaser's memories (its allpass stages and feedback memory).
	static void clear_phaser(State& s) {
		s.phaser = {};
		for (StereoSample& a : s.allpass) {
			a = {};
		}
	}
	/// @brief Event: zeroes the ring when one is held, otherwise the phaser's memories.
	static void clear_memory(State& s) {
		if (s.ring != nullptr) {
			zero_ring(s);
		}
		else {
			clear_phaser(s);
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
	[[nodiscard]] [[gnu::always_inline]] static Q<31> lfo(uint32_t& phase, Warbler& w, uint32_t inc,
	                                                      [[maybe_unused]] Noise& noise) {
		if constexpr (W == Wave::SINE) {
			const auto v = Q<31>::from_raw(kSineTable.value(phase));
			phase += inc;
			return v;
		}
		else if constexpr (W == Wave::TRIANGLE) {
			const auto v = Q<31>::from_raw(triangle(phase));
			phase += inc;
			return v;
		}
		else {
			inc *= 2; // the warbler sweeps at twice the rate
			if (phase + inc < phase) {
				w.target = Q<31>::from_raw(static_cast<int32_t>(noise.next()));
				w.speed = Q<31>{};
			}
			// The spring: the distance to the target, saturating so the pull points toward even a target more than
			// 2^31 away, times the increment / 2^8 as a Q31 gain, its Q30 product's bits added to the Q31 speed; the
			// held value saturates at a rail.
			const Q<31> distance = sub_sat(w.target, w.held);
			const Q<30> pull = mul(distance, Q<31>::from_raw(static_cast<int32_t>(inc >> 8)));
			const Q<31> speed = add_wrap(w.speed, reinterpret_bits<Q<31>>(pull));
			w.speed = speed;
			w.held = add_sat(w.held, speed);
			phase += inc;
			return w.held;
		}
	}

	/// @brief The tap at @p delay (16.16 frames behind the write index) on one channel, interpolated.
	struct Tap {
		Q<31> strength1; ///< The nearer slot's weight.
		Q<31> strength2; ///< The farther slot's weight.
		int32_t pos;     ///< The nearer slot's unmasked index.
	};
	[[nodiscard]] [[gnu::always_inline]] static Tap tap_at(const State& s, int32_t delay) {
		// The delay's 16-bit fraction as the farther slot's weight, the nearer's its complement from 65535.
		const auto strength2 = as_wrap<Q<31>>(Q<16>::from_raw(delay & 65535));
		return {.strength1 = sub_wrap(as_wrap<Q<31>>(Q<16>::from_raw(65535)), strength2),
		        .strength2 = strength2,
		        .pos = s.write - (delay >> 16)};
	}
	/// @return The tap on one channel's ring words: the two slots weighed, Q30.
	[[nodiscard]] [[gnu::always_inline]] static Q<30> tap_value(const Tap& t, Q<31> nearer, Q<31> farther) {
		return add_wrap(mul_r(nearer, t.strength1), mul_r(farther, t.strength2));
	}
	/// @return The tap's delay at LFO @p lfo: the centre plus the LFO times the swing, 16.16 frames.
	[[nodiscard]] [[gnu::always_inline]] static int32_t tap_delay(const Coefficients& c, Q<31> lfo) {
		// Raw: the tap reads the 16.16 delay's integer part and fraction as integers
		return add_wrap(mul(lfo, c.delay_depth), c.delay_offset).raw();
	}

	/// @brief One ring-mode frame: LFOs, taps, the ring write, the mix.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample ring_step(const Coefficients& c, State& s, StereoSample x,
	                                                                   Noise& noise) {
		constexpr bool kWarble = M == Mode::WARBLE || M == Mode::WARBLE_MONO;
		constexpr bool kOwnRightTap = M == Mode::CHORUS || M == Mode::FLANGER || M == Mode::DIMENSION || kWarble;
		const Q<31> lfo1 = lfo<wave_of(M)>(s.phase, s.warbler, c.rate, noise);
		const Q<31> lfo2 = [&] [[gnu::always_inline]] {
			if constexpr (M == Mode::WARBLE) {
				return lfo<Wave::WARBLER>(s.phase2, s.warbler2, c.rate2, noise);
			}
			else {
				if constexpr (M == Mode::WARBLE_MONO) {
					// The second warbler steps in mono too, so the noise draws match stereo's.
					[[maybe_unused]] const Q<31> second_warbler =
					    lfo<Wave::WARBLER>(s.phase2, s.warbler2, c.rate2, noise);
				}
				return neg_wrap(lfo1);
			}
		}();
		StereoSample* const ring = s.ring;
		Tap t = tap_at(s, tap_delay(c, lfo1));
		const Q<30> tap_l =
		    tap_value(t, Q<31>::from_raw(ring[t.pos & kRingMask].l), Q<31>::from_raw(ring[(t.pos - 1) & kRingMask].l));
		if constexpr (kOwnRightTap) {
			t = tap_at(s, tap_delay(c, lfo2));
		}
		const Q<30> tap_r =
		    tap_value(t, Q<31>::from_raw(ring[t.pos & kRingMask].r), Q<31>::from_raw(ring[(t.pos - 1) & kRingMask].r));

		const Q<31> feedback = c.feedback;
		const auto x_l = Q<31>::from_raw(x.l);
		const auto x_r = Q<31>::from_raw(x.r);
		Q<31> out_l{};
		Q<31> out_r{};
		StereoSample& slot = ring[s.write];
		if constexpr (M == Mode::FLANGER || M == Mode::FLANGER_MONO) {
			// The tap times the feedback, Q29, lifted to Q31 and written back over the input.
			out_l = mul_r_as_wrap<Q<31>>(tap_l, feedback);
			slot.l = add_wrap(out_l, x_l).raw();
			out_r = mul_r_as_wrap<Q<31>>(tap_r, feedback);
			slot.r = add_wrap(out_r, x_r).raw();
		}
		else if constexpr (kWarble) {
			// The Q29 product's bits are written back as Q31: a quarter of the flanger's feedback.
			slot.l = add_wrap(reinterpret_bits<Q<31>>(mul_r(tap_l, feedback)), x_l).raw();
			slot.r = add_wrap(reinterpret_bits<Q<31>>(mul_r(tap_r, feedback)), x_r).raw();
			out_l = as_wrap<Q<31>>(tap_l);
			out_r = as_wrap<Q<31>>(tap_r);
		}
		else {
			out_l = as_wrap<Q<31>>(tap_l);
			slot.l = x.l;
			out_r = as_wrap<Q<31>>(tap_r);
			slot.r = x.r;
		}
		s.write = (s.write + 1) & kRingMask;
		if constexpr (M == Mode::DIMENSION || kWarble) {
			// The wet signal alone, doubled, wrapping.
			return {.l = scale_shl_wrap<1>(out_l).raw(), .r = scale_shl_wrap<1>(out_r).raw()};
		}
		else {
			return {.l = add_wrap(x_l, out_l).raw(), .r = add_wrap(x_r, out_r).raw()};
		}
	}

	/// @brief One phaser frame.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample phaser_step(const Coefficients& c, State& s,
	                                                                     StereoSample x, Noise& noise) {
		const Q<31> lfo1 = lfo<Wave::SINE>(s.phase, s.warbler, c.rate, noise);
		// The allpass coefficient, Q30: one less the LFO offset to [0, 1) times the depth.
		// Raw: the LFO offset to [0, 1) by an unsigned add and a halving.
		const auto lfo_unit =
		    Q<31>::from_raw(static_cast<int32_t>((static_cast<uint32_t>(lfo1.raw()) + 2147483648U) >> 1));
		const Q<30> a1 = sub_wrap(Q<30>::one(), mul_r(lfo_unit, c.depth));
		const Q<30> minus_a1 = neg_wrap(a1);
		const Q<31> feedback = c.feedback;
		Q<31> phaser_l = add_wrap(Q<31>::from_raw(x.l), mul_r_as_wrap<Q<31>>(Q<31>::from_raw(s.phaser.l), feedback));
		Q<31> phaser_r = add_wrap(Q<31>::from_raw(x.r), mul_r_as_wrap<Q<31>>(Q<31>::from_raw(s.phaser.r), feedback));
		// Each stage's products, Q31 × Q30, are Q29, lifted to Q31.
		for (StereoSample& stage : s.allpass) {
			const Q<31> in_l = phaser_l;
			const Q<31> in_r = phaser_r;
			phaser_l = add_wrap(mul_r_as_wrap<Q<31>>(phaser_l, minus_a1), Q<31>::from_raw(stage.l));
			stage.l = add_wrap(mul_r_as_wrap<Q<31>>(phaser_l, a1), in_l).raw();
			phaser_r = add_wrap(mul_r_as_wrap<Q<31>>(phaser_r, minus_a1), Q<31>::from_raw(stage.r));
			stage.r = add_wrap(mul_r_as_wrap<Q<31>>(phaser_r, a1), in_r).raw();
		}
		s.phaser = {.l = phaser_l.raw(), .r = phaser_r.raw()};
		return {.l = add_wrap(Q<31>::from_raw(x.l), phaser_l).raw(),
		        .r = add_wrap(Q<31>::from_raw(x.r), phaser_r).raw()};
	}
};

} // namespace deluge::dsp::fixed
