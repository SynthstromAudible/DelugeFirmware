#pragma once
#include "base.hpp"
#include "freeverb/revmodel.hpp"

namespace deluge::dsp::reverb {
class Freeverb : public Base {
	revmodel model_;

public:
	Freeverb() = default;
	~Freeverb() override = default;

	void Process(std::span<int32_t> input, std::span<StereoSample> output) override {
		int32_t output_left = 0;
		int32_t output_right = 0;

		for (size_t frame = 0; frame < input.size(); frame++) {
			const int32_t input_sample = input[frame];
			StereoSample& output_sample = output[frame];

			model_.process(input_sample, &output_left, &output_right);
			output_sample.l += multiply_32x32_rshift32_rounded(output_left, this->amplitude_left_);
			output_sample.r += multiply_32x32_rshift32_rounded(output_right, this->amplitude_right_);
		}
	}

	void set_room_size(float value) override { model_.setroomsize(value); }
	[[nodiscard]] float get_room_size() override { return model_.getroomsize(); };

	void set_damping(float value) override { model_.setdamp(value); }
	[[nodiscard]] float get_damping() override { return model_.getdamp(); }

	void set_width(float value) override { model_.setwidth(value); }
	[[nodiscard]] float get_width() override { return model_.getwidth(); };
};
} // namespace dsp::reverb
