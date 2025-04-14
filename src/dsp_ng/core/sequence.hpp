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
#include "dsp_ng/core/generator.hpp"
#include "dsp_ng/core/processor.hpp"
#include "dsp_ng/core/util.hpp"
#include <tuple>
#include <type_traits>

/// @file A sequence is a collection of processors that are executed in order (sequentially). This is useful
/// for creating simple block processors out of small component sample processors.
///
/// For example:
/// ```cpp
/// Sequence processor{
///     PWMOscillator(),
///     Gain(0.5f),
///     Amplitude(),
/// };
///
/// processor.processBlock(input, output);
/// ```
/// @note The sequence should contain only inlineable or extremely small processors to increase code locality and
/// caching. If you need to use a large processor, consider using a chain instead. The sequence is not designed to be
/// used to compose functional processors (e.g. Oscillator, Filter, Delay) but rather to create those processors.

namespace deluge::dsp {

/// @brief A class to represent sequentially executed processors.
/// @tparam ProcessorType The type of the first processor in the pipeline.
/// @tparam Types The types of the remaining processors in the pipeline.
template <typename ProcessorType, typename = void, typename... Types>
struct Sequence;

/// @brief A class to represent a collection of DSP processors executed sequentially.
/// @tparam ProcessorType The type of the first processor in the pipeline.
/// @tparam Types The types of the remaining processors in the pipeline.
template <typename ProcessorType, typename... Types>
struct Sequence<ProcessorType,
                std::enable_if_t<
                    /// The first processor must be a sample processor
                    std::is_base_of_v<Processor<typename std::remove_pointer_t<ProcessorType>::value_type>,
                                      std::remove_pointer_t<ProcessorType>>
                    /// And it must not be a SIMD processor
                    && !std::is_base_of_v<Processor<Argon<typename std::remove_pointer_t<ProcessorType>::value_type>>,
                                          std::remove_pointer_t<ProcessorType>>>,
                Types...>
    final : std::tuple<ProcessorType, Types...>, Processor<typename std::remove_pointer_t<ProcessorType>::value_type> {
	using std::tuple<ProcessorType, Types...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename std::remove_pointer_t<ProcessorType>::value_type;

	static_assert((std::is_base_of_v<Processor<value_type>, std::remove_pointer_t<Types>> && ...),
	              "All types in the pipeline must be sample processors");

	/// @brief Process a sample using the processors in the pipeline.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	value_type render(value_type sample) final {
		// Call render() for the first processor
		sample = util::dereference(std::get<ProcessorType>(*this)).render(sample);

		// Call render() for each processor in the tuple
		((sample = util::dereference(std::get<Types>(*this)).render(sample)), ...);
		return sample;
	}
};

template <typename ProcessorType, typename... Types>
struct Sequence<
    ProcessorType,
    /// The first processor must be a SIMD processor
    std::enable_if_t<std::is_base_of_v<Processor<Argon<typename std::remove_pointer_t<ProcessorType>::value_type>>,
                                       std::remove_pointer_t<ProcessorType>>>,
    Types...>
    final : std::tuple<ProcessorType, Types...>,
            Processor<Argon<typename std::remove_pointer_t<ProcessorType>::value_type>> {
	using std::tuple<ProcessorType, Types...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename std::remove_pointer_t<ProcessorType>::value_type;

	static_assert((std::is_base_of_v<Processor<Argon<value_type>>, std::remove_pointer_t<Types>> && ...),
	              "All types in the pipeline must be SIMD processors with the same value type");

	Argon<value_type> render(Argon<value_type> sample) final {
		// Call render() for the first processor
		sample = util::dereference(std::get<ProcessorType>(*this)).render(sample);

		// Call render() for each processor in the tuple
		((sample = util::dereference(std::get<Types>(*this)).render(sample)), ...);
		return sample;
	}
};

template <typename GeneratorType, typename... ProcessorTypes>
struct Sequence<
    GeneratorType,
    std::enable_if_t<std::conjunction_v<
        /// The generator must be a sample generator
        std::is_base_of<Generator<typename std::remove_pointer_t<GeneratorType>::value_type>,
                        std::remove_pointer_t<GeneratorType>>,
        /// And it must not be a SIMD generator
        std::negation<std::is_base_of<Generator<Argon<typename std::remove_pointer_t<GeneratorType>::value_type>>,
                                      std::remove_pointer_t<GeneratorType>>>>>,
    ProcessorTypes...>
    final : std::tuple<GeneratorType, ProcessorTypes...>,
            Generator<typename std::remove_pointer_t<GeneratorType>::value_type> {
	using std::tuple<GeneratorType, ProcessorTypes...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename std::remove_pointer_t<GeneratorType>::value_type;

	static_assert((std::is_base_of_v<Processor<value_type>, std::remove_pointer_t<ProcessorTypes>> && ...),
	              "All processors in the pipeline must be sample processors with the same value type");

	/// @brief Generate a sample using the generators in the pipeline.
	/// @return The generated sample.
	value_type render() final {
		// Start with the first generator's output
		value_type sample = util::dereference(std::get<GeneratorType>(*this)).render();
		((sample = util::dereference(std::get<ProcessorTypes>(*this)).render(sample)), ...);
	}
};

template <typename GeneratorType, typename... ProcessorTypes>
struct Sequence<
    GeneratorType,
    std::enable_if_t<std::is_base_of_v<Generator<Argon<typename std::remove_pointer_t<GeneratorType>::value_type>>,
                                       std::remove_pointer_t<GeneratorType>>>,
    ProcessorTypes...>
    final : std::tuple<GeneratorType, ProcessorTypes...>,
            Generator<Argon<typename std::remove_pointer_t<GeneratorType>::value_type>> {
	using std::tuple<GeneratorType, ProcessorTypes...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename std::remove_pointer_t<GeneratorType>::value_type;

	static_assert((std::is_base_of_v<Processor<Argon<value_type>>, std::remove_pointer_t<ProcessorTypes>> && ...),
	              "All processors in the pipeline must be SIMD processors with the same value type");

	/// @brief Generate a vector of samples using the generators in the pipeline.
	/// @return The generated vector of samples.
	Argon<value_type> render() final {
		// Start with the first generator's output
		Argon<value_type> sample = util::dereference(std::get<GeneratorType>(*this)).render();
		((sample = util::dereference(std::get<ProcessorTypes>(*this)).render(sample)), ...);
		return sample;
	}
};

/// @brief Deduction guide for Sequence
template <class ProcessorType, class... Types>
Sequence(ProcessorType, Types...) -> Sequence<ProcessorType, void, Types...>;
} // namespace deluge::dsp
