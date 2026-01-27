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
#include "deluge/util/fixedpoint.h"
#include "dsp_ng/core/mixer.hpp"
#include "dsp_ng/core/processor.hpp"
#include "dsp_ng/core/types.hpp"

namespace deluge::dsp::processor {

template <typename T>
struct Gain {
	T gain = 1.f; ///< The gain value to apply to the input sample

	/// @brief Constructor for GainProcessor.
	/// @param gain The initial gain value to apply.
	Gain(T gain) : gain(gain) {} ///< Constructor to initialize the gain value

	Gain() = default;
	virtual ~Gain() = default;

	/// @brief Set the gain value for the input sample.
	/// @param gain The new gain value to apply.
	void setGain(T gain) { this->gain = gain; }
};

template <typename T>
struct GainProcessor final : Gain<T>, Processor<Argon<T>>, Processor<T> {
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
struct GainProcessor<fixed_point::Sample> final : Gain<FixedPoint<31>>,
                                                  Processor<Argon<q31_t>>,
                                                  Processor<fixed_point::Sample> {
	using Gain<FixedPoint<31>>::Gain;

	/// @brief Process a single sample and apply the gain.
	/// @param sample The input sample to process.
	/// @return The processed sample with gain applied.
	[[gnu::always_inline]] fixed_point::Sample render(fixed_point::Sample sample) final { return sample * gain.raw(); }

	/// @brief Process a vector of samples and apply the gain.
	/// @param input The input vector of samples to process.
	/// @return The processed vector of samples with gain applied.
	[[gnu::always_inline]] Argon<q31_t> render(Argon<q31_t> input) final {
		return input.MultiplyFixedPoint(gain.raw());
	}
};

/// @brief Combines a Gain processor with a Unity Mixer to create a GainMixer.
/// @details The GainMixer applies a gain to the first input sample and mixes it with the second input sample.
/// @note This is only really useful on platforms that have a Fused Multiply-Add (FMA) instruction.
template <typename T>
struct GainMixer final : Gain<T>, Mixer<Argon<T>>, Mixer<T> {
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
struct GainMixer<fixed_point::Sample> final : Gain<FixedPoint<31>>, Mixer<Argon<q31_t>>, Mixer<fixed_point::Sample> {
	using Gain<FixedPoint<31>>::Gain;

	/// @brief Mix two input samples into an output, treating the second input as a unity gain.
	/// @param input_a The first input sample to mix (adjustable gain).
	/// @param input_b The second input sample to mix (unity gain).
	/// @return The mixed sample.
	[[gnu::always_inline]] fixed_point::Sample render(fixed_point::Sample input_a, fixed_point::Sample input_b) final {
		return input_b.MultiplyAdd(input_a, gain);
	}

	/// @brief Mix a vector of samples from two inputs into an output, treating the second input as a unity gain.
	/// @param input_a The first input sample to mix (adjustable gain).
	/// @param input_b The second input sample to mix (unity gain).
	/// @return The mixed sample.
	[[gnu::always_inline]] Argon<q31_t> render(Argon<q31_t> input_a, Argon<q31_t> input_b) final {
		return input_b.MultiplyAddFixedPoint(input_a, gain.raw());
	}
};

/// @brief GainMixerProcessor is a processor that mixes input samples with a unity-gain input buffer.
template <typename T>
struct GainMixerProcessor final : Processor<Argon<T>>, Processor<T> {
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

GainMixerProcessor(FixedPoint<31>, fixed_point::Buffer) -> GainMixerProcessor<fixed_point::Sample>;
} // namespace deluge::dsp::processor
