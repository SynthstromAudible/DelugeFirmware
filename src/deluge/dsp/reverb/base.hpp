#pragma once
#include "dsp/stereo_sample.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include <type_traits>

namespace dsp::reverb {
struct Base {
	Base() = default;
	virtual ~Base() = default;;

	virtual void Process(std::span<int32_t> input, std::span<StereoSample> output) = 0;

	constexpr void set_pan_levels(const int32_t amplitude_left, const int32_t amplitude_right) {
		amplitude_right_ = amplitude_right;
		amplitude_left_ = amplitude_left;
	}

	// Dummy functions
	virtual void set_room_size(float value) {}
	[[nodiscard]] virtual float get_room_size() { return 0; };

	virtual void set_damping(float value) {}
	[[nodiscard]] virtual float get_damping() { return 0; }

	virtual void set_width(float value) {}
	[[nodiscard]] virtual float get_width() { return 0; };

protected:
	int32_t amplitude_right_ = 0;
	int32_t amplitude_left_ = 0;
};
} // namespace dsp::reverb
