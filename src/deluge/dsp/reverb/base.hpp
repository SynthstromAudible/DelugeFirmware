#pragma once
#include "dsp/stereo_sample.h"
#include <cmath>
#include <span>

namespace deluge::dsp::reverb {
struct Base {
	Base() = default;
	virtual ~Base() = default;

	virtual void process(std::span<int32_t> input, std::span<StereoSample> output) = 0;

	constexpr void setPanLevels(const int32_t amplitude_left, const int32_t amplitude_right) {
		amplitude_right_ = amplitude_right;
		amplitude_left_ = amplitude_left;
	}
	enum class FilterType { LowPass, HighPass };
	template <FilterType filtertype>
	static constexpr float calcFilterCutoff(float f) {
		float minFreq;
		float maxFreq;
		// this exp will be between 1 and 4.48, half the knob range is about 2
		// for the HPF the result will then be from 0 to 500Hz with half the knob range at 300hz
		// then shift to 20-520Hz as there is a low end buildup in the reverb that should always be filtered out
		// for the LPF the result will be from 0 to 20000 with half the knob range at 5678.537hz
		if constexpr (filtertype == FilterType::LowPass) {
			minFreq = 0.0f;
			maxFreq = 5083.74f;
		}
		else if constexpr (filtertype == FilterType::HighPass) {
			minFreq = 20.0f;
			maxFreq = 150.0f;
		}
		float fc_hz = minFreq + (std::exp(1.5f * f) - 1) * maxFreq;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1 + fc);
		return wc;
	}

	// Dummy functions
	virtual void setRoomSize(float value) {}
	[[nodiscard]] virtual float getRoomSize() const { return 0; };

	virtual void setHPF(float f) {}
	[[nodiscard]] virtual float getHPF() const { return 0; }

	virtual void setLPF(float f) {}
	[[nodiscard]] virtual float getLPF() const { return 0; }

	virtual void setDamping(float value) {}
	[[nodiscard]] virtual float getDamping() const { return 0; }

	virtual void setWidth(float value) {}
	[[nodiscard]] virtual float getWidth() const { return 0; };

	[[nodiscard]] constexpr int32_t getPanLeft() const { return amplitude_left_; }
	[[nodiscard]] constexpr int32_t getPanRight() const { return amplitude_right_; }

private:
	int32_t amplitude_right_ = 0;
	int32_t amplitude_left_ = 0;
};
} // namespace deluge::dsp::reverb
