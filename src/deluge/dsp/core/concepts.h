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
#include <span>

namespace deluge::dsp {
/// @brief A concept for generators that render a block of samples.
template <typename T>
concept block_generator = requires(T t, std::span<typename T::value_type> buffer) {
	{ t.renderBlock(buffer) };
};

/// @brief A concept for generators that render a single sample at a time.
template <typename T>
concept sample_generator = requires(T t) {
	{ t.render() } -> std::same_as<typename T::value_type>;
};

/// @brief A concept for processors that render a block of samples.
template <typename T>
concept block_processor =
    requires(T t, std::span<typename T::value_type> input, std::span<typename T::value_type> output) {
	    { t.renderBlock(input, output) };
    };

/// @brief A concept for processors that render a single sample at a time.
template <typename T>
concept sample_processor = requires(T t, typename T::value_type sample) {
	{ t.render(sample) } -> std::same_as<typename T::value_type>;
};

/// @brief A concept for processors that render a block of samples using SIMD operations.
template <typename T>
concept block_adapter =
    requires(T t, std::span<typename T::input_type> input, std::span<typename T::output_type> output) {
	    { t.renderBlock(input, output) };
    };

/// @brief A concept for mixers that render a block of samples.
template <typename T>
concept block_mixer = requires(T t, std::span<typename T::value_type> input_a,
                               std::span<typename T::value_type> input_b, std::span<typename T::value_type> output) {
	{ t.renderBlock(input_a, input_b, output) };
};

/// @brief A concept for mixers that render a single sample at a time.
template <typename T>
concept sample_mixer = requires(T t, typename T::value_type input_a, typename T::value_type input_b) {
	{ t.render(input_a, input_b) } -> std::same_as<typename T::value_type>;
};
} // namespace deluge::dsp
