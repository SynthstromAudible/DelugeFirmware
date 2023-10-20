#pragma once
#include "base.hpp"
#include "valley/Dattorro.hpp"

namespace deluge::dsp::reverb {
class Plateau : public Base {
	Dattorro plate_;
	float diffusion_ = 0;

public:
	Plateau() = default;
	~Plateau() override = default;

	void Process(std::span<int32_t> input, std::span<StereoSample> output) override {
		assert(input.size() == output.size());
		for (size_t i = 0; i < input.size(); ++i) {
			plate_.process(input[i], input[i]);
			output[i].l = plate_.getLeftOutput();
			output[i].r = plate_.getRightOutput();
		}
	}

	void set_room_size(float value) override { plate_.setTimeScale(value); }
	[[nodiscard]] float get_room_size() override { return plate_.getTimeScale(); };

	void set_damping(float value) override { plate_.setDecay(value); }
	[[nodiscard]] float get_damping() override { return plate_.getDecay(); }

	void set_width(float value) override { plate_.setTankDiffusion(diffusion_ = value); }
	[[nodiscard]] float get_width() override { return diffusion_; };
};
} // namespace deluge::dsp::reverb
