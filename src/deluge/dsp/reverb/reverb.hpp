#pragma once
#include "base.hpp"
#include "deluge/dsp/reverb/reverb.hpp"
#include "digital.hpp"
#include "featherverb.hpp"
#include "freeverb/freeverb.hpp"
#include "mutable.hpp"
#include <algorithm>
#include <cstdint>
#include <variant>

namespace deluge::dsp {

class [[gnu::hot]] Reverb : reverb::Base {
public:
	enum class Model {
		FEATHERVERB = 0, // Lightweight FDN reverb (default)
		FREEVERB,
		MUTABLE,
		DIGITAL,
	};

	Reverb()
	    : base_(&std::get<0>(reverb_)),     //<
	      room_size_(base_->getRoomSize()), //<
	      damping_(base_->getDamping()),    //<
	      width_(base_->getWidth()),        //<
	      hpf_(base_->getHPF()),            //<
	      lpf_(base_->getLPF()) {
		// Note: allocate() must be called after memory allocator is initialized
		// This is done in AudioEngine::init()
	}
	~Reverb() override = default;

	// Allocate reverb buffer - call after construction
	[[nodiscard]] bool allocate() {
		using namespace reverb;
		if (model_ == Model::FEATHERVERB) {
			return reverb_as<Featherverb>().allocate();
		}
		return true; // Other models don't need explicit allocation
	}

	void setModel(Model m) {
		switch (m) {
		case Model::FEATHERVERB:
			reverb_.emplace<reverb::Featherverb>();
			base_ = &std::get<reverb::Featherverb>(reverb_);
			(void)std::get<reverb::Featherverb>(reverb_).allocate();
			break;
		case Model::FREEVERB:
			reverb_.emplace<reverb::Freeverb>();
			base_ = &std::get<reverb::Freeverb>(reverb_);
			break;
		case Model::DIGITAL:
			reverb_.emplace<reverb::Digital>();
			base_ = &std::get<reverb::Digital>(reverb_);
			break;
		case Model::MUTABLE:
			reverb_.emplace<reverb::Mutable>();
			base_ = &std::get<reverb::Mutable>(reverb_);
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
		case Model::FEATHERVERB:
			reverb_as<Featherverb>().process(input, output);
			break;
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

	void setHPF(float f) override {
		hpf_ = f;
		base_->setHPF(f);
	}
	[[nodiscard]] float getHPF() const override { return base_->getHPF(); }

	void setLPF(float f) override {
		lpf_ = f;
		base_->setLPF(f);
	}
	[[nodiscard]] float getLPF() const override { return base_->getLPF(); }

	// Featherverb zone accessors
	void setFeatherZone1(int32_t value) {
		featherZone1_ = value;
		if (model_ == Model::FEATHERVERB) {
			reverb_as<reverb::Featherverb>().setZone1(value);
		}
	}
	[[nodiscard]] int32_t getFeatherZone1() const {
		if (model_ == Model::FEATHERVERB) {
			return std::get<reverb::Featherverb>(reverb_).getZone1();
		}
		return featherZone1_;
	}

	void setFeatherZone2(int32_t value) {
		featherZone2_ = value;
		if (model_ == Model::FEATHERVERB) {
			reverb_as<reverb::Featherverb>().setZone2(value);
		}
	}
	[[nodiscard]] int32_t getFeatherZone2() const {
		if (model_ == Model::FEATHERVERB) {
			return std::get<reverb::Featherverb>(reverb_).getZone2();
		}
		return featherZone2_;
	}

	void setFeatherZone3(int32_t value) {
		featherZone3_ = value;
		if (model_ == Model::FEATHERVERB) {
			reverb_as<reverb::Featherverb>().setZone3(value);
		}
	}
	[[nodiscard]] int32_t getFeatherZone3() const {
		if (model_ == Model::FEATHERVERB) {
			return std::get<reverb::Featherverb>(reverb_).getZone3();
		}
		return featherZone3_;
	}

	void setFeatherPredelay(float value) {
		featherPredelay_ = value;
		if (model_ == Model::FEATHERVERB) {
			reverb_as<reverb::Featherverb>().setPredelay(value);
		}
	}
	[[nodiscard]] float getFeatherPredelay() const {
		if (model_ == Model::FEATHERVERB) {
			return std::get<reverb::Featherverb>(reverb_).getPredelay();
		}
		return featherPredelay_;
	}

	void setFeatherCascadeOnly(bool value) {
		if (model_ == Model::FEATHERVERB) {
			reverb_as<reverb::Featherverb>().setCascadeOnly(value);
		}
	}
	[[nodiscard]] bool getFeatherCascadeOnly() const {
		if (model_ == Model::FEATHERVERB) {
			return std::get<reverb::Featherverb>(reverb_).getCascadeOnly();
		}
		return false;
	}

	template <typename T>
	constexpr T& reverb_as() {
		return std::get<T>(reverb_);
	}

private:
	std::variant<            //<
	    reverb::Featherverb, //<
	    reverb::Freeverb,    //<
	    reverb::Mutable,     //<
	    reverb::Digital      //<
	    >
	    reverb_{};

	Model model_ = Model::FEATHERVERB;

	reverb::Base* base_ = nullptr;

	float room_size_;
	float damping_;
	float width_;
	float hpf_;
	float lpf_;

	// Featherverb zone caches (preserved across model switches)
	int32_t featherZone1_{512};
	int32_t featherZone2_{512};
	int32_t featherZone3_{0};
	float featherPredelay_{0.0f};
};
} // namespace deluge::dsp
