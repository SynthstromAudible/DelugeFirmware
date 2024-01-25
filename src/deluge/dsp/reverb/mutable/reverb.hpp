// Copyright 2023 Katherine Whitlock
// Copyright 2014 Emilie Gillet
// Reverb.

#pragma once
#include "dsp/reverb/base.hpp"
#include "fx_engine.hpp"
#include <array>
#include <limits>
#include <numeric>
#include <span>

namespace deluge::dsp::reverb {

class Mutable : public Base {
	constexpr static size_t kBufferSize = 32768;

public:
	Mutable() : reverb_time_(0.35f + 0.63f * 0.5f) {}

	~Mutable() override = default;

	void Process(std::span<int32_t> in, std::span<StereoSample> output) override {
		// This is the Griesinger topology described in the Dattorro paper
		// (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
		// Modulation is applied in the loop of the first diffuser AP for additional
		// smearing; and to the two long delays for a slow shimmer/chorus effect.
		typename FxEngine::AllPass ap1(150);
		typename FxEngine::AllPass ap2(214);
		typename FxEngine::AllPass ap3(319);
		typename FxEngine::AllPass ap4(527);

		typename FxEngine::AllPass dap1a(2182);
		typename FxEngine::AllPass dap1b(2690);
		typename FxEngine::AllPass del1(4501);

		typename FxEngine::AllPass dap2a(2525);
		typename FxEngine::AllPass dap2b(2197);
		typename FxEngine::AllPass del2(6312);

		typename FxEngine::Context c;
		FxEngine::ConstructTopology(engine_, //<
		                            {
		                                &ap1, &ap2, &ap3, &ap4, //<
		                                &dap1a, &dap1b, &del1,  //<
		                                &dap2a, &dap2b, &del2,  //<
		                            });

		const float kap = diffusion_;
		const float klp = lp_;
		const float krt = reverb_time_;
		const float gain = input_gain_;

		float lp_1 = lp_decay_1_;
		float lp_2 = lp_decay_2_;

		for (size_t frame = 0; frame < in.size(); frame++) {
			StereoSample& s = output[frame];
			float wet = 0;
			float apout = 0.0f;
			engine_.Advance();

			// Smear AP1 inside the loop.
			// c.Interpolate(ap1, 10.0f, LFO_1, 80.0f, 1.0f);
			// c.Write(ap1, 100, 0.0f);

			const float input_sample = in[frame] / static_cast<float>(std::numeric_limits<int32_t>::max());

			c.Set(input_sample // * gain
			);

			// Diffuse through 4 allpasses.
			ap1.Process(c, kap);
			ap2.Process(c, kap);
			ap3.Process(c, kap);
			ap4.Process(c, kap);
			apout = c.Get();

			// Main reverb loop.
			c.Set(apout);
			del2.Interpolate(c, 6261.0f, LFO_2, 50.0f, krt);
			c.Lp(lp_1, klp);
			dap1a.Process(c, -kap);
			dap1b.Process(c, kap);
			del1.Write(c, 2.0f);
			wet = c.Get();

			int32_t output_right =
			    static_cast<int32_t>(wet * static_cast<float>(std::numeric_limits<uint32_t>::max()) * 0xF);

			c.Set(apout);
			del1.Interpolate(c, 4460.0f, LFO_1, 40.0f, krt);
			c.Lp(lp_2, klp);
			dap2a.Process(c, -kap);
			dap2b.Process(c, kap);
			del2.Write(c, 2.0f);
			wet = c.Get();

			int32_t output_left =
			    static_cast<int32_t>(wet * static_cast<float>(std::numeric_limits<uint32_t>::max()) * 0xF);

			// Mix
			s.l += multiply_32x32_rshift32_rounded(output_left, getPanLeft());
			s.r += multiply_32x32_rshift32_rounded(output_right, getPanRight());
		}

		lp_decay_1_ = lp_1;
		lp_decay_2_ = lp_2;
	}

	inline void Clear() { engine_.Clear(); }

	// Reverb Base Overrides
	void setRoomSize(float value) override { reverb_time_ = 0.35f + 0.63f * value; }
	[[nodiscard]] float getRoomSize() const override { return (reverb_time_ - 0.35) / 0.63f; };

	void setDamping(float value) override { lp_ = 0.3f + value * 0.6f; }
	[[nodiscard]] float getDamping() const override { return (lp_ - 0.3f) / 0.6f; }

	void setWidth(float value) override { diffusion_ = 0.35 + 0.63f * value; }
	[[nodiscard]] float getWidth() const override { return (diffusion_ - 0.35) / 0.63f; };

private:
	static constexpr float sample_rate = 44100.f;

	std::array<float, kBufferSize> buffer_{};
	FxEngine engine_{buffer_, {0.5f / sample_rate, 0.3f / sample_rate}};

	float input_gain_{0.2};

	// size
	float reverb_time_;

	// width
	float diffusion_{0.625f};

	// dampening
	float lp_{0.7f};

	float lp_decay_1_{0};
	float lp_decay_2_{0};
};

} // namespace deluge::dsp::reverb
