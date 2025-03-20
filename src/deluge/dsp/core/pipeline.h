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
#include "dsp/core/generator.h"
#include "dsp/core/processor.h"
#include <tuple>
#include <type_traits>

template <typename ProcessorType, typename = void, typename... Types>
struct Pipeline;

template <typename ProcessorType, typename... Types>
struct Pipeline<
    ProcessorType,
    std::enable_if_t<std::conjunction_v<
        std::is_base_of<Processor<typename std::remove_pointer_t<ProcessorType>::value_type>,
                        std::remove_pointer_t<ProcessorType>>,
        std::negation<std::is_base_of<SIMDProcessor<typename std::remove_pointer_t<ProcessorType>::value_type>,
                                      std::remove_pointer_t<ProcessorType>>>>>,
    Types...> : std::tuple<ProcessorType, Types...>,
                Processor<typename std::remove_pointer_t<ProcessorType>::value_type> {
	using std::tuple<ProcessorType, Types...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename std::remove_pointer_t<ProcessorType>::value_type;

	static_assert((std::is_base_of_v<Processor<value_type>, std::remove_pointer_t<Types>> && ...),
	              "All types in the pipeline must be sample processors");

	/// @brief Process a sample using the processors in the pipeline.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	value_type render(value_type sample) override {
		sample = std::invoke(static_cast<value_type (std::remove_pointer_t<ProcessorType>::*)(value_type)>(
		                         &std::remove_pointer_t<ProcessorType>::render),
		                     std::get<ProcessorType>(*this), sample); // Call render() for the first processor
		((sample = std::invoke(static_cast<value_type (std::remove_pointer_t<Types>::*)(value_type)>(
		                           &std::remove_pointer_t<Types>::render),
		                       std::get<Types>(*this), sample)),
		 ...); // Call render() for each processor in the tuple
		return sample;
	}
};

template <typename ProcessorType, typename... Types>
struct Pipeline<
    ProcessorType,
    std::enable_if_t<std::is_base_of_v<SIMDProcessor<typename std::remove_pointer_t<ProcessorType>::value_type>,
                                       std::remove_pointer_t<ProcessorType>>>,
    Types...> : std::tuple<ProcessorType, Types...>,
                SIMDProcessor<typename std::remove_pointer_t<ProcessorType>::value_type> {
	using std::tuple<ProcessorType, Types...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename std::remove_pointer_t<ProcessorType>::value_type;

	static_assert((std::is_base_of_v<SIMDProcessor<value_type>, std::remove_pointer_t<Types>> && ...),
	              "All types in the pipeline must be SIMD processors with the same value type");

	/// @brief Process a block of samples using SIMD operations.
	/// @param samples The input buffer of samples to process.
	/// @return The processed buffer of samples.
	Argon<value_type> render(Argon<value_type> sample) override {
		sample =
		    std::invoke(static_cast<Argon<value_type> (std::remove_pointer_t<ProcessorType>::*)(Argon<value_type>)>(
		                    &std::remove_pointer_t<ProcessorType>::render),
		                std::get<ProcessorType>(*this), sample); // Call render() for the first processor
		((sample = std::invoke(static_cast<Argon<value_type> (std::remove_pointer_t<Types>::*)(Argon<value_type>)>(
		                           &std::remove_pointer_t<Types>::render),
		                       std::get<Types>(*this), sample)),
		 ...); // Call render() for each processor in the tuple
		return sample;
	}
};

template <typename GeneratorType, typename... ProcessorTypes>
struct Pipeline<
    GeneratorType,
    std::enable_if_t<std::conjunction_v<
        std::is_base_of<Generator<typename std::remove_pointer_t<GeneratorType>::value_type>,
                        std::remove_pointer_t<GeneratorType>>,
        std::negation<std::is_base_of<SIMDGenerator<typename std::remove_pointer_t<GeneratorType>::value_type>,
                                      std::remove_pointer_t<GeneratorType>>>>>,
    ProcessorTypes...> : std::tuple<GeneratorType, ProcessorTypes...>,
                         Generator<typename std::remove_pointer_t<GeneratorType>::value_type> {
	using std::tuple<GeneratorType, ProcessorTypes...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename std::remove_pointer_t<GeneratorType>::value_type;

	static_assert((std::is_base_of_v<Processor<value_type>, std::remove_pointer_t<ProcessorTypes>> && ...),
	              "All processors in the pipeline must be sample processors with the same value type");

	/// @brief Generate a sample using the generators in the pipeline.
	/// @return The generated sample.
	value_type render() override {
		value_type sample = std::invoke(static_cast<value_type (std::remove_pointer_t<GeneratorType>::*)()>(
		                                    &std::remove_pointer_t<GeneratorType>::render),
		                                std::get<GeneratorType>(*this)); // Start with the first generator's output
		((sample = std::invoke(static_cast<value_type (std::remove_pointer_t<ProcessorTypes>::*)(value_type)>(
		                           &std::remove_pointer_t<ProcessorTypes>::render),
		                       std::get<ProcessorTypes>(*this), sample)),
		 ...);
		return sample;
	}
};

template <typename GeneratorType, typename... ProcessorTypes>
struct Pipeline<
    GeneratorType,
    std::enable_if_t<std::is_base_of_v<SIMDGenerator<typename std::remove_pointer_t<GeneratorType>::value_type>,
                                       std::remove_pointer_t<GeneratorType>>>,
    ProcessorTypes...> : std::tuple<GeneratorType, ProcessorTypes...>,
                         SIMDGenerator<typename std::remove_pointer_t<GeneratorType>::value_type> {
	using std::tuple<GeneratorType, ProcessorTypes...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename std::remove_pointer_t<GeneratorType>::value_type;

	static_assert((std::is_base_of_v<SIMDProcessor<value_type>, std::remove_pointer_t<ProcessorTypes>> && ...),
	              "All processors in the pipeline must be SIMD processors with the same value type");

	/// @brief Generate a vector of samples using the generators in the pipeline.
	/// @return The generated vector of samples.
	Argon<value_type> render() override {
		Argon<value_type> sample =
		    std::invoke(static_cast<Argon<value_type> (std::remove_pointer_t<GeneratorType>::*)()>(
		                    &std::remove_pointer_t<GeneratorType>::render),
		                std::get<GeneratorType>(*this)); // Start with the first generator's output
		((sample =
		      std::invoke(static_cast<Argon<value_type> (std::remove_pointer_t<ProcessorTypes>::*)(Argon<value_type>)>(
		                      &std::remove_pointer_t<ProcessorTypes>::render),
		                  std::get<ProcessorTypes>(*this), sample)),
		 ...);
		return sample;
	}
};

template <class ProcessorType, class... Types>
Pipeline(ProcessorType, Types...) -> Pipeline<ProcessorType, void, Types...>;
