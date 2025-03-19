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
#include "dsp/core/processor.h"
#include <argon.hpp>
#include <type_traits>

/// @brief ConditionalProcessor is a processor that conditionally applies another processor based on a given condition.
/// @tparam CondType The type of the condition (e.g., a boolean or a callable).
/// @tparam ProcessorType The type of the processor to apply if the condition is true.
template <typename CondType, typename ProcessorType, typename = void>
struct ConditionalProcessor; // Forward declaration

template <typename CondType, typename ProcessorType>
struct ConditionalProcessor<
    CondType, ProcessorType,
    std::enable_if_t<std::is_base_of_v<Processor<typename ProcessorType::value_type>, ProcessorType>>> : ProcessorType {
	CondType condition_; // Condition to apply the processors

	using value_type = typename ProcessorType::value_type;

	ConditionalProcessor(CondType condition, ProcessorType processor)
	    : Processor<value_type>{processor}, condition_(condition) {}

	/// @brief Process a block of samples using SIMD operations, conditionally applying processors.
	/// @param samples The input buffer of samples to process.
	/// @param condition A boolean indicating whether to apply the processors.
	/// @return The processed buffer of samples.
	Argon<value_type> process(Argon<value_type> sample) override {
		if (condition_()) {                                // Check the condition
			return Processor<value_type>::process(sample); // Apply the processor if condition is true
		}
		return sample; // Return the original sample if condition is false
	}
};

template <typename ProcessorType>
struct ConditionalProcessor<
    bool, ProcessorType,
    std::enable_if_t<std::is_base_of_v<Processor<typename ProcessorType::value_type>, ProcessorType>>> : ProcessorType {
	bool condition_; // Condition to apply the processors

	using value_type = typename ProcessorType::value_type;

	ConditionalProcessor(bool condition, ProcessorType processor)
	    : Processor<value_type>{processor}, condition_(condition) {}

	/// @brief Process a block of samples using SIMD operations, conditionally applying processors.
	/// @param samples The input buffer of samples to process.
	/// @param condition A boolean indicating whether to apply the processors.
	/// @return The processed buffer of samples.
	Argon<value_type> process(Argon<value_type> sample) override {
		if (condition_) {                                  // Check the condition
			return Processor<value_type>::process(sample); // Apply the processor if condition is true
		}
		return sample; // Return the original sample if condition is false
	}
};

template <typename CondType, typename ProcessorType>
struct ConditionalProcessor<
    CondType, ProcessorType,
    std::enable_if_t<std::is_base_of_v<SIMDProcessor<typename ProcessorType::value_type>, ProcessorType>>>
    : ProcessorType {
	CondType condition_; // Condition to apply the processors

	using value_type = typename ProcessorType::value_type;

	ConditionalProcessor(CondType condition, ProcessorType processor)
	    : ProcessorType{processor}, condition_(condition) {}

	/// @brief Process a block of samples using SIMD operations, conditionally applying processors.
	/// @param samples The input buffer of samples to process.
	/// @param condition A boolean indicating whether to apply the processors.
	/// @return The processed buffer of samples.
	Argon<value_type> process(Argon<value_type> sample) override {
		if (condition_()) {                        // Check the condition
			return ProcessorType::process(sample); // Apply the processor if condition is true
		}
		return sample; // Return the original sample if condition is false
	}
};

template <typename ProcessorType>
struct ConditionalProcessor<
    bool, ProcessorType,
    std::enable_if_t<std::is_base_of_v<SIMDProcessor<typename ProcessorType::value_type>, ProcessorType>>>
    : ProcessorType {
	bool condition_; // Condition to apply the processors

	using value_type = typename ProcessorType::value_type;

	ConditionalProcessor(bool condition, ProcessorType processor) : ProcessorType{processor}, condition_(condition) {}

	/// @brief Process a block of samples using SIMD operations, conditionally applying processors.
	/// @param samples The input buffer of samples to process.
	/// @param condition A boolean indicating whether to apply the processors.
	/// @return The processed buffer of samples.
	Argon<value_type> process(Argon<value_type> sample) override {
		if (condition_) {                          // Check the condition
			return ProcessorType::process(sample); // Apply the processor if condition is true
		}
		return sample; // Return the original sample if condition is false
	}
};

template <typename ProcessorType>
ConditionalProcessor(bool, ProcessorType) -> ConditionalProcessor<bool, ProcessorType>;

template <typename CondType, typename ProcessorType>
ConditionalProcessor(CondType, ProcessorType) -> ConditionalProcessor<CondType, ProcessorType>;
