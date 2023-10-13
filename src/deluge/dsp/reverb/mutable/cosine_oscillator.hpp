// Copyright 2014 Emilie Gillet. MIT License.
// Cosine oscillator. Generates a cosine between 0.0 and 1.0 with minimal
// CPU use. Fixed frequency.

#pragma once
#include <cmath>
#include <numbers>

class CosineOscillator {
public:
	enum class Mode { APPROX, EXACT };

	CosineOscillator() = default;
	~CosineOscillator() = default;

	template <Mode mode>
	inline void Init(float frequency) {
		if constexpr (mode == Mode::APPROX) {
			InitApproximate(frequency);
		}
		else {
			iir_coefficient_ = 2.0f * std::cos(2.0f * std::numbers::pi_v<float> * frequency);
			initial_amplitude_ = iir_coefficient_ * 0.25f;
		}
		Start();
	}

	inline void InitApproximate(float frequency) {
		float sign = 16.0f;
		frequency -= 0.25f;
		if (frequency < 0.0f) {
			frequency = -frequency;
		}
		else if (frequency > 0.5f) {
			frequency -= 0.5f;
		}
		else {
			sign = -16.0f;
		}
		iir_coefficient_ = sign * frequency * (1.0f - 2.0f * frequency);
		initial_amplitude_ = iir_coefficient_ * 0.25f;
	}

	inline void Start() {
		y[0] = initial_amplitude_;
		y[1] = 0.5f;
	}

	inline float value() const { return y[0] + 0.5f; }

	inline float Next() {
		float temp = y[1];
		y[1] = iir_coefficient_ * y[1] - y[0];
		y[0] = temp;
		return temp + 0.5f;
	}

private:
	float y[2];
	float iir_coefficient_;
	float initial_amplitude_;
};