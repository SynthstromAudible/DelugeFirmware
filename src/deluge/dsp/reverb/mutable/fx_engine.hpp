// Copyright 2023 Katherine Whitlock
// Heavily based on Mutable Instrument's code, copyright 2014 Emilie Gillet
// Base class for building reverb.

#pragma once
#include "cosine_oscillator.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace dsp {
constexpr float OnePole(float& out, float in, float coefficient) {
	out += (coefficient) * ((in)-out);
	return out;
}

template <typename T = float>
constexpr T Interpolate(const T x0, const T x1, float fractional) {
	return static_cast<T>(x0 + (x1 - x0) * fractional);
}
} // namespace dsp

namespace dsp::reverb {
constexpr static int32_t TAIL = -1;

enum LFOIndex { LFO_1, LFO_2 };

class FxEngine {
public:
	FxEngine(std::span<float> signal) : buffer_(signal), mask(buffer_.size() - 1){};
	~FxEngine() = default;

	void Clear() {
		std::fill(buffer_.begin(), buffer_.end(), 0);
		write_ptr_ = 0;
	}

	inline void SetLFOFrequency(LFOIndex index, float frequency) {
		using OscMode = CosineOscillator::Mode;
		lfo_[index].Init<OscMode::APPROX>(frequency * 32.0f);
	}

	inline void Advance() {
		--write_ptr_;
		if (write_ptr_ < 0) {
			write_ptr_ += buffer_.size();
		}
	}

	inline float& at(size_t index) { return buffer_[(write_ptr_ + index) & mask]; }

	inline void StepLFO() {
		if ((write_ptr_ & 31) == 0) {
			lfo_[0].Next();
			lfo_[1].Next();
		}
	}

	inline float LFO(LFOIndex lfo) {
		StepLFO();
		switch (lfo) {
		case LFO_1:
			return lfo_[0].value();
		case LFO_2:
			return lfo_[1].value();
		}
	}

private:
	int32_t write_ptr_ = 0;
	std::span<float> buffer_;
	std::array<CosineOscillator, 2> lfo_;

	size_t mask;

public: /******************** INNER CLASSES ****************/
	class Context {
	public:
		[[nodiscard]] constexpr float Get() const { return accumulator_; }
		constexpr void Set(float value) { accumulator_ = value; }
		constexpr void Add(float value) { accumulator_ += value; }
		constexpr void Multiply(float value) { accumulator_ *= value; }
		constexpr void Reset() { Set(0); }

		inline void Lp(float& state, float coefficient) { accumulator_ = OnePole(state, accumulator_, coefficient); }

		inline void Hp(float& state, float coefficient) { accumulator_ -= OnePole(state, accumulator_, coefficient); }

	private:
		float accumulator_ = 0.f;
	};

	struct DelayLine {
		DelayLine(size_t length) : length(length){};

		// Store and Fetch
		inline void Process(Context& c, size_t offset = 0) {
			this->at(offset) = c.Get();
			c.Set(this->at(length - offset));
		}

		inline float& at(int32_t index) {
			if (index < 0) {
				index = length - index;
			}
			return engine_->at(this->base + index);
		}

		inline float Read(int32_t offset) {
			// STATIC_ASSERT(d.base + d.length <= size, delay_memory_full);
			return this->at(offset);
		}

		inline float Interpolate(float offset) {
			auto offset_integral = static_cast<int32_t>(offset);
			float offset_fractional = offset - static_cast<float>(offset_integral);
			const float a = this->at(offset_integral);
			const float b = this->at(offset_integral + 1);
			return dsp::Interpolate(a, b, offset_fractional);
		}

		// writes to the delayline
		inline void Write(int32_t offset, float value) { this->at(offset) = value; }

	public:
		const size_t length = 0;
		size_t base = 0;
		FxEngine* engine_ = nullptr;
	};

	struct AllPass : public DelayLine {
		AllPass(size_t length) : DelayLine(length){};

		inline float Read(Context& c, int32_t offset, float scale) {
			const float r = DelayLine::Read(offset);
			c.Add(r * scale);
			return r;
		}

		//inline float Read(Context& c, float scale) { return Read(c, scale); }

		inline void Write(Context& c, int32_t offset, float scale) {
			DelayLine::Write(offset, c.Get());
			c.Multiply(scale);
		}

		inline void Write(Context& c, float scale) { Write(c, (int32_t)0, scale); }

		inline void Write(Context& c, int32_t offset, float scale, float input) {
			Write(c, offset, scale);
			c.Add(input);
		}

		inline void Write(Context& c, float scale, float input) { Write(c, 0, scale, input); }

		/** @brief Can be used in place of any AllPass::Read calls */
		inline float Interpolate(Context& c, float offset, float scale) {
			// STATIC_ASSERT(d.base + d.length <= size, delay_memory_full);
			const float r = DelayLine::Interpolate(offset);
			c.Add(r * scale);
			return r;
		}

		inline float Interpolate(Context& c, float offset, LFOIndex index, float amplitude, float scale) {
			offset += amplitude * this->engine_->LFO(index);
			return Interpolate(c, offset, scale);
		}

		inline void ProcessInterpolate(Context& c, float offset, LFOIndex index, float amplitude, float scale) {
			const float read = this->Interpolate(c, offset, index, amplitude, scale);
			this->Write(c, 0, -scale, read);
		}

		// Simple Schroeder allpass section
		//
		//        ------[*-scale]-----,
		//       |    -----------     ∨
		//  ---+-'-->| Delayline |--,-+--->
		//     ∧      -----------   |
		//     '------[*scale]------
		//
		inline void Process(Context& c, float scale) {
			const float head = c.Get();
			const float tail = this->at(TAIL);

			const float feedback = head + (tail * scale);
			this->at(0) = feedback; // feedback into delayline

			const float feedforward = (feedback * -scale) + tail;
			c.Set(feedforward); // output into pipeline

			// c.Add(tail * scale);
			// this->at(0) = c.Get();

			// c.Multiply(-scale);
			// c.Add(tail);
		}
	};

	static void ConstructTopology(FxEngine& e, std::initializer_list<DelayLine*> delays) {
		size_t base = 0;
		for (DelayLine* d : delays) {
			d->engine_ = &e;
			d->base = base;
			base += d->length + 1;
		}
	}
};

} // namespace dsp::reverb
