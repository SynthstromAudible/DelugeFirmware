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
struct Pipeline<ProcessorType,
                std::enable_if_t<std::conjunction_v<
                    std::is_base_of<Processor<typename ProcessorType::value_type>, ProcessorType>,
                    std::negation<std::is_base_of<SIMDProcessor<typename ProcessorType::value_type>, ProcessorType>>>>,
                Types...> : std::tuple<ProcessorType, Types...>,
                            Processor<typename ProcessorType::value_type> {
	using std::tuple<ProcessorType, Types...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename ProcessorType::value_type;

	static_assert((std::is_base_of_v<Processor<value_type>, Types> && ...),
	              "All types in the pipeline must be sample processors");

	/// @brief Process a sample using the processors in the pipeline.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	value_type process(value_type sample) override {
		sample = std::get<ProcessorType>(*this).process(sample);  // Call process() for the first processor
		((sample = std::get<Types>(*this).process(sample)), ...); // Call process() for each processor in the tuple
		return sample;
	}
};

template <typename ProcessorType, typename... Types>
struct Pipeline<ProcessorType,
                std::enable_if_t<std::is_base_of_v<SIMDProcessor<typename ProcessorType::value_type>, ProcessorType>>,
                Types...> : std::tuple<ProcessorType, Types...>,
                            SIMDProcessor<typename ProcessorType::value_type> {
	using std::tuple<ProcessorType, Types...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename ProcessorType::value_type;

	static_assert((std::is_base_of_v<SIMDProcessor<value_type>, Types> && ...),
	              "All types in the pipeline must be SIMD processors with the same value type");

	/// @brief Process a block of samples using SIMD operations.
	/// @param samples The input buffer of samples to process.
	/// @return The processed buffer of samples.
	Argon<value_type> process(Argon<value_type> sample) override {
		sample = std::get<ProcessorType>(*this).process(sample);  // Call process() for the first processor
		((sample = std::get<Types>(*this).process(sample)), ...); // Call process() for each processor in the tuple
		return sample;
	}
};

template <typename GeneratorType, typename... ProcessorTypes>
struct Pipeline<GeneratorType,
                std::enable_if_t<std::conjunction_v<
                    std::is_base_of<Generator<typename GeneratorType::value_type>, GeneratorType>,
                    std::negation<std::is_base_of<SIMDGenerator<typename GeneratorType::value_type>, GeneratorType>>>>,
                ProcessorTypes...> : std::tuple<GeneratorType, ProcessorTypes...>,
                                     Generator<typename GeneratorType::value_type> {
	using std::tuple<GeneratorType, ProcessorTypes...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename GeneratorType::value_type;

	static_assert((std::is_base_of_v<Processor<value_type>, ProcessorTypes> && ...),
	              "All processors in the pipeline must be sample processors with the same value type");

	/// @brief Generate a sample using the generators in the pipeline.
	/// @return The generated sample.
	value_type process() override {
		value_type sample = std::get<GeneratorType>(*this).process(); // Start with the first generator's output
		((sample = std::get<ProcessorTypes>(*this).process(sample)), ...);
		return sample;
	}
};

template <typename GeneratorType, typename... ProcessorTypes>
struct Pipeline<GeneratorType,
                std::enable_if_t<std::is_base_of_v<SIMDGenerator<typename GeneratorType::value_type>, GeneratorType>>,
                ProcessorTypes...> : std::tuple<GeneratorType, ProcessorTypes...>,
                                     SIMDGenerator<typename GeneratorType::value_type> {
	using std::tuple<GeneratorType, ProcessorTypes...>::tuple; // Inherit constructors from std::tuple

	using value_type = typename GeneratorType::value_type;

	static_assert((std::is_base_of_v<SIMDProcessor<value_type>, ProcessorTypes> && ...),
	              "All processors in the pipeline must be SIMD processors with the same value type");

	/// @brief Generate a vector of samples using the generators in the pipeline.
	/// @return The generated vector of samples.
	Argon<value_type> process() override {
		Argon<value_type> sample = std::get<GeneratorType>(*this).process(); // Start with the first generator's output
		((sample = std::get<ProcessorTypes>(*this).process(sample)), ...);
		return sample;
	}
};

template <class ProcessorType, class... Types>
Pipeline(ProcessorType, Types...) -> Pipeline<ProcessorType, void, Types...>;
