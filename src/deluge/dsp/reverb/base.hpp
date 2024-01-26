#pragma once
#include "dsp/stereo_sample.h"
#include <cstdint>
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

	// Dummy functions
	virtual void setRoomSize(float value) {}
	[[nodiscard]] virtual float getRoomSize() const { return 0; };

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
