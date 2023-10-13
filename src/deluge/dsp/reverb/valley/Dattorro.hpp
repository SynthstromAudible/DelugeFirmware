//
// This plate reverb is based upon Jon Dattorro's 1997 reverb algorithm.
//

#pragma once
#include "AllpassFilter.hpp"
#include "LFO.hpp"
#include "OnePoleFilters.hpp"
#include <array>

class Dattorro1997Tank {
public:
	Dattorro1997Tank(const float initMaxLfoDepth = 0.0, const float initMaxTimeScale = 1.0);

	void process(const float leftInput, const float rightIn, float* leftOut, float* rightOut);

	void freeze(const bool freezeFlag);

	void setTimeScale(const float newTimeScale);

	void setDecay(const float newDecay);

	void setModSpeed(const float newModSpeed);
	void setModDepth(const float newModDepth);
	void setModShape(const float shape);

	void setHighCutFrequency(const float frequency);
	void setLowCutFrequency(const float frequency);

	void setDiffusion(const float diffusion);

	void clear();

private:
	static constexpr float dattorroSampleRate = 29761.0;
	static constexpr float dattorroSampleTime = 1.0 / dattorroSampleRate;

	static constexpr float leftApf1Time = 672.0;
	static constexpr float leftDelay1Time = 4453.0;
	static constexpr float leftApf2Time = 1800.0;
	static constexpr float leftDelay2Time = 3720.0;

	static constexpr float rightApf1Time = 908.0;
	static constexpr float rightDelay1Time = 4217.0;
	static constexpr float rightApf2Time = 2656.0;
	static constexpr float rightDelay2Time = 3163.0;

	static constexpr size_t leftDelay1RightTap1 = 266;
	static constexpr size_t leftDelay1RightTap2 = 2974;
	static constexpr size_t leftApf2RightTap1 = 1913;
	static constexpr size_t leftDelay2RightTap = 1996;
	static constexpr size_t rightDelay1RightTap = 1990;
	static constexpr size_t rightApf2RightTap = 187;
	static constexpr size_t rightDelay2RightTap = 1066;

	enum LeftOutTaps {
		L_DELAY_1_L_TAP_1,
		L_DELAY_1_L_TAP_2,
		L_APF_2_L_TAP,
		L_DELAY_2_L_TAP,
		R_DELAY_1_L_TAP,
		R_APF_2_L_TAP,
		R_DELAY_2_L_TAP
	};

	enum RightOutTaps {
		R_DELAY_1_R_TAP_1,
		R_DELAY_1_R_TAP_2,
		R_APF_2_R_TAP,
		R_DELAY_2_R_TAP,
		L_DELAY_1_R_TAP,
		L_APF_2_R_TAP,
		L_DELAY_2_R_TAP
	};

	const long kOutputTaps[7] = {266, 2974, 1913, 1996, 1990, 187, 1066};

	static constexpr float maxDiffusion1 = 0.7;
	static constexpr float maxDiffusion2 = 0.7;

	static constexpr float lfoMaxExcursion = 16.0;
	static constexpr float lfo1Freq{0.10f};
	static constexpr float lfo2Freq{0.150f};
	static constexpr float lfo3Freq{0.120f};
	static constexpr float lfo4Freq{0.180f};

	static constexpr float minTimeScale = 0.0001;

	float timePadding = 0.0;

	float scaledLeftApf1Time = leftApf1Time;
	float scaledLeftDelay1Time = leftDelay1Time;
	float scaledLeftApf2Time = leftApf2Time;
	float scaledLeftDelay2Time = leftDelay2Time;

	float scaledRightApf1Time = rightApf1Time;
	float scaledRightDelay1Time = rightDelay1Time;
	float scaledRightApf2Time = rightApf2Time;
	float scaledRightDelay2Time = rightDelay2Time;

	std::array<long, 7> scaledOutputTaps;

	static constexpr float sampleRate = 44100.f;
	static constexpr float sampleRateScale = sampleRate / dattorroSampleRate;

	float maxTimeScale = 1.0;
	float timeScale = 1.0;

	float modDepth = 0.0;
	float decayParam = 0.0;
	float decay = 0.0;

	float lfoExcursion = 0.0;

	// Freeze Cross fade
	bool frozen = false;
	float fade = 1.0;
	float fadeTime = 0.002;
	float fadeStep = 1.0 / (fadeTime * sampleRate);
	float fadeDir = 1.0;

	TriSawLFOBlock lfos;

	float leftSum = 0.0;
	float rightSum = 0.0;

	AllpassFilter<float> leftApf1;
	InterpDelay<float> leftDelay1;
	OnePoleLPFilter leftHighCutFilter;
	OnePoleHPFilter leftLowCutFilter;
	AllpassFilter<float> leftApf2;
	InterpDelay<float> leftDelay2;

	AllpassFilter<float> rightApf1;
	InterpDelay<float> rightDelay1;
	OnePoleLPFilter rightHighCutFilter;
	;
	OnePoleHPFilter rightLowCutFilter;
	AllpassFilter<float> rightApf2;
	InterpDelay<float> rightDelay2;

	OnePoleHPFilter leftOutDCBlock;
	OnePoleHPFilter rightOutDCBlock;

	void initialiseDelaysAndApfs();

	void tickApfModulation();

	void rescaleApfAndDelayTimes();
	void rescaleTapTimes();
};

class Dattorro {
public:
	Dattorro(const float initMaxLfoDepth = 16.0, const float initMaxTimeScale = 1.0);
	void process(float leftInput, float rightInput);
	void clear();

	void setTimeScale(float timeScale);
	void setPreDelay(float time);

	void freeze(const bool freezeFlag);

	void setInputFilterLowCutoffPitch(float pitch);
	void setInputFilterHighCutoffPitch(float pitch);
	void enableInputDiffusion(bool enable);

	void setDecay(float newDecay);
	void setTankDiffusion(const float diffusion);
	void setTankFilterHighCutFrequency(const float frequency);
	void setTankFilterLowCutFrequency(const float frequency);

	void setTankModSpeed(const float modSpeed);
	void setTankModDepth(const float modDepth);
	void setTankModShape(const float modShape);

	[[nodiscard]] float getLeftOutput() const;
	[[nodiscard]] float getRightOutput() const;

private:
	float preDelayTime = 0.0;
	static constexpr long kInApf1Time = 141;
	static constexpr long kInApf2Time = 107;
	static constexpr long kInApf3Time = 379;
	static constexpr long kInApf4Time = 277;

	static constexpr float dattorroSampleRate = 29761.0;
	static constexpr float sampleRate = 44100.0;
	static constexpr float dattorroScaleFactor = sampleRate / dattorroSampleRate;
	float leftSum = 0.0;
	float rightSum = 0.0;

	float rightOut = 0.0;
	float leftOut = 0.0;
	float inputLowCut = 0.0;
	float inputHighCut = 10000.0;
	float reverbHighCut = 10000.0;
	float reverbLowCut = 0.0;
	float modDepth = 1.0;
	float inputDiffusion1 = 0.75;
	float inputDiffusion2 = 0.625;
	float decay = 0.9999;
	float modSpeed = 1.0;
	float diffuseInput = 0.0;

	OnePoleHPFilter leftInputDCBlock;
	OnePoleHPFilter rightInputDCBlock;
	OnePoleLPFilter inputLpf;
	OnePoleHPFilter inputHpf;

	InterpDelay<float, 192010> preDelay;

	AllpassFilter<float> inApf1;
	AllpassFilter<float> inApf2;
	AllpassFilter<float> inApf3;
	AllpassFilter<float> inApf4;

	Dattorro1997Tank tank;

	float tankFeed = 0.0;

	float dattorroScale(float delayTime);
};
