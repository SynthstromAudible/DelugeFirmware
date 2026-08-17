#pragma once
#include "base.hpp"
#include "deluge/dsp/reverb/reverb.hpp"
#include "digital.hpp"
#include "freeverb/freeverb.hpp"
#include "mutable.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <variant>

namespace deluge::dsp {

class [[gnu::hot]] Reverb : reverb::Base {
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

	/// Replaces the active reverb with a freshly built one of model `m`.
	///
	/// Callable only from outside the audio tier. `emplace` destroys the live reverb and constructs a
	/// ~128 KB replacement in its storage, which is far too long to mask interrupts for, and the audio
	/// tier renders from an interrupt executor that can preempt this at any point. So the rebuild
	/// publishes `rebuilding_` instead: while it is set the audio tier contributes no reverb, which
	/// costs a few blocks of reverb tail on a model change and never observes a half-built object.
	void setModel(Model m) {
		rebuilding_.store(true, std::memory_order_release);
		// Re-point `base_` as we go: `emplace` returns a reference to the new alternative, and every
		// accessor below reaches the reverb through this pointer.
		switch (m) {
		case Model::FREEVERB:
			base_ = &reverb_.emplace<reverb::Freeverb>();
			break;
		case Model::DIGITAL:
			base_ = &reverb_.emplace<reverb::Digital>();
			break;
		case Model::MUTABLE:
			base_ = &reverb_.emplace<reverb::Mutable>();
			break;
		}
		base_->setRoomSize(room_size_);
		base_->setDamping(damping_);
		base_->setWidth(width_);
		base_->setHPF(hpf_);
		base_->setLPF(lpf_);
		model_ = m;
		rebuilding_.store(false, std::memory_order_release);
	}

	Model getModel() { return model_; }

	/// Mixes the reverb into `output`. Audio-tier entry point.
	///
	/// Dispatches through the variant's own discriminant rather than `model_`: the two are separate
	/// pieces of state and a reader that trusted `model_` could reach `std::get` for an alternative the
	/// variant no longer held, which throws `bad_variant_access` and terminates. The variant is the
	/// single source of truth, so it cannot disagree with itself.
	void process(std::span<int32_t> input, std::span<StereoSample> output) override {
		// Mid-rebuild there is no reverb to render. `process` mixes into `output`, so contributing
		// nothing leaves the dry signal intact.
		if (rebuilding_.load(std::memory_order_acquire)) {
			return;
		}
		std::visit([&](auto& r) { r.process(input, output); }, reverb_);
	}

	/// Audio-tier entry point, called each block just before `process`; it reaches the reverb through
	/// `base_`, so it has to sit out a rebuild for the same reason `process` does. The levels are
	/// re-applied on the next block.
	void setPanLevels(const int32_t amplitude_left, const int32_t amplitude_right) {
		if (rebuilding_.load(std::memory_order_acquire)) {
			return;
		}
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

	/// Set while `setModel` tears down and rebuilds the active reverb, so the audio tier knows to
	/// leave it alone. Written only outside the audio tier, read only inside it.
	std::atomic<bool> rebuilding_ = false;

	reverb::Base* base_ = nullptr;

	float room_size_;
	float damping_;
	float width_;
	float hpf_{};
	float lpf_;
};
} // namespace deluge::dsp
