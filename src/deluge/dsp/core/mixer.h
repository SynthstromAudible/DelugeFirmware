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
#include "processor.h"
#include <argon.hpp>
#include <span>

template <typename T>
struct BlockMixer {
	using value_type = T;

	/// @brief Mix a block of samples from two input buffers into an output buffer.
	/// @param inputs A span of input buffers to mix.
	/// @param output The output buffer to fill with the mixed samples.
	virtual void processBlock(std::span<T> input_a, std::span<T> input_b, std::span<T> output) = 0;
};

template <typename T>
struct Mixer : BlockMixer<T> {
	/// @brief Mix two input samples into an output.
	/// @param input_a The first input sample to mix.
	/// @param input_b The second input sample to mix.
	/// @return The mixed sample.
	virtual T process(T input_a, T input_b) = 0;

	/// @brief Mix a block of samples from two input buffers into an output buffer.
	/// @param inputs A span of input buffers to mix.
	/// @param output The output buffer to fill with the mixed samples.
	void processBlock(std::span<T> input_a, std::span<T> input_b, std::span<T> output) override {
		for (size_t i = 0; i < std::min(input_a.size(), std::min(input_b.size(), output.size())); ++i) {
			output[i] = process(input_a[i], input_b[i]);
		}
	}
};

template <typename T>
struct SIMDMixer : BlockMixer<T> {
	/// @brief Mix a vector of samples from two inputs into an output.
	/// @param input_a The first input sample to mix.
	/// @param input_b The second input sample to mix.
	/// @return The mixed sample.
	virtual Argon<T> process(Argon<T> input_a, Argon<T> input_b) = 0;

	/// @brief Mix a block of samples from two input buffers into an output buffer.
	/// @param inputs A span of input buffers to mix.
	/// @param output The output buffer to fill with the mixed samples.
	void processBlock(std::span<T> input_a, std::span<T> input_b, std::span<T> output) override {
		auto input_a_view = argon::vectorize(input_a);
		auto input_b_view = argon::vectorize(input_b);
		auto output_view = argon::vectorize(output);

		for (auto it_a = input_a_view.begin(), it_b = input_b_view.begin(), it_out = output_view.begin();
		     it_a != input_a_view.end() && it_b != input_b_view.end() && it_out != output_view.end();
		     ++it_a, ++it_b, ++it_out) {
			*it_out = process(*it_a, *it_b);
		}
	}
};

template <typename MixerType>
requires std::is_base_of_v<Mixer<typename MixerType::value_type>, MixerType>
struct ProcessorForMixer final : MixerType, Processor<typename MixerType::value_type> {
	using value_type = typename MixerType::value_type; ///< The type of the samples being processed

	using MixerType::MixerType; /// Inherit constructors from the base class

	value_type process(value_type input) override { return MixerType::process(input, 0); }

	using Processor<typename MixerType::value_type>::processBlock;
};

template <typename MixerType>
requires std::is_base_of_v<SIMDMixer<typename MixerType::value_type>, MixerType>
struct SIMDProcessorForSIMDMixer final : MixerType, SIMDProcessor<typename MixerType::value_type> {
	using value_type = typename MixerType::value_type; ///< The type of the samples being processed

	using MixerType::MixerType; /// Inherit constructors from the base class

	Argon<value_type> process(Argon<value_type> input) override { return MixerType::process(input, 0); }

	using SIMDProcessor<typename MixerType::value_type>::processBlock;
};
