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
#include <functional> // For std::invoke
#include <optional>
#include <type_traits>

/// @brief ConditionalGenerator is a generator that conditionally applies another generator based on a given condition.
/// @tparam CondType The type of the condition (e.g., a boolean or a callable).
/// @tparam GeneratorType The type of the generator to apply if the condition is true.
template <typename CondType, typename GeneratorType, typename = void>
struct ConditionalGenerator; // Forward declaration

template <typename CondType, typename GeneratorType>
struct ConditionalGenerator<
    CondType, GeneratorType,
    std::enable_if_t<std::is_base_of_v<Generator<typename std::remove_pointer_t<GeneratorType>::value_type>,
                                       std::remove_pointer_t<GeneratorType>>>>
    : Generator<typename std::remove_pointer_t<GeneratorType>::value_type> {
	CondType condition_;          // Condition to apply the generators
	GeneratorType generator_;     // Contained generator (can be a pointer)
	GeneratorType elseGenerator_; // Else generator (required)

	using value_type = typename std::remove_pointer_t<GeneratorType>::value_type;

	ConditionalGenerator(CondType condition, GeneratorType generator, GeneratorType elseGenerator)
	    : condition_(condition), generator_(generator), elseGenerator_(elseGenerator) {}

	/// @brief Generate a value conditionally applying generators.
	/// @return The generated value.
	value_type generate() override {
		if (std::invoke(condition_)) {                                                       // Check the condition
			return std::invoke(&std::remove_pointer_t<GeneratorType>::generate, generator_); // Apply the generator
		}
		return std::invoke(&std::remove_pointer_t<GeneratorType>::generate, elseGenerator_); // Apply the else generator
	}
};

template <typename GeneratorType>
struct ConditionalGenerator<
    bool, GeneratorType,
    std::enable_if_t<std::is_base_of_v<Generator<typename std::remove_pointer_t<GeneratorType>::value_type>,
                                       std::remove_pointer_t<GeneratorType>>>>
    : Generator<typename std::remove_pointer_t<GeneratorType>::value_type> {
	bool condition_;              // Condition to apply the generators
	GeneratorType generator_;     // Contained generator (can be a pointer)
	GeneratorType elseGenerator_; // Else generator (required)

	using value_type = typename std::remove_pointer_t<GeneratorType>::value_type;

	ConditionalGenerator(bool condition, GeneratorType generator, GeneratorType elseGenerator)
	    : condition_(condition), generator_(generator), elseGenerator_(elseGenerator) {}

	/// @brief Generate a value conditionally applying generators.
	/// @return The generated value.
	value_type generate() override {
		if (condition_) {                                                                    // Check the condition
			return std::invoke(&std::remove_pointer_t<GeneratorType>::generate, generator_); // Apply the generator
		}
		return std::invoke(&std::remove_pointer_t<GeneratorType>::generate, elseGenerator_); // Apply the else generator
	}
};

template <typename CondType, typename GeneratorType>
struct ConditionalGenerator<
    CondType, GeneratorType,
    std::enable_if_t<std::is_base_of_v<SIMDGenerator<typename std::remove_pointer_t<GeneratorType>::value_type>,
                                       std::remove_pointer_t<GeneratorType>>>>
    : SIMDGenerator<typename std::remove_pointer_t<GeneratorType>::value_type> {
	CondType condition_;          // Condition to apply the generators
	GeneratorType generator_;     // Contained generator (can be a pointer)
	GeneratorType elseGenerator_; // Else generator (required)

	using value_type = typename std::remove_pointer_t<GeneratorType>::value_type;

	ConditionalGenerator(CondType condition, GeneratorType generator, GeneratorType elseGenerator)
	    : condition_(condition), generator_(generator), elseGenerator_(elseGenerator) {}

	/// @brief Generate a SIMD value conditionally applying generators.
	/// @return The generated SIMD value.
	Argon<value_type> generate() override {
		if (std::invoke(condition_)) {                                                       // Check the condition
			return std::invoke(&std::remove_pointer_t<GeneratorType>::generate, generator_); // Apply the generator
		}
		return std::invoke(&std::remove_pointer_t<GeneratorType>::generate, elseGenerator_); // Apply the else generator
	}
};

template <typename GeneratorType>
struct ConditionalGenerator<
    bool, GeneratorType,
    std::enable_if_t<std::is_base_of_v<SIMDGenerator<typename std::remove_pointer_t<GeneratorType>::value_type>,
                                       std::remove_pointer_t<GeneratorType>>>>
    : SIMDGenerator<typename std::remove_pointer_t<GeneratorType>::value_type> {
	bool condition_;              // Condition to apply the generators
	GeneratorType generator_;     // Contained generator (can be a pointer)
	GeneratorType elseGenerator_; // Else generator (required)

	using value_type = typename std::remove_pointer_t<GeneratorType>::value_type;

	ConditionalGenerator(bool condition, GeneratorType generator, GeneratorType elseGenerator)
	    : condition_(condition), generator_(generator), elseGenerator_(elseGenerator) {}

	/// @brief Generate a SIMD value conditionally applying generators.
	/// @return The generated SIMD value.
	Argon<value_type> generate() override {
		if (condition_) {                                                                    // Check the condition
			return std::invoke(&std::remove_pointer_t<GeneratorType>::generate, generator_); // Apply the generator
		}
		return std::invoke(&std::remove_pointer_t<GeneratorType>::generate, elseGenerator_); // Apply the else generator
	}
};

template <typename GeneratorType>
ConditionalGenerator(bool, GeneratorType, GeneratorType) -> ConditionalGenerator<bool, GeneratorType>;

template <typename CondType, typename GeneratorType>
ConditionalGenerator(CondType, GeneratorType, GeneratorType) -> ConditionalGenerator<CondType, GeneratorType>;
