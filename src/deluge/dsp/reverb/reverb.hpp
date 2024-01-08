#pragma once
#include "base.hpp"
#include "freeverb/freeverb.hpp"
#include "mutable/reverb.hpp"
#include <algorithm>
#include <cstdint>
#include <variant>

namespace deluge::dsp {

class Reverb : reverb::Base {
public:
	enum class Model {
		FREEVERB = 0, // Freeverb is the original
		MUTABLE,
	};

	Reverb() : base_(&std::get<0>(reverb_)) {}
	~Reverb() override = default;

	void setModel(Model m) {
		switch (m) {
		case Model::FREEVERB:
			reverb_.emplace<reverb::Freeverb>();
			break;
		case Model::MUTABLE:
			reverb_.emplace<reverb::Mutable>();
			break;
		}
		model_ = m;
	}

	Model getModel() { return model_; }

	void Process(std::span<int32_t> input, std::span<StereoSample> output) override {
		using namespace reverb;
		switch (model_) {
		case Model::FREEVERB:
			reverb_as<Freeverb>().Process(input, output);
			break;
		case Model::MUTABLE:
			reverb_as<Mutable>().Process(input, output);
			break;
		}
	}

	void setPanLevels(const int32_t amplitude_left, const int32_t amplitude_right) {
		base_->setPanLevels(amplitude_left, amplitude_right);
	}

	void setRoomSize(float value) override { base_->setRoomSize(value); }
	[[nodiscard]] float getRoomSize() const override { return base_->getRoomSize(); };

	void setDamping(float value) override { base_->setDamping(value); }
	[[nodiscard]] float getDamping() const override { return base_->getDamping(); }

	void setWidth(float value) override { base_->setWidth(value); }
	[[nodiscard]] float getWidth() const override { return base_->getWidth(); };

	template <typename T>
	constexpr T& reverb_as() {
		return std::get<T>(reverb_);
	}

private:
	std::variant<         //<
	    reverb::Freeverb, //<
	    reverb::Mutable   //<
	    >
	    reverb_{};

	Model model_ = Model::FREEVERB;

	reverb::Base* base_ = nullptr;
};
} // namespace deluge::dsp
