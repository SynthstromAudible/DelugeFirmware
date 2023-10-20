#pragma once
#include <algorithm>
#include <cstdint>
#include <variant>

#include "base.hpp"
#include "freeverb.hpp"
#include "mutable/reverb.hpp"
#include "plateau.hpp"

namespace deluge::dsp {

class Reverb : reverb::Base {
public:
	enum class Model {
		Freeverb = 0, // Freeverb is the original
		Mutable,
		Plateau
	};

	Reverb() : base_(&std::get<0>(reverb_)) {}
	~Reverb() override = default;

	void setModel(Model m) {
		switch (m) {
		case Model::Freeverb:
			reverb_.emplace<reverb::Freeverb>();
			break;
		case Model::Mutable:
			reverb_.emplace<reverb::Mutable>();
			break;
		}
		model_ = m;
	}

	Model getModel() { return model_; }

	void Process(std::span<int32_t> input, std::span<StereoSample> output) override {
		using namespace reverb;
		switch (model_) {
		case Model::Freeverb:
			reverb_as<Freeverb>().Process(input, output);
			break;
		case Model::Mutable:
			reverb_as<Mutable>().Process(input, output);
			break;
		case Model::Plateau:
			reverb_as<Plateau>().Process(input, output);
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

	template <typename T>
	constexpr T reverb_as() {
		return std::get<T>(reverb_);
	}

private:
	std::variant<         //<
	    reverb::Freeverb, //<
	    reverb::Mutable,  //<
	    reverb::Plateau   //<
	    >
	    reverb_{};

	Model model_ = Model::Freeverb;

	reverb::Base* base_;
};
} // namespace deluge::dsp
