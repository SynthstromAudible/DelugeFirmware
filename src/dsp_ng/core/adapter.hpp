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
#include "dsp_ng/core/converter.hpp"
#include "dsp_ng/core/processor.hpp"
#include "util.hpp"
#include <type_traits>

/// @file An adapter is a specialization of a processor that wraps another processor, converting between an
/// "inner" type and an "outer" type. This allows for floating-point processors in fixed-point pipelines
/// and vice versa.

namespace deluge::dsp {
/// @class Adapter
/// @brief A class that adapts a processor to work in a pipeline of a different type.
///
/// The Adapter class is a specialization of a processor that wraps another processor, converting between an
/// "inner" type and an "outer" type. This allows for floating-point processors in fixed-point pipelines
/// and vice versa.
///
/// @tparam T The type of the input samples.
/// @tparam ProcessorType The type of the processor
///
/// @details
/// The Adapter class is a template that takes two types: `T` and `ProcessorType`. The `T` type is the type of the input
/// samples, while the `ProcessorType` is the type of the processor. The Adapter class inherits from both `Converter<T,
/// typename ProcessorType::value_type>` and `Processor<T>`, allowing it to convert between the two types and process
/// the samples.
///
/// @note The processor can be a pointer type, in which case it will be dereferenced when used.
template <typename T, typename ProcessorType>
class Adapter : public Converter<T, typename std::remove_pointer_t<ProcessorType>::value_type>, public Processor<T> {
public:
	using outer_type = T;                                                         ///< The type of the outer samples
	using inner_type = typename std::remove_pointer_t<ProcessorType>::value_type; ///< The type of the inner samples

	// There's no need to convert between samples of the same type.
	static_assert(!std::is_same_v<outer_type, inner_type>,
	              "Adapter requires different types for source and destination.");

	/// @brief Constructor for the BlockAdapter class.
	/// @param processor The processor to be contained in the adapter.
	Adapter(ProcessorType processor) : processor_(processor) {}

	/// @brief Renders a sample by converting it to the inner type, processing it, and converting it back to the outer
	/// type.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	[[gnu::always_inline]] T render(T sample) final {
		auto converted = converter_.render(sample);
		auto output = util::dereference(processor_).render(converted);
		return converter_.render(output);
	}

private:
	ProcessorType processor_;                     //< The contained processor (can be a pointer)
	Converter<outer_type, inner_type> converter_; //< The converter
};

/// @copydoc Adapter
template <typename T, typename ProcessorType>
class Adapter<Argon<T>, ProcessorType>
    : public Converter<Argon<T>, Argon<typename std::remove_pointer_t<ProcessorType>::value_type>>,
      public Processor<Argon<T>> {
public:
	using from_type = Argon<T>;
	using to_type = Argon<typename std::remove_pointer_t<ProcessorType>::value_type>;

	static_assert(!std::is_same_v<from_type, to_type>, "Adapter requires different types for source and destination.");

	/// @brief Constructor for the BlockAdapter class.
	/// @param processor The processor to be contained in the adapter.
	Adapter(ProcessorType processor) : processor_{processor} {}

	/// @brief Renders a sample by converting it to the inner type, processing it, and converting it back to the outer
	/// type.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	[[gnu::always_inline]] Argon<T> render(Argon<T> sample) final {
		auto converted = converter_.render(sample);
		auto output = util::dereference(processor_).render(converted);
		return converter_.render(output);
	}

private:
	ProcessorType processor_;                 ///< The contained processor (can be a pointer)
	Converter<from_type, to_type> converter_; ///< The converter
};
} // namespace deluge::dsp
