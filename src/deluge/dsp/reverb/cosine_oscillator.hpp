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
	constexpr DualCosineOscillator(std::array<float, 2> frequencies) : frequencies_(frequencies[0], frequencies[1]) {
		Init<mode>();
	}
	~DualCosineOscillator() = default;

	template <Mode mode = Mode::APPROX>
	inline void SetFrequency(int index, float frequency) {
		frequencies_[index] = frequency;
		Init<mode>();
	}

	inline void InitApproximate() {
		ArgonHalf<float> sign = 16.0f;
		ArgonHalf<float> frequencies = frequencies_;
		frequencies = frequencies.each_lane_with_index([&](float& frequency, int i) {
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

	[[nodiscard]] inline ArgonHalf<float> values() const { return y_0 + 0.5f; }

	inline ArgonHalf<float> Next() {
		ArgonHalf<float> temp = y_1;
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

	ArgonHalf<float> frequencies_;
	ArgonHalf<float> y_0;
	ArgonHalf<float> y_1;
	ArgonHalf<float> iir_coefficient_;
	ArgonHalf<float> initial_amplitude_;
};
