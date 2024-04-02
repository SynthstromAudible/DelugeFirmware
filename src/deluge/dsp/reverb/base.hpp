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

	static constexpr float calcFilterCutoff(float f) {
		// this exp will be between 1 and 4.48, half the knob range is about 2
		// the result will then be from 0 to 330Hz with half the knob range at 200hz
		// then shift to 20-350Hz as there is a low end buildup in the reverb that should always be filtered out
		float fc_hz = 20 + (std::exp(1.5f * f) - 1) * 100;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1 + fc);
		return wc;
	}

	// Dummy functions
	virtual void setRoomSize(float value) {}
	[[nodiscard]] virtual float getRoomSize() const { return 0; };

	virtual void setHPF(float f) {}
	[[nodiscard]] virtual float getHPF() const { return 0; }

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
