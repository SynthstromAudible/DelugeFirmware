// The Mutable loop after Emilie Gillet's Clouds reverb (Copyright 2014 Emilie Gillet, MIT License); the Digital loop
// after Jon Dattorro, "Effect Design, Part 1: Reverberator and Other Filters" (JAES, 1997).

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
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/float/reverb/plate_lfo.h"
#include "util/fixedpoint.h"
#include "util/misc.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <type_traits>

/// @file
/// @brief The plate reverb: the Mutable and Digital loops as one moded processor kernel of stereo frames.
namespace deluge::dsp::fp {

/// @brief The plate reverb as one moded processor kernel of stereo frames, rendered in place over the master block
/// as `Mono<Plate>`: MUTABLE is the Griesinger loop (four input allpasses, then two branches of two allpasses and a
/// modulated delay, each feeding the other), DIGITAL is Dattorro's figure-of-eight plate.
/// @details Its arithmetic is float; its interface is q31 like every effect: a q31 send in by cursor, q31 frames in
/// and out. The lines live in a 32768-float ring the owner hands in (@ref set_memory), addressed from a write index
/// that steps down one each frame, every line at a fixed base from it (each mode's table). Both modes share one State.
struct Plate {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	/// @brief The topology each loop runs.
	enum class Mode : uint8_t {
		MUTABLE, ///< The Griesinger loop.
		DIGITAL, ///< Dattorro's plate.
	};
	using Modes = ModeList<Mode::MUTABLE, Mode::DIGITAL>;

	static constexpr std::size_t kMemoryFloats = 32768;  ///< The ring.
	static constexpr uint32_t kMask = kMemoryFloats - 1; ///< Wraps a ring index.
	/// Lifts the normalised loop signal back to q31: 15 × 2³², the gain the plates are balanced around.
	static constexpr float kOutputGain = 15.f * 4294967296.f;
	/// q31 to the loop's float: the send over (float)INT32_MAX, which rounds to 2³¹.
	static constexpr float kQ31Max = static_cast<float>(std::numeric_limits<int32_t>::max());
	static constexpr float kReverbTimeMin = 0.01f; ///< MUTABLE's room knob mapping.
	static constexpr float kReverbTimeMax = 0.98f;
	static constexpr float kWidthMin = 0.1f; ///< The width knob to the diffusion.
	static constexpr float kWidthMax = 0.9f;
	/// The damping filter's one-pole coefficient: kLpMax is transparent, kLpMin a cutoff of about 70 Hz.
	static constexpr float kLpMin = 0.01f;
	static constexpr float kLpMax = 1.f;
	/// MUTABLE's modulated taps: branch one reads del2 at kDel2Tap + kDel2Excursion × LFO 2, branch two reads del1 at
	/// kDel1Tap + kDel1Excursion × LFO 1 (frames).
	static constexpr float kDel2Tap = 6261.f;
	static constexpr float kDel2Excursion = 50.f;
	static constexpr float kDel1Tap = 4460.f;
	static constexpr float kDel1Excursion = 40.f;
	/// Each branch's output is the loop signal at twice the level it is written into its delay.
	static constexpr float kLoopGain = 2.f;

	/// @brief One line in the ring: its base from the write index, and its length (length + 1 slots are its own).
	struct Line {
		uint32_t base;
		uint32_t length;
	};
	/// @brief MUTABLE's lines, laid out in this order, each length + 1 slots after the last.
	struct MutableLines {
		Line ap1, ap2, ap3, ap4, dap1a, dap1b, del1, dap2a, dap2b, del2;
	};
	static constexpr MutableLines kMutable = [] consteval {
		uint32_t base = 0;
		const auto line = [&base](uint32_t length) {
			const Line l{base, length};
			base += length + 1;
			return l;
		};
		return MutableLines{.ap1 = line(150),
		                    .ap2 = line(214),
		                    .ap3 = line(319),
		                    .ap4 = line(527),
		                    .dap1a = line(2182),
		                    .dap1b = line(2690),
		                    .del1 = line(4501),
		                    .dap2a = line(2525),
		                    .dap2b = line(2197),
		                    .del2 = line(6312)};
	}();
	/// The LFO lanes' reach. Nominally [0, 1]; the recurrence's float rounding carries each lane up to 6.8e-4 past
	/// either end (it settles there within 40 million frames and stays, measured to 400 million), so the read bounds
	/// allow 0.001.
	static constexpr float kLfoMin = -0.001f;
	static constexpr float kLfoMax = 1.001f;
	// Every read stays inside its line: the modulated taps read offset and offset + 1, the LFO within its reach.
	static_assert(static_cast<uint32_t>(kDel2Tap + (kDel2Excursion * kLfoMax)) + 1 <= kMutable.del2.length);
	static_assert(static_cast<uint32_t>(kDel1Tap + (kDel1Excursion * kLfoMax)) + 1 <= kMutable.del1.length);
	static_assert(kDel1Tap + (kDel1Excursion * kLfoMin) >= 0.f && kDel2Tap + (kDel2Excursion * kLfoMin) >= 0.f);
	static_assert(kMutable.del2.base + kMutable.del2.length < kMemoryFloats);

	/// Lexicon's 29,761 Hz rate to ours: Dattorro's lengths and taps scale by it.
	static constexpr float kRatio = 29761.f / kSampleRate;
	/// Dattorro's ±16 frames of tank modulation at our rate. Named quirk: truncated to a whole number of frames (10).
	static constexpr std::size_t kMaxExcursion = 16.f * kRatio;
	/// A modulated read touches offset + 1, so a modulated line keeps one slot beyond its furthest reach.
	static constexpr std::size_t kInterpolationGuard = 1;
	static constexpr float kDecayMin = 0.1f; ///< DIGITAL's room knob mapping: Dattorro's decay range.
	static constexpr float kDecayMax = 0.9f;
	static constexpr float kInputDiffusion1 = 0.750f;
	static constexpr float kInputDiffusion2 = 0.625f;
	static constexpr float kDecayDiffusion1 = 0.70f;
	static constexpr float kBandwidth = 0.9995f;
	static constexpr float kTapGain = 0.6f;
	/// DIGITAL's second decay diffusion: reverb_time + kDecayDiffusion2Offset, within kDecayDiffusion2Min to Max.
	static constexpr float kDecayDiffusion2Offset = 0.15f;
	static constexpr float kDecayDiffusion2Min = 0.25f;
	static constexpr float kDecayDiffusion2Max = 0.5f;
	/// The damping knob's log curve: log2(damping × kDampingCurveScale + 1) / kDampingCurveDivisor, clamped to 0 to 1.
	static constexpr float kDampingCurveScale = 50.f;
	static constexpr float kDampingCurveDivisor = 5.7f;
	/// The output filters' knob curves: min + (e^(kFilterCurve × knob) − 1) × span, in Hz.
	static constexpr float kFilterCurve = 1.5f;
	static constexpr float kLowPassMinHz = 0.f;
	static constexpr float kLowPassSpanHz = 5083.74f;
	static constexpr float kHighPassMinHz = 20.f;
	static constexpr float kHighPassSpanHz = 150.f;

	/// @brief DIGITAL's lines, laid out in this order. Named quirk: each length is float × kRatio truncated to whole
	/// frames.
	struct DigitalLines {
		Line ap1, ap2, ap3, ap4, dap1a, del1a, dap1b, del1b, dap2a, del2a, dap2b, del2b;
	};
	static constexpr DigitalLines kDigital = [] consteval {
		uint32_t base = 0;
		const auto line = [&base](std::size_t length) {
			const Line l{base, static_cast<uint32_t>(length)};
			base += static_cast<uint32_t>(length) + 1;
			return l;
		};
		return DigitalLines{
		    .ap1 = line(142 * kRatio),
		    .ap2 = line(107 * kRatio),
		    .ap3 = line(379 * kRatio),
		    .ap4 = line(277 * kRatio),
		    .dap1a = line((672 * kRatio) + kMaxExcursion + kInterpolationGuard),
		    .del1a = line(4453 * kRatio),
		    .dap1b = line(1800 * kRatio),
		    .del1b = line(3720 * kRatio),
		    .dap2a = line((908 * kRatio) + kMaxExcursion + kInterpolationGuard),
		    .del2a = line(4217 * kRatio),
		    .dap2b = line(2656 * kRatio),
		    .del2b = line(3163 * kRatio),
		};
	}();
	/// @return An output tap's offset. Named quirk: int × kRatio truncated to int32.
	/// consteval, so each tap is a constant at its use: a constexpr function is a call per frame under
	/// -fno-inline-functions.
	[[nodiscard]] static consteval int32_t tap(int32_t frames) { return static_cast<int32_t>(frames * kRatio); }
	// Each modulated read at its furthest, plus the slot after, stays inside its line (the output taps: below).
	static_assert(static_cast<std::size_t>(672.0f * kRatio) + kMaxExcursion + 1 <= kDigital.dap1a.length);
	static_assert(static_cast<std::size_t>(908.0f * kRatio) + kMaxExcursion + 1 <= kDigital.dap2a.length);
	static_assert(kDigital.del2b.base + kDigital.del2b.length < kMemoryFloats);

	/// @brief What the owner sets: the mode and the knobs, each 0 to 1.
	struct Settings {
		Mode mode = Mode::MUTABLE;
		float room_size = 0.f;
		float damping = 0.f;
		float width = 0.f;
		float hpf = 0.f;
		float lpf = 0.f;
		bool operator==(const Settings&) const = default;
	};
	/// @brief What the loops read, each mapped from its knob in float.
	struct Coefficients {
		float reverb_time = 0.f;       ///< The loop's decay: the room knob, mapped per mode.
		float diffusion = 0.f;         ///< MUTABLE's allpass coefficient: the width knob.
		float lp = 0.f;                ///< The damping one-pole's coefficient.
		float hp_cutoff = 0.f;         ///< The output high-pass's one-pole coefficient.
		float lp_cutoff = 0.f;         ///< The output low-pass's one-pole coefficient.
		float decay_diffusion_2 = 0.f; ///< DIGITAL's second decay diffusion (kDecayDiffusion2Offset and its bounds).
		PlateLfo::Coefficients lfo{};
		Mode mode = Mode::MUTABLE;
	};
	/// @brief The ring, this block's send cursor and return levels, the write index, the LFO, and the loop and output
	/// filter memories. Both modes share it.
	struct State {
		float* memory = nullptr;   ///< kMemoryFloats floats.
		const q31* send = nullptr; ///< The next send sample; set by set_send, advanced per frame.
		q31 pan_l = 0;             ///< The left return level, Q31.
		q31 pan_r = 0;             ///< The right return level, Q31.
		int32_t write = 0;         ///< The ring index the lines are read from; steps down one each frame.
		PlateLfo::State lfo{};
		float lp_decay_1 = 0.f; ///< Branch one's damping memory.
		float lp_decay_2 = 0.f; ///< Branch two's damping memory.
		float lp_band = 0.f;    ///< DIGITAL's input bandwidth memory.
		float hp_l = 0.f;       ///< The output high-passes' memories.
		float hp_r = 0.f;
		float lp_l = 0.f; ///< The output low-passes' memories.
		float lp_r = 0.f;
	};

	/// @return The coefficients for @p s: each knob's mapping, in float.
	[[nodiscard]] static Coefficients coefficients(const Settings& s) {
		Coefficients c{};
		c.mode = s.mode;
		c.reverb_time = s.mode == Mode::DIGITAL ? util::map(s.room_size, 0.f, 1.f, kDecayMin, kDecayMax)
		                                        : util::map(s.room_size, 0.f, 1.f, kReverbTimeMin, kReverbTimeMax);
		c.diffusion = util::map(s.width, 0.f, 1.f, kWidthMin, kWidthMax);
		// 0 is no damping, 1 as dark as the loop filter goes, on a log curve so the change spreads over the knob.
		const float damping =
		    std::clamp(std::log2((s.damping * kDampingCurveScale) + 1.f) / kDampingCurveDivisor, 0.f, 1.f);
		c.lp = util::map(1.f - damping, 0.f, 1.f, kLpMin, kLpMax);
		c.hp_cutoff = filter_cutoff<Filter::HIGH_PASS>(s.hpf);
		c.lp_cutoff = filter_cutoff<Filter::LOW_PASS>(s.lpf);
		c.decay_diffusion_2 =
		    std::clamp(c.reverb_time + kDecayDiffusion2Offset, kDecayDiffusion2Min, kDecayDiffusion2Max);
		c.lfo = PlateLfo::coefficients({});
		return c;
	}

	/// @brief One frame at mode @p M: the ring index down one (stepping the LFO every kStride frames), the send's next
	/// sample into the loop, and the loop's two outputs added to @p x at the return levels.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& c, const EmptyBlock& /*b*/,
	                                                              State& s, StereoSample x) {
		step_write(c, s);
		const float input = static_cast<float>(*s.send) / kQ31Max;
		++s.send;
		if constexpr (M == Mode::MUTABLE) {
			return mutable_frame(c, s, x, input);
		}
		else {
			return digital_frame(c, s, x, input);
		}
	}

	static void set_memory(State& s, float* memory) { s.memory = memory; }
	static void set_send(State& s, const q31* send, q31 pan_l, q31 pan_r) {
		s.send = send;
		s.pan_l = pan_l;
		s.pan_r = pan_r;
	}
	/// @brief Event: the LFO at its start; the owner sends it after a reset, since its start depends on the
	/// coefficients.
	static void start(const Coefficients& c, State& s) { PlateLfo::start(c.lfo, s.lfo); }

private:
	enum class Filter : uint8_t { LOW_PASS, HIGH_PASS };
	/// @return A knob position's one-pole coefficient: a cutoff on an exponential curve, from 20 to about 540 Hz
	/// (high-pass) or from 0 to about 17.7 kHz (low-pass), as the bilinear one-pole's wc = fc / (1 + fc).
	template <Filter F>
	[[nodiscard]] static float filter_cutoff(float f) {
		float min_hz = kLowPassMinHz;
		float span_hz = kLowPassSpanHz;
		if constexpr (F == Filter::HIGH_PASS) {
			min_hz = kHighPassMinHz;
			span_hz = kHighPassSpanHz;
		}
		const float fc_hz = min_hz + (std::exp(kFilterCurve * f) - 1) * span_hz;
		const float fc = fc_hz / float(kSampleRate);
		return fc / (1 + fc);
	}

	/// @brief The write index down one, wrapping, then the LFO's step when the index is a multiple of kStride.
	/// Named quirk: the LFO's phase is tied to the write index, which runs down from the ring's top.
	[[gnu::always_inline]] static void step_write(const Coefficients& c, State& s) {
		--s.write;
		if (s.write < 0) {
			s.write += static_cast<int32_t>(kMemoryFloats);
		}
		if ((s.write & static_cast<int32_t>(PlateLfo::kStride - 1)) == 0) {
			std::ignore = kernel_ops::advance_at<PlateLfo, kernel_ops::NoMode::NONE>(c.lfo, s.lfo, PlateLfo::kStride);
		}
	}
	/// @return The ring slot @p index past @p line's base from the write index @p w.
	[[nodiscard]] [[gnu::always_inline]] static float& at(float* m, int32_t w, Line line, int32_t index) {
		return m[(static_cast<uint32_t>(w) + line.base + static_cast<uint32_t>(index)) & kMask];
	}
	/// @return The line read between two slots at a fractional @p offset, linearly. Named quirk: the offset is not
	/// truncated to the line, so a modulated read moves smoothly.
	[[nodiscard]] [[gnu::always_inline]] static float interpolate(float* m, int32_t w, Line line, float offset) {
		const auto integral = static_cast<int32_t>(offset);
		const float fractional = offset - static_cast<float>(integral);
		const float a = at(m, w, line, integral);
		const float b = at(m, w, line, integral + 1);
		return a + (b - a) * fractional;
	}
	/// @return A Schroeder allpass over @p line: the head plus the scaled tail written in, the scaled-back head plus
	/// the tail out. Named quirk: an allpass's tail is the slot at length − 1, where a plain delay (@ref delay) and the
	/// Digital loop's close read the slot at length.
	[[nodiscard]] [[gnu::always_inline]] static float allpass(float* m, int32_t w, Line line, float head, float scale) {
		const float tail = at(m, w, line, static_cast<int32_t>(line.length) - 1);
		const float feedback = head + (tail * scale);
		at(m, w, line, 0) = feedback;
		return (feedback * -scale) + tail;
	}
	/// @brief The float one-pole the loop and output filters use: y += c × (x − y), returning y.
	[[nodiscard]] [[gnu::always_inline]] static float one_pole(float& y, float x, float coefficient) {
		y += coefficient * (x - y);
		return y;
	}
	/// @return @p scaled clamped into q31 (the top at the largest float below 2³¹) and converted: std::clamp's
	/// comparisons written out, since std::clamp is a call per sample under -fno-inline-functions.
	[[nodiscard]] [[gnu::always_inline]] static q31 to_q31_saturating(float scaled) {
		constexpr float kMax = 2147483520.f; // 0x7FFFFF80
		constexpr float kMin = -2147483648.f;
		const float clamped = scaled < kMin ? kMin : (kMax < scaled ? kMax : scaled);
		return static_cast<q31>(clamped);
	}
	/// @return @p x with the two outputs added at the return levels: the output stage, q31 saturating then the
	/// rounded multiply-accumulate.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample mix(const State& s, StereoSample x, float left,
	                                                             float right) {
		const q31 output_left = to_q31_saturating(left * kOutputGain);
		const q31 output_right = to_q31_saturating(right * kOutputGain);
		return {.l = wrapping_add(x.l, multiply_32x32_rshift32_rounded(output_left, s.pan_l)),
		        .r = wrapping_add(x.r, multiply_32x32_rshift32_rounded(output_right, s.pan_r))};
	}
	/// @return The modulated allpass: the tail read at offset + amplitude × lfo, scaled into the head, written, then
	/// the head scaled back plus the read out.
	[[nodiscard]] [[gnu::always_inline]] static float allpass_modulated(float* m, int32_t w, Line line, float acc,
	                                                                    float offset, float lfo, float amplitude,
	                                                                    float scale) {
		offset += amplitude * lfo;
		const float read = interpolate(m, w, line, offset);
		acc += read * scale;
		at(m, w, line, 0) = acc;
		acc *= -scale;
		acc += read;
		return acc;
	}
	/// @return A plain delay over @p line: the head written, the tail (length frames ago) out.
	[[nodiscard]] [[gnu::always_inline]] static float delay(float* m, int32_t w, Line line, float acc) {
		at(m, w, line, 0) = acc;
		return at(m, w, line, static_cast<int32_t>(line.length));
	}
	/// @brief One Digital frame: the input band-limited and diffused, then the figure of eight, then the output taps.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample digital_frame(const Coefficients& c, State& s,
	                                                                       StereoSample x, float input) {
		const float decay = c.reverb_time;
		const float decay_diffusion_2 = c.decay_diffusion_2;
		const float damp = c.lp;
		float* const m = s.memory;
		const int32_t w = s.write;
		float acc = input;
		acc = one_pole(s.lp_band, acc, kBandwidth);
		acc = allpass(m, w, kDigital.ap1, acc, kInputDiffusion1);
		acc = allpass(m, w, kDigital.ap2, acc, kInputDiffusion1);
		acc = allpass(m, w, kDigital.ap3, acc, kInputDiffusion2);
		acc = allpass(m, w, kDigital.ap4, acc, kInputDiffusion2);
		const float apout = acc;

		// The figure of eight: branch one closes the loop on del2b's tail; branch two carries on from it.
		acc = apout;
		acc += decay * at(m, w, kDigital.del2b, static_cast<int32_t>(kDigital.del2b.length));
		acc = allpass_modulated(m, w, kDigital.dap1a, acc, 672.0f * kRatio, PlateLfo::value(s.lfo)[1],
		                        static_cast<float>(kMaxExcursion), -kDecayDiffusion1);
		acc = delay(m, w, kDigital.del1a, acc);
		acc = one_pole(s.lp_decay_1, acc, damp);
		acc *= decay;
		acc = allpass(m, w, kDigital.dap1b, acc, decay_diffusion_2);
		acc = delay(m, w, kDigital.del1b, acc);

		acc *= decay;
		acc += apout;
		acc = allpass_modulated(m, w, kDigital.dap2a, acc, 908.0f * kRatio, PlateLfo::value(s.lfo)[0],
		                        static_cast<float>(kMaxExcursion), -kDecayDiffusion1);
		acc = delay(m, w, kDigital.del2a, acc);
		acc = one_pole(s.lp_decay_2, acc, damp);
		acc *= decay;
		acc = allpass(m, w, kDigital.dap2b, acc, decay_diffusion_2);
		acc = delay(m, w, kDigital.del2b, acc);

		float left = 0;
		left += kTapGain * at(m, w, kDigital.del2a, tap(266));
		left += kTapGain * at(m, w, kDigital.del2a, tap(2974));
		left -= kTapGain * at(m, w, kDigital.dap2b, tap(1913));
		left += kTapGain * at(m, w, kDigital.del2b, tap(1996));
		left -= kTapGain * at(m, w, kDigital.del1a, tap(1990));
		left -= kTapGain * at(m, w, kDigital.dap1b, tap(187));
		left -= kTapGain * at(m, w, kDigital.del1b, tap(1066));
		left = left - one_pole(s.hp_l, left, c.hp_cutoff);
		left = one_pole(s.lp_l, left, c.lp_cutoff);

		float right = 0;
		right += kTapGain * at(m, w, kDigital.del1a, tap(353));
		right += kTapGain * at(m, w, kDigital.del1a, tap(3627));
		right -= kTapGain * at(m, w, kDigital.dap1b, tap(1228));
		right += kTapGain * at(m, w, kDigital.del1b, tap(2673));
		right -= kTapGain * at(m, w, kDigital.del2a, tap(2111));
		right -= kTapGain * at(m, w, kDigital.dap2b, tap(335));
		right -= kTapGain * at(m, w, kDigital.del2b, tap(121));
		right = right - one_pole(s.hp_r, right, c.hp_cutoff);
		right = one_pole(s.lp_r, right, c.lp_cutoff);
		return mix(s, x, left, right);
	}
	/// @brief One Mutable frame: four input allpasses, then the two branches, each feeding the other's delay.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample mutable_frame(const Coefficients& c, State& s,
	                                                                       StereoSample x, float input) {
		const float diffusion = c.diffusion;
		const float lp = c.lp;
		const float reverb_time = c.reverb_time;
		float* const m = s.memory;
		const int32_t w = s.write;
		float acc = input;
		acc = allpass(m, w, kMutable.ap1, acc, diffusion);
		acc = allpass(m, w, kMutable.ap2, acc, diffusion);
		acc = allpass(m, w, kMutable.ap3, acc, diffusion);
		acc = allpass(m, w, kMutable.ap4, acc, diffusion);
		const float apout = acc;

		// Branch one: del2's tail, modulated by LFO 2, damped, through dap1a and dap1b into del1 at twice the level.
		acc = apout;
		{
			float offset = kDel2Tap;
			offset += kDel2Excursion * PlateLfo::value(s.lfo)[1];
			acc += interpolate(m, w, kMutable.del2, offset) * reverb_time;
		}
		acc = one_pole(s.lp_decay_1, acc, lp);
		acc = allpass(m, w, kMutable.dap1a, acc, -diffusion);
		acc = allpass(m, w, kMutable.dap1b, acc, diffusion);
		at(m, w, kMutable.del1, 0) = acc;
		acc *= kLoopGain;
		float right = acc;
		right = right - one_pole(s.hp_r, right, c.hp_cutoff);
		right = one_pole(s.lp_r, right, c.lp_cutoff);

		// Branch two: del1's tail, modulated by LFO 1, through dap2a and dap2b into del2.
		acc = apout;
		{
			float offset = kDel1Tap;
			offset += kDel1Excursion * PlateLfo::value(s.lfo)[0];
			acc += interpolate(m, w, kMutable.del1, offset) * reverb_time;
		}
		acc = one_pole(s.lp_decay_2, acc, lp);
		acc = allpass(m, w, kMutable.dap2a, acc, -diffusion);
		acc = allpass(m, w, kMutable.dap2b, acc, diffusion);
		at(m, w, kMutable.del2, 0) = acc;
		acc *= kLoopGain;
		float left = acc;
		left = left - one_pole(s.hp_l, left, c.hp_cutoff);
		left = one_pole(s.lp_l, left, c.lp_cutoff);
		return mix(s, x, left, right);
	}
};

// Every Digital output tap reads inside its line.
static_assert(Plate::tap(266) <= static_cast<int32_t>(Plate::kDigital.del2a.length)
              && Plate::tap(2974) <= static_cast<int32_t>(Plate::kDigital.del2a.length));
static_assert(Plate::tap(2111) <= static_cast<int32_t>(Plate::kDigital.del2a.length)
              && Plate::tap(1913) <= static_cast<int32_t>(Plate::kDigital.dap2b.length));
static_assert(Plate::tap(335) <= static_cast<int32_t>(Plate::kDigital.dap2b.length)
              && Plate::tap(1996) <= static_cast<int32_t>(Plate::kDigital.del2b.length));
static_assert(Plate::tap(121) <= static_cast<int32_t>(Plate::kDigital.del2b.length)
              && Plate::tap(1990) <= static_cast<int32_t>(Plate::kDigital.del1a.length));
static_assert(Plate::tap(353) <= static_cast<int32_t>(Plate::kDigital.del1a.length)
              && Plate::tap(3627) <= static_cast<int32_t>(Plate::kDigital.del1a.length));
static_assert(Plate::tap(187) <= static_cast<int32_t>(Plate::kDigital.dap1b.length)
              && Plate::tap(1228) <= static_cast<int32_t>(Plate::kDigital.dap1b.length));
static_assert(Plate::tap(1066) <= static_cast<int32_t>(Plate::kDigital.del1b.length)
              && Plate::tap(2673) <= static_cast<int32_t>(Plate::kDigital.del1b.length));
static_assert(processor_kernel<Plate>);
static_assert(!std::is_polymorphic_v<Plate>);
} // namespace deluge::dsp::fp
