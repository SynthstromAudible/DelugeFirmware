#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <variant>

#include "base.hpp"
#include "freeverb.hpp"
#include "mutable/reverb.hpp"

namespace dsp {

class Reverb : reverb::Base {
public:
	enum class Model {
		Freeverb = 0, // Freeverb is the original
		Mutable
	};

	Reverb() : base_(&std::get<0>(reverb_)) {}
	~Reverb() override = default;

	void setModel(Model m) {
		switch (m) {
		case Model::Freeverb:
			reverb_.emplace<reverb::Freeverb>();
			break;
		case Model::Mutable:
			reverb_.emplace<reverb::MutableReverb>();
			break;
		}
		model_ = m;
	}

	Model getModel() { return model_; }

	void Process(std::span<int32_t> input, std::span<StereoSample> output) override {
		switch (model_) {
		case Model::Freeverb:
			std::get<reverb::Freeverb>(reverb_).Process(input, output);
			break;
		case Model::Mutable:
			std::get<reverb::MutableReverb>(reverb_).Process(input, output);
			break;
		}
	}

	void set_pan_levels(const int32_t amplitude_left, const int32_t amplitude_right) {
		base_->set_pan_levels(amplitude_left, amplitude_right);
	}

	void set_room_size(float value) override { base_->set_room_size(value); }
	[[nodiscard]] float get_room_size() override { return base_->get_room_size(); };

	void set_damping(float value) override { base_->set_damping(value); }
	[[nodiscard]] float get_damping() override { return base_->get_damping(); }

	void set_width(float value) override { base_->set_width(value); }
	[[nodiscard]] float get_width() override { return base_->get_width(); };

private:
	std::variant<             //<
	    reverb::Freeverb,     //<
	    reverb::MutableReverb //<
	    >
	    reverb_;

	Model model_ = Model::Mutable;

	reverb::Base* base_;
};
} // namespace dsp
