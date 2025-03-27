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
#include <functional> // For std::invoke
#include <optional>   // For std::optional
#include <type_traits>

namespace deluge::dsp {

/// @brief ConditionalProcessor is a processor that conditionally applies another processor based on a given condition.
/// @tparam CondType The type of the condition (e.g., a boolean or a callable).
/// @tparam ProcessorType The type of the processor to apply if the condition is true.
template <typename CondType, typename ProcessorType, typename = void>
struct ConditionalProcessor; // Forward declaration

/// @copydoc ConditionalProcessor
template <typename CondType, typename ProcessorType>
struct ConditionalProcessor<
    CondType, ProcessorType,
    std::enable_if_t<std::is_base_of_v<Processor<typename std::remove_pointer_t<ProcessorType>::value_type>,
                                       std::remove_pointer_t<ProcessorType>>>>
    final : Processor<typename std::remove_pointer_t<ProcessorType>::value_type> {
	CondType condition_;                         // Condition to apply the processors
	ProcessorType processor_;                    // Contained processor (can be a pointer)
	std::optional<ProcessorType> elseProcessor_; // Optional else processor (can be a pointer)

	using value_type = typename std::remove_pointer_t<ProcessorType>::value_type;

	ConditionalProcessor(CondType condition, ProcessorType processor) : condition_(condition), processor_(processor) {}
	ConditionalProcessor(CondType condition, ProcessorType processor, ProcessorType elseProcessor)
	    : condition_(condition), processor_(processor), elseProcessor_(elseProcessor) {}

	/// @brief Process a block of samples using SIMD operations, conditionally applying processors.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	[[gnu::always_inline]] value_type render(value_type sample) final {
		if (condition_()) { // Check the condition
			return std::invoke(static_cast<value_type (std::remove_pointer_t<ProcessorType>::*)(value_type)>(
			                       &std::remove_pointer_t<ProcessorType>::render),
			                   processor_, sample); // Apply the processor
		}
		if (elseProcessor_) { // Check if else processor exists
			return std::invoke(static_cast<value_type (std::remove_pointer_t<ProcessorType>::*)(value_type)>(
			                       &std::remove_pointer_t<ProcessorType>::render),
			                   *elseProcessor_, sample); // Apply the else processor
		}
		return sample; // Return the original sample if no processors are applied
	}
};

template <typename ProcessorType>
struct ConditionalProcessor<
    bool, ProcessorType,
    std::enable_if_t<std::is_base_of_v<Processor<typename std::remove_pointer_t<ProcessorType>::value_type>,
                                       std::remove_pointer_t<ProcessorType>>>>
    final : Processor<typename std::remove_pointer_t<ProcessorType>::value_type> {
	bool condition_;                             // Condition to apply the processors
	ProcessorType processor_;                    // Contained processor (can be a pointer)
	std::optional<ProcessorType> elseProcessor_; // Optional else processor (can be a pointer)

	using value_type = typename std::remove_pointer_t<ProcessorType>::value_type;

	ConditionalProcessor(bool condition, ProcessorType processor) : condition_(condition), processor_(processor) {}
	ConditionalProcessor(bool condition, ProcessorType processor, ProcessorType elseProcessor)
	    : condition_(condition), processor_(processor), elseProcessor_(elseProcessor) {}

	/// @brief Process a block of samples using SIMD operations, conditionally applying processors.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	[[gnu::always_inline]] value_type render(value_type sample) final {
		if (condition_) { // Check the condition
			return std::invoke(static_cast<value_type (std::remove_pointer_t<ProcessorType>::*)(value_type)>(
			                       &std::remove_pointer_t<ProcessorType>::render),
			                   processor_, sample); // Apply the processor
		}
		if (elseProcessor_.has_value()) { // Check if else processor exists
			return std::invoke(static_cast<value_type (std::remove_pointer_t<ProcessorType>::*)(value_type)>(
			                       &std::remove_pointer_t<ProcessorType>::render),
			                   *elseProcessor_, sample); // Apply the else processor
		}
		return sample; // Return the original sample if no processors are applied
	}
};

template <typename CondType, typename ProcessorType>
struct ConditionalProcessor<
    CondType, ProcessorType,
    std::enable_if_t<std::is_base_of_v<SIMDProcessor<typename std::remove_pointer_t<ProcessorType>::value_type>,
                                       std::remove_pointer_t<ProcessorType>>>>
    final : SIMDProcessor<typename std::remove_pointer_t<ProcessorType>::value_type> {
	CondType condition_;                         // Condition to apply the processors
	ProcessorType processor_;                    // Contained processor (can be a pointer)
	std::optional<ProcessorType> elseProcessor_; // Optional else processor (can be a pointer)

	using value_type = typename std::remove_pointer_t<ProcessorType>::value_type;

	ConditionalProcessor(CondType condition, ProcessorType processor) : condition_(condition), processor_(processor) {}
	ConditionalProcessor(CondType condition, ProcessorType processor, ProcessorType elseProcessor)
	    : condition_(condition), processor_(processor), elseProcessor_(elseProcessor) {}

	/// @brief Process a block of samples using SIMD operations, conditionally applying processors.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	[[gnu::always_inline]] Argon<value_type> render(Argon<value_type> sample) final {
		if (condition_()) { // Check the condition
			return std::invoke(
			    static_cast<Argon<value_type> (std::remove_pointer_t<ProcessorType>::*)(Argon<value_type>)>(
			        &std::remove_pointer_t<ProcessorType>::render),
			    processor_, sample); // Apply the processor
		}
		if (elseProcessor_.has_value()) { // Check if else processor exists
			return std::invoke(
			    static_cast<Argon<value_type> (std::remove_pointer_t<ProcessorType>::*)(Argon<value_type>)>(
			        &std::remove_pointer_t<ProcessorType>::render),
			    *elseProcessor_, sample); // Apply the else processor
		}
		return sample; // Return the original sample if no processors are applied
	}
};

template <typename ProcessorType>
struct ConditionalProcessor<
    bool, ProcessorType,
    std::enable_if_t<std::is_base_of_v<SIMDProcessor<typename std::remove_pointer_t<ProcessorType>::value_type>,
                                       std::remove_pointer_t<ProcessorType>>>>
    final : SIMDProcessor<typename std::remove_pointer_t<ProcessorType>::value_type> {
	bool condition_;                             // Condition to apply the processors
	ProcessorType processor_;                    // Contained processor (can be a pointer)
	std::optional<ProcessorType> elseProcessor_; // Optional else processor (can be a pointer)

	using value_type = typename std::remove_pointer_t<ProcessorType>::value_type;

	ConditionalProcessor(bool condition, ProcessorType processor) : condition_(condition), processor_(processor) {}
	ConditionalProcessor(bool condition, ProcessorType processor, ProcessorType elseProcessor)
	    : condition_(condition), processor_(processor), elseProcessor_(elseProcessor) {}

	/// @brief Process a block of samples using SIMD operations, conditionally applying processors.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	[[gnu::always_inline]] Argon<value_type> render(Argon<value_type> sample) final {
		if (condition_) { // Check the condition
			return std::invoke(
			    static_cast<Argon<value_type> (std::remove_pointer_t<ProcessorType>::*)(Argon<value_type>)>(
			        &std::remove_pointer_t<ProcessorType>::render),
			    processor_, sample); // Apply the processor
		}
		if (elseProcessor_) { // Check if else processor exists
			return std::invoke(
			    static_cast<Argon<value_type> (std::remove_pointer_t<ProcessorType>::*)(Argon<value_type>)>(
			        &std::remove_pointer_t<ProcessorType>::render),
			    *elseProcessor_, sample); // Apply the else processor
		}
		return sample; // Return the original sample if no processors are applied
	}
};

template <typename ProcessorType>
ConditionalProcessor(bool, ProcessorType) -> ConditionalProcessor<bool, ProcessorType>;

template <typename CondType, typename ProcessorType>
ConditionalProcessor(CondType, ProcessorType) -> ConditionalProcessor<CondType, ProcessorType>;

template <typename ProcessorType>
ConditionalProcessor(bool, ProcessorType, ProcessorType) -> ConditionalProcessor<bool, ProcessorType>;

template <typename CondType, typename ProcessorType>
ConditionalProcessor(CondType, ProcessorType, ProcessorType) -> ConditionalProcessor<CondType, ProcessorType>;
} // namespace deluge::dsp
