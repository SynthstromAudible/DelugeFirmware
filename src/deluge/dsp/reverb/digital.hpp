/*
 * Copyright © 2024 Katherine Whitlock
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
#include "mutable.hpp"

namespace deluge::dsp::reverb {

/// @brief The Griesinger topology model from Part 1 of Effect Design by John Dattorro,
/// classically based on the famous Lexicon 224 Digital Reverb
class Digital : public Mutable {
	constexpr static float kRatio = 29761.f / kSampleRate; // Lexicon sample rate to Deluge sample rate

	/// Dattorro's +/-16 samples of tank modulation, rescaled to our sample rate.
	constexpr static size_t max_excursion = 16.f * kRatio;

	/// An interpolated read touches both `offset` and `offset + 1`, so a modulated line has to
	/// reserve one slot beyond the furthest its LFO can push it. Without this the read spills
	/// into the next line's memory and drags a sample from a whole buffer ago into the tank.
	constexpr static size_t kInterpolationGuard = 1;

	/// Dattorro's decay range. The tank's allpasses are unity gain, so the round trip loses
	/// only decay^4 -- pushing this to the old shared [0.01, 0.98] leaves the top of the knob
	/// ringing for the better part of half a minute.
	constexpr static float kDecayMin = 0.1f;
	constexpr static float kDecayMax = 0.9f;

public:
	void process(std::span<q31_t> in, std::span<StereoSample> output) override {
		typename FxEngine::Context c;

		typename FxEngine::AllPass ap1(142 * kRatio);
		typename FxEngine::AllPass ap2(107 * kRatio);
		typename FxEngine::AllPass ap3(379 * kRatio);
		typename FxEngine::AllPass ap4(277 * kRatio);

		typename FxEngine::AllPass dap1a((672 * kRatio) + max_excursion + kInterpolationGuard);
		typename FxEngine::DelayLine del1a(4453 * kRatio);
		typename FxEngine::AllPass dap1b(1800 * kRatio);
		typename FxEngine::DelayLine del1b(3720 * kRatio);

		typename FxEngine::AllPass dap2a((908 * kRatio) + max_excursion + kInterpolationGuard);
		typename FxEngine::DelayLine del2a(4217 * kRatio);
		typename FxEngine::AllPass dap2b(2656 * kRatio);
		typename FxEngine::DelayLine del2b(3163 * kRatio);

		FxEngine::ConstructTopology(engine_, {&ap1, &ap2, &ap3, &ap4,         //<
		                                      &dap1a, &del1a, &dap1b, &del1b, //<
		                                      &dap2a, &del2a, &dap2b, &del2b});

		const float kdecay = reverb_time_;                          // 0.5f
		const float kid1 = 0.750f;                                  // input diffusion 1
		const float kid2 = 0.625f;                                  // input diffusion 2
		const float kdd1 = 0.70f;                                   // decay diffusion 1
		const float kdd2 = std::clamp(kdecay + 0.15f, 0.25f, 0.5f); // decay diffusion 2

		const float kdamp = lp_; // 1.f - 0.0005f;            // damping
		const float kbandwidth = 0.9995f;

		float lp_1 = lp_decay_1_;
		float lp_2 = lp_decay_2_;
		float lp_band = lp_band_;

		for (size_t frame = 0; frame < in.size(); ++frame) {
			engine_.Advance();

			const float input_sample = in[frame] / static_cast<float>(std::numeric_limits<int32_t>::max());
			c.Set(input_sample);

			c.Lp(lp_band, kbandwidth);

			// Diffuse through 4 allpasses.
			ap1.Process(c, kid1);
			ap2.Process(c, kid1);
			ap3.Process(c, kid2);
			ap4.Process(c, kid2);
			float apout = c.Get();

			// Main reverb loop: a figure of eight, where each branch's tail feeds the other's
			// head. del2b's output is already a delayed read, so branch one can close the loop
			// on it here and branch two simply carries on from branch one's accumulator.
			c.Set(apout);
			c.Add(kdecay * del2b.Read(del2b.length));
			dap1a.ProcessInterpolate(c, 672.0f * kRatio, LFO_2, max_excursion, -kdd1);
			del1a.Process(c);
			c.Lp(lp_1, kdamp); // damping
			c.Multiply(kdecay);
			dap1b.Process(c, kdd2);
			del1b.Process(c);

			c.Multiply(kdecay);
			c.Add(apout);
			dap2a.ProcessInterpolate(c, 908.0f * kRatio, LFO_1, max_excursion, -kdd1);
			del2a.Process(c);
			c.Lp(lp_2, kdamp); // damping
			c.Multiply(kdecay);
			dap2b.Process(c, kdd2);
			del2b.Process(c);

			float left_sum = 0;
			left_sum += 0.6f * del2a.at(266 * kRatio);
			left_sum += 0.6f * del2a.at(2974 * kRatio);
			left_sum -= 0.6f * dap2b.at(1913 * kRatio);
			left_sum += 0.6f * del2b.at(1996 * kRatio);
			left_sum -= 0.6f * del1a.at(1990 * kRatio);
			left_sum -= 0.6f * dap1b.at(187 * kRatio);
			left_sum -= 0.6f * del1b.at(1066 * kRatio);
			left_sum = left_sum - dsp::OnePole(hp_l_, left_sum, hp_cutoff_);
			left_sum = dsp::OnePole(lp_l_, left_sum, lp_cutoff_);

			float right_sum = 0;
			right_sum += 0.6f * del1a.at(353 * kRatio);
			right_sum += 0.6f * del1a.at(3627 * kRatio);
			right_sum -= 0.6f * dap1b.at(1228 * kRatio);
			right_sum += 0.6f * del1b.at(2673 * kRatio);
			right_sum -= 0.6f * del2a.at(2111 * kRatio);
			right_sum -= 0.6f * dap2b.at(335 * kRatio);
			right_sum -= 0.6f * del2b.at(121 * kRatio);
			right_sum = right_sum - dsp::OnePole(hp_r_, right_sum, hp_cutoff_);
			right_sum = dsp::OnePole(lp_r_, right_sum, lp_cutoff_);

			q31_t output_left = dsp::toQ31Saturating(left_sum * kOutputGain);
			q31_t output_right = dsp::toQ31Saturating(right_sum * kOutputGain);

			// Mix
			output[frame].l += multiply_32x32_rshift32_rounded(output_left, getPanLeft());
			output[frame].r += multiply_32x32_rshift32_rounded(output_right, getPanRight());
		}

		lp_decay_1_ = lp_1;
		lp_decay_2_ = lp_2;
		lp_band_ = lp_band;
	}

	// Reverb Base Overrides
	void setRoomSize(float value) override { reverb_time_ = util::map(value, 0.f, 1.f, kDecayMin, kDecayMax); }
	[[nodiscard]] float getRoomSize() const override {
		return util::map(reverb_time_, kDecayMin, kDecayMax, 0.f, 1.f);
	};

private:
	float lp_band_{0.f};
};
} // namespace deluge::dsp::reverb
