#pragma once
// Deluge adapter for the MIT-licensed Mutable Instruments Plaits engines.
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "processing/plaits/engine_parameter.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
namespace deluge::dsp {
namespace plaits_detail {
inline float sanitizeOutput(float sample) {
	// -ffast-math may remove std::isfinite checks. Inspect IEEE-754 bits instead.
	if ((std::bit_cast<uint32_t>(sample) & 0x7f800000u) == 0x7f800000u)
		return 0.f;
	return std::clamp(sample, -1.f, 1.f);
}
} // namespace plaits_detail
class PlaitsVoice {
public:
	explicit PlaitsVoice(unsigned model, bool deferred = false) : model_(std::min(model, 2u)) {
		if (!deferred)
			initialize();
	}
	~PlaitsVoice() { destroy(); }
	unsigned model() const { return model_; }
	void setModelFromFinal(int base, int32_t value) {
		setModel(engine_ ? plaits_control::stableModelFromFinal(base, value, model_)
		                 : plaits_control::modelFromFinal(base, value));
	}
	// Reuses reserved engine storage. No heap allocation when changing a model.
	void setModel(unsigned model) {
		model = std::min(model, 2u);
		if (!engine_) {
			model_ = model;
			initialize();
			return;
		}
		if (model == model_)
			return;
		destroy();
		storage_.fill(std::byte{});
		temp_.fill(0.f);
		model_ = model;
		initialize();
		transitionFrom_ = lastOutput_;
		transitionRemaining_ = transitionSamples;
	}
	PlaitsVoice(const PlaitsVoice&) = delete;
	PlaitsVoice& operator=(const PlaitsVoice&) = delete;
	void render(uint32_t phaseIncrement, float harmonics, float timbre, float morph, float* output, size_t count) {
		if (!phaseIncrement) {
			std::fill_n(output, count, 0.f);
			lastOutput_ = 0.f;
			transitionRemaining_ = 0;
			return;
		}
		if (!engine_)
			initialize();
		plaits::EngineParameters p{};
		// Match normalized frequency to Deluge's phase increment. Plaits' original
		// NoteToFrequency assumes 47872.34 Hz; Deluge runs at 44100 Hz.
		if (phaseIncrement != cachedIncrement_) {
			cachedIncrement_ = phaseIncrement;
			cachedNote_ = 69.f
			              + 12.f
			                    * std::log2(static_cast<float>(phaseIncrement)
			                                * (plaits::kCorrectedSampleRate / (4294967296.f * 440.f)));
		}
		p.note = cachedNote_;
		p.harmonics = std::clamp(harmonics, 0.f, 1.f);
		p.timbre = std::clamp(timbre, 0.f, 1.f);
		p.morph = std::clamp(morph, 0.f, 1.f);
		p.trigger = plaits::TRIGGER_UNPATCHED;
		p.accent = 1.f;
		while (count) {
			const auto n = std::min(count, plaits::kMaxBlockSize);
			bool enveloped = false;
			// Static dispatch allows engine-specific inlining; AUX is unused.
			switch (model_) {
			case 0:
				static_cast<plaits::FMEngine*>(engine_)->Render(p, output, nullptr, n, &enveloped);
				break;
			case 1:
				static_cast<plaits::WaveshapingEngine*>(engine_)->Render(p, output, nullptr, n, &enveloped);
				break;
			default:
				static_cast<plaits::VirtualAnalogEngine*>(engine_)->Render(p, output, nullptr, n, &enveloped);
				break;
			}
			for (size_t i = 0; i < n; ++i) {
				float sample = plaits_detail::sanitizeOutput(output[i]);
				if (transitionRemaining_) {
					const float mix = static_cast<float>(transitionRemaining_--) / transitionSamples;
					sample = sample * (1.f - mix) + transitionFrom_ * mix;
				}
				output[i] = lastOutput_ = sample;
			}
			output += n;
			count -= n;
		}
	}

private:
	void initialize() {
		switch (model_) {
		case 0:
			engine_ = new (storage_.data()) plaits::FMEngine;
			break;
		case 1:
			engine_ = new (storage_.data()) plaits::WaveshapingEngine;
			break;
		default:
			model_ = 2;
			engine_ = new (storage_.data()) plaits::VirtualAnalogEngine;
			break;
		}
		stmlib::BufferAllocator allocator(temp_.data(), sizeof(temp_));
		engine_->Init(&allocator);
	}
	void destroy() {
		if (!engine_)
			return;
		switch (model_) {
		case 0:
			static_cast<plaits::FMEngine*>(engine_)->~FMEngine();
			break;
		case 1:
			static_cast<plaits::WaveshapingEngine*>(engine_)->~WaveshapingEngine();
			break;
		default:
			static_cast<plaits::VirtualAnalogEngine*>(engine_)->~VirtualAnalogEngine();
			break;
		}
	}
	static constexpr unsigned transitionSamples = 64;
	unsigned transitionRemaining_ = 0;
	float lastOutput_ = 0.f;
	float transitionFrom_ = 0.f;
	static constexpr size_t storageSize =
	    std::max({sizeof(plaits::FMEngine), sizeof(plaits::WaveshapingEngine), sizeof(plaits::VirtualAnalogEngine)});
	// Upstream assumes zero-initialized static memory (including filter history).
	alignas(std::max_align_t) std::array<std::byte, storageSize> storage_{};
	std::array<float, plaits::kMaxBlockSize> temp_{};
	uint32_t cachedIncrement_ = 0;
	float cachedNote_ = 0.f;
	unsigned model_;
	plaits::Engine* engine_ = nullptr;
};
} // namespace deluge::dsp
