#pragma once
#include <cmath>
#include <cstdint>
#include <stdio.h>

#define _1_FACT_2 0.5
#define _1_FACT_3 0.1666666667
#define _1_FACT_4 0.04166666667
#define _1_FACT_5 0.008333333333
#define _2M_PI 2.0 * M_PI

template <typename T>
T fastexp(T x) {
	T xx = x * x;
	T x3 = x * xx;
	T x4 = xx * xx;
	T x5 = x4 * x;
	x = 1 + x + (xx * _1_FACT_2) + (x3 * _1_FACT_3) + (x4 * _1_FACT_4);
	return x + (x5 * _1_FACT_5);
}

class OnePoleLPFilter {
public:
	OnePoleLPFilter(float cutoffFreq = 22049.0);
	float process();
	void clear();
	void setCutoffFreq(float cutoffFreq);
	[[nodiscard]] float getMaxCutoffFreq() const;
	float input = 0.0;
	float output = 0.0;

private:
	static constexpr float _sampleRate = 44100.0;
	static constexpr float _1_sampleRate = 1.0 / _sampleRate;
	static constexpr float _maxCutoffFreq = _sampleRate / 2.0 - 1.0;
	float _cutoffFreq = 0.0;
	float _a = 0.0;
	float _b = 0.0;
	float _z = 0.0;
};

//////////////////////////////////////////////////////////////////////////////////////////////////

class OnePoleHPFilter {
public:
	OnePoleHPFilter(float initCutoffFreq = 10.0);
	float process();
	void clear();
	void setCutoffFreq(float cutoffFreq);
	float input = 0.0;
	float output = 0.0;

private:
	static constexpr float _sampleRate = 0.0;
	static constexpr float _1_sampleRate = 0.0;
	static constexpr float _maxCutoffFreq = _sampleRate / 2.0 - 1.0;
	float _cutoffFreq = 0.0;
	float _y0 = 0.0;
	float _y1 = 0.0;
	float _x0 = 0.0;
	float _x1 = 0.0;
	float _a0 = 0.0;
	float _a1 = 0.0;
	float _b1 = 0.0;
};

class DCBlocker {
public:
	DCBlocker() = default;
	DCBlocker(float cutoffFreq);
	float process(float input);
	void clear();
	void setCutoffFreq(float cutoffFreq);
	[[nodiscard]] static constexpr float getMaxCutoffFreq() { return _maxCutoffFreq; };
	float output = 0.f;

private:
	static constexpr float _sampleRate = 44100.f;
	static constexpr float _maxCutoffFreq = _sampleRate / 2.0f;
	float _cutoffFreq = 20.f;
	float _b = 0.999f;
	float _z = 0.f;
};
