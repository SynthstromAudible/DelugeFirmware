// Copyright 2014 Emilie Gillet. MIT License.
// Cosine oscillator. Generates a cosine between 0.0 and 1.0 with minimal
// CPU use. Fixed frequency.

#pragma once
#include "argon.hpp"
#include <cmath>
#include <initializer_list>

class DualCosineOscillator {
public:
	enum class Mode { APPROX, EXACT };

	template <Mode mode = Mode::APPROX>
	constexpr DualCosineOscillator(std::initializer_list<float> frequencies) : frequencies_(frequencies) {
		Init<mode>();
	}
	~DualCosineOscillator() = default;

	template <Mode mode = Mode::APPROX>
	inline void SetFrequency(int index, float frequency) {
		frequencies_[index] = frequency;
		Init<mode>();
	}

	inline void InitApproximate() {
		argon::Neon64<float> sign = 16.0f;
		argon::Neon64<float> frequencies = frequencies_;
		frequencies.each_lane([&](float& frequency, int i) {
			frequency -= 0.25f;
			if (frequency < 0.0f) {
				frequency = -frequency;
			}
			else if (frequency > 0.5f) {
				frequency -= 0.5f;
			}
			else {
				sign[i] = -16.0f;
			}
		});
		iir_coefficient_ = sign * frequencies * (1.0f - (2.0f * frequencies));
		initial_amplitude_ = iir_coefficient_ * 0.25f;
	}

	constexpr void Start() {
		y_0 = initial_amplitude_;
		y_1 = 0.5f;
	}

	[[nodiscard]] inline argon::Neon64<float> values() const { return y_0 + 0.5f; }

	inline argon::Neon64<float> Next() {
		argon::Neon64<float> temp = y_1;
		y_1 = iir_coefficient_ * y_1 - y_0;
		y_0 = temp;
		return temp + 0.5f;
	}

private:
	template <Mode mode = Mode::APPROX>
	inline void Init() {
		if constexpr (mode == Mode::APPROX) {
			InitApproximate();
		}
		else {
			for (size_t i = 0; i < frequencies_.size(); ++i) {
				iir_coefficient_[i] = 2.0f * std::cos(2.0f * std::numbers::pi_v<float> * frequencies_[i]);
				initial_amplitude_[i] = iir_coefficient_[i] * 0.25f;
			}
		}
		Start();
	}

	argon::Neon64<float> frequencies_;
	argon::Neon64<float> y_0;
	argon::Neon64<float> y_1;
	argon::Neon64<float> iir_coefficient_;
	argon::Neon64<float> initial_amplitude_;
};
