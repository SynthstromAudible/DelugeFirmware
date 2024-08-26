// Copyright 2023 Katherine Whitlock
// Copyright 2014 Emilie Gillet
// Reverb.

#pragma once
#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/util.hpp"
#include "fx_engine.hpp"
#include <array>
#include <limits>

namespace deluge::dsp::reverb {

class Mutable : public Base {
	constexpr static size_t kBufferSize = 32768;

public:
	Mutable() = default;

	~Mutable() override = default;

	void process(std::span<int32_t> in, std::span<StereoSample> output) override {
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
		                                &ap1,
		                                &ap2,
		                                &ap3,
		                                &ap4, //<
		                                &dap1a,
		                                &dap1b,
		                                &del1, //<
		                                &dap2a,
		                                &dap2b,
		                                &del2, //<
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
			wet = wet - dsp::OnePole(hp_r_, wet, hp_cutoff_);
			;
			wet = dsp::OnePole(lp_r_, wet, lp_cutoff_);

			auto output_right =
			    static_cast<int32_t>(wet * static_cast<float>(std::numeric_limits<uint32_t>::max()) * 0xF);

			c.Set(apout);
			del1.Interpolate(c, 4460.0f, LFO_1, 40.0f, krt);
			c.Lp(lp_2, klp);
			dap2a.Process(c, -kap);
			dap2b.Process(c, kap);
			del2.Write(c, 2.0f);
			wet = c.Get();
			wet = wet - dsp::OnePole(hp_l_, wet, hp_cutoff_);
			;
			wet = dsp::OnePole(lp_l_, wet, lp_cutoff_);
			;

			auto output_left =
			    static_cast<int32_t>(wet * static_cast<float>(std::numeric_limits<uint32_t>::max()) * 0xF);

			// Mix
			s.l += multiply_32x32_rshift32_rounded(output_left, getPanLeft());
			s.r += multiply_32x32_rshift32_rounded(output_right, getPanRight());
		}

		lp_decay_1_ = lp_1;
		lp_decay_2_ = lp_2;
	}

	inline void Clear() { engine_.Clear(); }

	static constexpr float kReverbTimeMin = 0.01f;
	static constexpr float kReverbTimeMax = 0.98f;
	static constexpr float kWidthMin = 0.1f;
	static constexpr float kWidthMax = 0.9f;

	// Reverb Base Overrides
	void setRoomSize(float value) override {
		reverb_time_ = util::map(value, 0.f, 1.f, kReverbTimeMin, kReverbTimeMax);
	}
	[[nodiscard]] float getRoomSize() const override {
		return util::map(reverb_time_, kReverbTimeMin, kReverbTimeMax, 0.f, 1.f);
	};

	void setDamping(float value) override {
		lp_val_ = value;
		lp_ = (value == 0.f) ? 1.f : 1.f - std::clamp((std::log2(((1.f - lp_val_) * 50.f) + 1.f) / 5.7f), 0.f, 1.f);
	}
	[[nodiscard]] float getDamping() const override { return lp_val_; }

	void setWidth(float value) override { diffusion_ = util::map(value, 0.f, 1.f, kWidthMin, kWidthMax); }
	[[nodiscard]] float getWidth() const override { return util::map(diffusion_, kWidthMin, kWidthMax, 0.f, 1.f); };

	void setHPF(float f) {
		hp_cutoff_val_ = f;
		hp_cutoff_ = calcFilterCutoff<FilterType::HighPass>(f);
	}

	[[nodiscard]] float getHPF() const { return hp_cutoff_val_; }

	void setLPF(float f) {
		lp_cutoff_val_ = f;
		lp_cutoff_ = calcFilterCutoff<FilterType::LowPass>(f);
	}

	[[nodiscard]] float getLPF() const { return lp_cutoff_val_; }

protected:
	static constexpr float sample_rate = kSampleRate;

	std::array<float, kBufferSize> buffer_{};
	FxEngine engine_{buffer_, {0.5f / sample_rate, 0.3f / sample_rate}};

	float input_gain_ = 0.2;

	// size
	float reverb_time_ = 0.665f;

	// width
	float diffusion_ = 0.625f;

	// damping
	float lp_{0.7f};
	float lp_val_{0.7f};

	// These are the state variables for the low-pass filters
	float lp_decay_1_{0};
	float lp_decay_2_{0};

	// High-pass
	float hp_cutoff_val_{0.f};
	// corresponds to 20Hz
	float hp_cutoff_{calcFilterCutoff<FilterType::HighPass>(0)};
	float hp_l_{0.0}; // HP state variable
	float hp_r_{0.0}; // HP state variable

	// Low-pass
	float lp_cutoff_val_{0.f};
	// corresponds to 0Hz
	float lp_cutoff_{calcFilterCutoff<FilterType::LowPass>(0)};
	float lp_l_{0.0}; // LP state variable
	float lp_r_{0.0}; // LP state variable
};

} // namespace deluge::dsp::reverb
