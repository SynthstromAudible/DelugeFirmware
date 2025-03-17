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

template <typename T>
concept block_generator = requires(T t, std::span<typename T::value_type> buffer) {
	{ t.processBlock(buffer) };
};

template <typename T>
concept sample_generator = requires(T t) {
	{ t.process() } -> std::same_as<typename T::value_type>;
};

template <typename T>
concept block_processor =
    requires(T t, std::span<typename T::value_type> input, std::span<typename T::value_type> output) {
	    { t.processBlock(input, output) };
    };

template <typename T>
concept sample_processor = requires(T t, typename T::value_type sample) {
	{ t.process(sample) } -> std::same_as<typename T::value_type>;
};

template <typename T>
concept block_adapter =
    requires(T t, std::span<typename T::input_type> input, std::span<typename T::output_type> output) {
	    { t.processBlock(input, output) };
    };
