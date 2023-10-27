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

	static constexpr float sampleRate = 44100.f;
	static constexpr float sampleRateScale = sampleRate / dattorroSampleRate;

	static constexpr std::array<int32_t, 7> kOutputTaps = {266, 2974, 1913, 1996, 1990, 187, 1066};

	static constexpr std::array<int32_t, 7> scaledOutputTaps{{
	    static_cast<int32_t>(266 * sampleRateScale),  //<
	    static_cast<int32_t>(2974 * sampleRateScale), //<
	    static_cast<int32_t>(1913 * sampleRateScale), //<
	    static_cast<int32_t>(1996 * sampleRateScale), //<
	    static_cast<int32_t>(1990 * sampleRateScale), //<
	    static_cast<int32_t>(187 * sampleRateScale),  //<
	    static_cast<int32_t>(1066 * sampleRateScale), //<
	}};

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

	AllpassFilter<float, 512> leftApf1;
	InterpDelay<float, std::max(scaledOutputTaps[L_DELAY_1_L_TAP_1], scaledOutputTaps[L_DELAY_1_L_TAP_2]) + 1>
	    leftDelay1;
	OnePoleLPFilter leftHighCutFilter;
	OnePoleHPFilter leftLowCutFilter;
	AllpassFilter<float, scaledOutputTaps[L_APF_2_L_TAP] + 1> leftApf2;
	InterpDelay<float, scaledOutputTaps[L_DELAY_2_L_TAP] + 1> leftDelay2;

	AllpassFilter<float, 512> rightApf1;
	InterpDelay<float, std::max(scaledOutputTaps[R_DELAY_1_R_TAP_1], scaledOutputTaps[R_DELAY_1_R_TAP_2]) + 1>
	    rightDelay1;
	OnePoleLPFilter rightHighCutFilter;
	OnePoleHPFilter rightLowCutFilter;
	AllpassFilter<float, scaledOutputTaps[R_APF_2_R_TAP] + 1> rightApf2;
	InterpDelay<float, scaledOutputTaps[R_DELAY_2_R_TAP] + 1> rightDelay2;

	OnePoleHPFilter leftOutDCBlock;
	OnePoleHPFilter rightOutDCBlock;

	void initialiseDelaysAndApfs();

	void tickApfModulation();

	void rescaleApfAndDelayTimes();
};
