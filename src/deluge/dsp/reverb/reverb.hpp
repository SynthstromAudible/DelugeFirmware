#pragma once
#include "base.hpp"
#include "deluge/dsp/reverb/reverb.hpp"
#include "digital.hpp"
#include "freeverb/freeverb.hpp"
#include "mutable.hpp"
#include <algorithm>
#include <cstdint>
#include <variant>

namespace deluge::dsp {

class Reverb : reverb::Base {
public:
	enum class Model {
		FREEVERB = 0, // Freeverb is the original
		MUTABLE,
		DIGITAL,
	};

	Reverb()
	    : base_(&std::get<0>(reverb_)),     //<
	      room_size_(base_->getRoomSize()), //<
	      damping_(base_->getDamping()),    //<
	      lpf_(base_->getLPF()),            //<
	      width_(base_->getWidth()) {}
	~Reverb() override = default;

	void setModel(Model m) {
		switch (m) {
		case Model::FREEVERB:
			reverb_.emplace<reverb::Freeverb>();
			break;
		case Model::DIGITAL:
			reverb_.emplace<reverb::Digital>();
			break;
		case Model::MUTABLE:
			reverb_.emplace<reverb::Mutable>();
			break;
		}
		base_->setRoomSize(room_size_);
		base_->setDamping(damping_);
		base_->setWidth(width_);
		base_->setHPF(hpf_);
		base_->setLPF(lpf_);
		model_ = m;
	}

	Model getModel() { return model_; }

	void process(std::span<int32_t> input, std::span<StereoSample> output) override {
		using namespace reverb;
		switch (model_) {
		case Model::FREEVERB:
			reverb_as<Freeverb>().process(input, output);
			break;
		case Model::MUTABLE:
			reverb_as<Mutable>().process(input, output);
			break;
		case Model::DIGITAL:
			reverb_as<Digital>().process(input, output);
			break;
		}
	}

	void setPanLevels(const int32_t amplitude_left, const int32_t amplitude_right) {
		base_->setPanLevels(amplitude_left, amplitude_right);
	}

	void setRoomSize(float value) override {
		room_size_ = value;
		base_->setRoomSize(value);
	}

	[[nodiscard]] float getRoomSize() const override { return base_->getRoomSize(); };

	void setDamping(float value) override {
		damping_ = value;
		base_->setDamping(value);
	}

	[[nodiscard]] float getDamping() const override { return base_->getDamping(); }

	void setWidth(float value) override {
		width_ = value;
		base_->setWidth(value);
	}

	[[nodiscard]] float getWidth() const override { return base_->getWidth(); };

	virtual void setHPF(float f) {
		hpf_ = f;
		base_->setHPF(f);
	}
	[[nodiscard]] virtual float getHPF() const { return base_->getHPF(); }

	virtual void setLPF(float f) {
		lpf_ = f;
		base_->setLPF(f);
	}
	[[nodiscard]] virtual float getLPF() const { return base_->getLPF(); }

	template <typename T>
	constexpr T& reverb_as() {
		return std::get<T>(reverb_);
	}

private:
	std::variant<         //<
	    reverb::Freeverb, //<
	    reverb::Mutable,  //<
	    reverb::Digital   //<
	    >
	    reverb_{};

	Model model_ = Model::FREEVERB;

	reverb::Base* base_ = nullptr;

	float room_size_;
	float damping_;
	float width_;
	float hpf_;
	float lpf_;
};
} // namespace deluge::dsp
