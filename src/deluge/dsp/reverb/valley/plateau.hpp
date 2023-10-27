#pragma once
#include "Dattorro.hpp"
#include "dsp/reverb/base.hpp"

namespace deluge::dsp::reverb {

class Plateau : public Base {
public:
	struct Settings {
		float width = 0.f;
		bool input_diffusion;
		float input_lpf{0}, input_hpf{10};     // 0 to 10
		float tank_lpf{0}, tank_hpf{10};       // 0 to 10
		float mod_speed, mod_depth, mod_shape; // 0 to 1, 0 to 16, 0 to 1
		float damping = 0.9999;                // 0 to 1
		float pre_delay_time = 0.0;            // 0 to 0.5
	};

	Plateau(float initMaxLfoDepth = 16.f, float initMaxTimeScale = 1.f);
	~Plateau() override = default;

	void processOne(float leftInput, float rightInput);
	void clear();

	void setPreDelay(float time);

	void freeze(bool freezeFlag) { tank.freeze(freezeFlag); }

	void setInputFilterLowCutoffPitch(float pitch);
	void setInputFilterHighCutoffPitch(float pitch);
	void enableInputDiffusion(bool enable) {
		diffuseInput = enable ? 1.0 : 0.0;
		settings_.input_diffusion = enable;
	}

	void setTankFilterHighCutFrequency(float frequency);
	void setTankFilterLowCutFrequency(float frequency);

	void setTankModSpeed(float modSpeed) { tank.setModSpeed(settings_.mod_speed = modSpeed); }
	void setTankModDepth(float modDepth) { tank.setModDepth(settings_.mod_depth = modDepth); }
	void setTankModShape(float modShape) { tank.setModShape(settings_.mod_shape = modShape); }

	[[nodiscard]] float getLeftOutput() const { return leftOut; }
	[[nodiscard]] float getRightOutput() const { return rightOut; }

	[[gnu::always_inline]] void Process(std::span<int32_t> input, std::span<StereoSample> output) override {
		assert(input.size() == output.size());
		for (size_t i = 0; i < input.size(); ++i) {
			StereoSample& out_sample = output[i];
			const float input_sample = input[i] / static_cast<float>(std::numeric_limits<int32_t>::max());
			processOne(input_sample, input_sample);

			int32_t output_left =
			    static_cast<int32_t>(leftOut * static_cast<float>(std::numeric_limits<uint32_t>::max()) * 0xF);
			int32_t output_right =
			    static_cast<int32_t>(rightOut * static_cast<float>(std::numeric_limits<uint32_t>::max()) * 0xF);

			// Mix
			out_sample.l += multiply_32x32_rshift32_rounded(output_left, getPanLeft());
			out_sample.r += multiply_32x32_rshift32_rounded(output_right, getPanRight());
		}
	}

	// Room Size (time scale)
	void setRoomSize(float timeScale) override;
	[[nodiscard]] float getRoomSize() const override { return timeScale; }

	// Damping (decay)
	void setDamping(float newDecay) override;
	[[nodiscard]] float getDamping() const override { return settings_.damping; }

	// Width (tank diffusion)
	void setWidth(float diffusion) override;
	[[nodiscard]] float getWidth() const override { return settings_.width; };

	Settings settings() { return settings_; }

private:
	Settings settings_;

	static constexpr long kInApf1Time = 141;
	static constexpr long kInApf2Time = 107;
	static constexpr long kInApf3Time = 379;
	static constexpr long kInApf4Time = 277;

	static constexpr float dattorroSampleRate = 29761.0;
	static constexpr float sampleRate = 44100.0;
	static constexpr float dattorroScaleFactor = sampleRate / dattorroSampleRate;
	static constexpr long dattorroScale(float delayTime) { return static_cast<long>(delayTime * dattorroScaleFactor); }

	float leftSum = 0.0;
	float rightSum = 0.0;

	float rightOut = 0.0;
	float leftOut = 0.0;
	float inputLowCut = 1.0;
	float inputHighCut = 10000.0;
	float reverbHighCut = 10000.0;
	float reverbLowCut = 0.0;
	float modDepth = 1.0;
	float inputDiffusion1 = 0.75;
	float inputDiffusion2 = 0.625;

	float modSpeed = 1.0;
	float diffuseInput = 0.f;
	float timeScale = 0.5;

	OnePoleHPFilter leftInputDCBlock;
	OnePoleHPFilter rightInputDCBlock;
	OnePoleLPFilter inputLpf{22000.0};
	OnePoleHPFilter inputHpf{1.f};

	InterpDelay<float, 192010> preDelay{0};

	AllpassFilter<float, static_cast<int32_t>(8 * kInApf1Time * dattorroScaleFactor)> inApf1{dattorroScale(kInApf1Time),
	                                                                                         inputDiffusion1};
	AllpassFilter<float, static_cast<int32_t>(8 * kInApf2Time * dattorroScaleFactor)> inApf2{dattorroScale(kInApf2Time),
	                                                                                         inputDiffusion1};
	AllpassFilter<float, static_cast<int32_t>(8 * kInApf3Time * dattorroScaleFactor)> inApf3{dattorroScale(kInApf3Time),
	                                                                                         inputDiffusion2};
	AllpassFilter<float, static_cast<int32_t>(8 * kInApf4Time * dattorroScaleFactor)> inApf4{dattorroScale(kInApf4Time),
	                                                                                         inputDiffusion2};

	Dattorro1997Tank tank;

	float tankFeed = 0.0;
};
} // namespace deluge::dsp::reverb
