/**
 * Copyright (c) 2025 Katherine Whitlock
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "dsp/core/mixer.h"
#include "dsp/core/processor.h"
#include "util/fixedpoint.h"

namespace deluge::dsp::processor {

template <typename T>
struct Gain {
	T gain = 1.f; ///< The gain value to apply to the input sample

	/// @brief Constructor for GainProcessor.
	/// @param gain The initial gain value to apply.
	Gain(T gain) : gain(gain) {} ///< Constructor to initialize the gain value

	/// @brief Default constructor for GainProcessor.
	Gain() = default;          ///< Default constructor
	virtual ~Gain() = default; ///< Destructor

	/// @brief Set the gain value for the input sample.
	/// @param gain The new gain value to apply.
	void setGain(T gain) { this->gain = gain; } ///< Set the gain value
};

template <typename T>
struct GainProcessor final : Gain<T>, SIMDProcessor<T>, Processor<T> {
	using Gain<T>::Gain;

	/// @brief Apply the gain to a single sample.
	/// @param sample The input sample to render.
	/// @return The processed sample with gain applied.
	[[gnu::always_inline]] T render(T sample) final { return sample * Gain<T>::gain; }

	/// @brief Apply the gain to a vector of samples.
	/// @param input The input vector of samples to render.
	/// @return The processed vector of samples with gain applied.
	[[gnu::always_inline]] Argon<T> render(Argon<T> input) final { return input * Gain<T>::gain; }
};

template <>
struct GainProcessor<FixedPoint<31>> final : Gain<FixedPoint<31>>, SIMDProcessor<int32_t>, Processor<int32_t> {
	using Gain<FixedPoint<31>>::Gain;

	/// @brief Process a single sample and apply the gain.
	/// @param sample The input sample to process.
	/// @return The processed sample with gain applied.
	[[gnu::always_inline]] int32_t render(int32_t sample) final {
		return (FixedPoint<31>::from_raw(sample) * gain).raw();
	}

	/// @brief Process a vector of samples and apply the gain.
	/// @param input The input vector of samples to process.
	/// @return The processed vector of samples with gain applied.
	[[gnu::always_inline]] Argon<int32_t> render(Argon<int32_t> input) final {
		return input.MultiplyFixedPoint(gain.raw());
	}
};

/// @brief Combines a Gain processor with a Unity Mixer to create a GainMixer.
/// @details The GainMixer applies a gain to the first input sample and mixes it with the second input sample.
/// @note This is only really useful on platforms that have a Fused Multiply-Add (FMA) instruction.
template <typename T>
struct GainMixer final : Gain<T>, SIMDMixer<T>, Mixer<T> {
	using Gain<T>::Gain;

	/// @brief Mix two input samples into an output, treating the second input as a unity gain.
	/// @param input_a The first input sample to mix (adjustable gain).
	/// @param input_b The second input sample to mix (unity gain).
	/// @return The mixed sample.
	[[gnu::always_inline]] T render(T input_a, T input_b) final { return (GainProcessor<T>::gain * input_a) + input_b; }

	/// @brief Mix a vector of samples from two inputs into an output, treating the second input as a unity gain.
	/// @param input_a The first input sample to mix (adjustable gain).
	/// @param input_b The second input sample to mix (unity gain).
	/// @return The mixed sample.
	[[gnu::always_inline]] Argon<T> render(Argon<T> input_a, Argon<T> input_b) final {
		return (GainProcessor<T>::gain * input_a) + input_b;
	}
};

/// @brief Combines a Gain processor with a Unity Mixer to create a GainMixer.
/// @details The GainMixer applies a gain to the first input sample and mixes it with the second input sample.
/// @note This is only really useful on platforms that have a Fused Multiply-Add (FMA) instruction.
template <>
struct GainMixer<int32_t> final : Gain<FixedPoint<31>>, SIMDMixer<int32_t>, Mixer<int32_t> {
	using Gain<FixedPoint<31>>::Gain;

	/// @brief Mix two input samples into an output, treating the second input as a unity gain.
	/// @param input_a The first input sample to mix (adjustable gain).
	/// @param input_b The second input sample to mix (unity gain).
	/// @return The mixed sample.
	[[gnu::always_inline]] int32_t render(int32_t input_a, int32_t input_b) final {
		return (FixedPoint<31>::from_raw(input_b).MultiplyAdd(FixedPoint<31>::from_raw(input_a), gain)).raw();
	}

	/// @brief Mix a vector of samples from two inputs into an output, treating the second input as a unity gain.
	/// @param input_a The first input sample to mix (adjustable gain).
	/// @param input_b The second input sample to mix (unity gain).
	/// @return The mixed sample.
	[[gnu::always_inline]] Argon<int32_t> render(Argon<int32_t> input_a, Argon<int32_t> input_b) final {
		return input_b.MultiplyAddFixedPoint(input_a, gain.raw()); ///< Use FMA for mixing
	}
};

/// @brief GainMixerProcessor is a processor that mixes input samples with a unity-gain input buffer.
template <typename T>
struct GainMixerProcessor final : SIMDProcessor<T>, Processor<T> {
	GainMixer<T> gain_mixer;                     ///< The GainMixer instance to apply gain and mix samples
	std::span<T>::iterator unity_input_iterator; ///< Iterator for the unity-input buffer

	/// @brief Constructor for GainMixerProcessor.
	/// @param unity_input A span of input samples to mix with (unity gain).
	/// @details The iterator is initialized to the beginning of the unity input span.
	GainMixerProcessor(T gain, std::span<T> unity_input)
	    : gain_mixer{gain}, unity_input_iterator{unity_input.begin()} {}

	GainMixerProcessor(FixedPoint<31> gain, std::span<T> unity_input)
	    : gain_mixer{gain}, unity_input_iterator{unity_input.begin()} {}

	/// @brief Render a block of samples by mixing the input with the unity-input buffer.
	/// @param input The input samples to render.
	/// @return The mixed output samples.
	[[gnu::always_inline]] Argon<T> render(Argon<T> input) final {
		auto output = gain_mixer.render(input, Argon<T>::Load(&*unity_input_iterator));
		std::advance(unity_input_iterator, Argon<T>::lanes); ///< Advance the iterator for the next call
		return output;
	}

	/// @brief Render a single sample by mixing it with the current unity-input sample.
	/// @param input The input sample to render.
	/// @return The mixed output sample.
	[[gnu::always_inline]] T render(T input) final { return gain_mixer.render(input, *unity_input_iterator++); }
};

GainMixerProcessor(FixedPoint<31>, std::span<int32_t>) -> GainMixerProcessor<int32_t>;
} // namespace deluge::dsp::processor
