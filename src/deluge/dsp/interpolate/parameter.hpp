#pragma once
#include <argon.hpp>
#include <cstddef>

namespace deluge::dsp::interpolated {
class Parameter {
public:
	Parameter(float old_value, float new_value, size_t size)
	    : value_(old_value), target_(new_value), increment_((new_value - old_value) / static_cast<float>(size)) {}

	constexpr float Next() {
		if (value_ >= target_) {
			return target_;
		}
		value_ += increment_;
		return value_;
	}

	constexpr Argon<float> NextSIMD() {
		Argon<float> value = Argon{value_}.MultiplyAdd(increment_, {1.f, 2.f, 3.f, 4.f});
		value = (((value <= target_) & value.As<uint32_t>())            // keep lanes less than or equal to target
		         | ((value > target_) & Argon{target_}.As<uint32_t>())) // replace lanes greater than target with target
		            .As<float>();

		value_ = value.LastLane();
		return value;
	}

	constexpr float subsample(float t) { return value_ + (increment_ * t); }

private:
	float value_;
	float target_;
	float increment_;
};
} // namespace deluge::dsp::interpolated
