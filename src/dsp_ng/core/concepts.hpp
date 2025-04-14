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
#include "dsp_ng/core/types.hpp"
#include "processor.hpp"

namespace deluge::dsp {
/// @brief A concept for generators that render a block of samples.
/// @tparam GeneratorType The type of the generator.
/// @details This concept checks if the generator has a method `renderBlock` that takes a `Buffer` of samples.
template <typename GeneratorType>
concept block_generator = requires(GeneratorType g, Buffer<typename GeneratorType::value_type> buffer) {
	{ g.renderBlock(buffer) } -> std::same_as<void>;
};

/// @brief A concept for generators that render a single sample.
/// @tparam GeneratorType The type of the generator.
/// @details This concept checks if the generator has a method `render` that returns a sample of the same type as the
/// generator's value type.
template <typename GeneratorType>
concept sample_generator = requires(GeneratorType g) {
	{ g.render() } -> std::same_as<typename GeneratorType::value_type>;
};

/// @brief A concept for generators that can render both blocks and single samples.
/// @tparam GeneratorType The type of the generator.
/// @details This concept checks if the generator has both `renderBlock` and `render` methods.
template <typename GeneratorType>
concept generator = block_generator<GeneratorType> && sample_generator<GeneratorType>;

/// @brief A concept for processors that render a block of samples.
/// @tparam ProcessorType The type of the processor.
/// @details This concept checks if the processor has a method `renderBlock` that takes a `Signal` and a `Buffer` of
/// samples. The `Signal` is the input signal, and the `Buffer` is the output buffer where the processed signal will be
/// stored.
template <typename ProcessorType>
concept block_processor = requires(ProcessorType t, Signal<typename ProcessorType::value_type> input,
                                   Buffer<typename ProcessorType::value_type> output) {
	{ t.renderBlock(input, output) } -> std::same_as<void>;
};

/// @brief A concept for processors that render a single sample.
/// @tparam ProcessorType The type of the processor.
/// @details This concept checks if the processor has a method `render` that takes a sample of the same type as the
/// processor's value type. The method should return a sample of the same type as the processor's value type.
template <typename ProcessorType>
concept sample_processor = requires(ProcessorType t, typename ProcessorType::value_type sample) {
	{ t.render(sample) } -> std::same_as<typename ProcessorType::value_type>;
};

/// @brief A concept for processors that can render both blocks and single samples.
/// @tparam ProcessorType The type of the processor.
/// @details This concept checks if the processor has both `renderBlock` and `render` methods.
/// @see block_processor
/// @see sample_processor
template <typename ProcessorType>
concept processor = block_processor<ProcessorType> && sample_processor<ProcessorType>;

/// @brief A concept for converters that render a block of samples.
/// @tparam ConverterType The type of the converter.
/// @details This concept checks if the converter has a method `render` that takes a sample of the same type as the
/// converter's input type. The method should return a sample of the same type as the converter's output type.
template <typename ConverterType>
concept sample_converter = requires(ConverterType t, typename ConverterType::input_type sample) {
	{ t.render(sample) } -> std::same_as<typename ConverterType::output_type>;
};

/// @brief A concept for mixers that render a block of samples.
/// @tparam MixerType The type of the mixer.
/// @details This concept checks if the mixer has a method `renderBlock` that takes two `Signal` inputs and a `Buffer`
/// for the output. The method should return a `void`.
template <typename MixerType>
concept block_mixer =
    requires(MixerType t, Signal<typename MixerType::value_type> input_a,
             Signal<typename MixerType::value_type> input_b, Buffer<typename MixerType::value_type> output) {
	    { t.renderBlock(input_a, input_b, output) } -> std::same_as<void>;
    };

/// @brief A concept for mixers that render a single sample at a time.
/// @tparam MixerType The type of the mixer.
/// @details This concept checks if the mixer has a method `render` that takes two samples of the same type as the
/// mixer's value type. The method should return a sample of the same type as the mixer's value type.
template <typename MixerType>
concept sample_mixer =
    requires(MixerType t, typename MixerType::value_type input_a, typename MixerType::value_type input_b) {
	    { t.render(input_a, input_b) } -> std::same_as<typename MixerType::value_type>;
    };

/// @brief A concept for mixers that can render both blocks and single samples.
/// @tparam MixerType The type of the mixer.
/// @details This concept checks if the mixer has both `renderBlock` and `render` methods.
/// @see block_mixer
/// @see sample_mixer
template <typename MixerType>
concept mixer = block_mixer<MixerType> && sample_mixer<MixerType>;

} // namespace deluge::dsp
