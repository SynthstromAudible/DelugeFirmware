#pragma once

#include <algorithm>
#include <cstdint>

namespace deluge::hid::encoders {

class EncoderAcceleration {
public:
	struct Tuning {
		// - acceleration and inertia (1 - acceleration) control how fast the knob speeds up
		// - speedScale controls how we go from "raw speed" to speed used as offset multiplier
		// - min and max speed clamp the max effective speed
		//
		// current values have been tuned to be slow enough to feel easy to control, but fast
		// enough to go from 0 to 50 with one fast turn of the encoder. speedScale and min/max
		// could potentially be user-configurable in a small range.
		double acceleration = 0.1;
		double speed_scale = 0.15;
		double min_multiplier = 1.0;
		double max_multiplier = 3.0;
		double reset_after = 0.3;
	};

	double advance(int8_t offset, double time) { return advance(offset, time, Tuning{}); }
	double advance(int8_t offset, double time, Tuning tuning) {
		const double elapsed = time - last_time_;
		if (elapsed >= tuning.reset_after || elapsed < 0.0 || offset != last_offset_) {
			// too much time passed, or the knob direction changed, reset the speed
			speed_ = 0.0;
		}
		else if (elapsed > 0.0) {
			// moving in the same direction, update speed
			speed_ = speed_ * (1.0 - tuning.acceleration) + tuning.acceleration / elapsed;
		}
		// last_time and last_offset keep track of our time and direction
		last_time_ = time;
		last_offset_ = offset;
		return std::clamp(speed_ * tuning.speed_scale, tuning.min_multiplier, tuning.max_multiplier);
	}
	void reset() { *this = {}; }

private:
	double speed_ = 0.0;
	double last_time_ = 0.0;
	int8_t last_offset_ = 0;
};

inline constexpr EncoderAcceleration::Tuning kGoldEncoderAcceleration{};
inline constexpr EncoderAcceleration::Tuning kHorizontalMenuAcceleration{.max_multiplier = 5.0};

} // namespace deluge::hid::encoders
