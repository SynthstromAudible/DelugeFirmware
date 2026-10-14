/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/generator.h"
#include "dsp-next/core/mixer.h"
#include "dsp-next/core/processor.h"
#include "dsp-next/core/types.h"
#include <utility>

/// @file
/// @brief AsBlock: the one route from a concrete component to virtual dispatch.
///
/// Components are concrete, non-virtual types. Where one must be chosen at runtime, the owner wraps it in an
/// AsBlock and holds it through the matching virtual interface (@ref BlockGenerator, @ref BlockProcessor or
/// @ref BlockMixer). The block call is virtual; everything inside it stays inlined.
namespace deluge::dsp {

/// @brief Wraps a component @p C in the virtual block interface its concept names.
/// @details Defined only for a @ref block_generator, @ref block_processor or @ref block_mixer.
template <typename C>
class AsBlock;

/// @brief A @ref block_generator behind @ref BlockGenerator.
template <block_generator C>
class AsBlock<C> final : public BlockGenerator<typename C::value_type> {
public:
	using value_type = typename C::value_type; ///< The sample type of the block.

	/// @brief Wrap @p component, held by value.
	explicit AsBlock(C component) : component_{std::move(component)} {}

	/// @brief The wrapped component, for its owner to configure.
	[[nodiscard]] C& component() { return component_; }
	/// @brief The wrapped component.
	[[nodiscard]] const C& component() const { return component_; }

	/// @brief The component's renderBlock.
	void renderBlock(Buffer<value_type> output) override { component_.renderBlock(output); }

private:
	C component_;
};

/// @brief A @ref block_processor behind @ref BlockProcessor.
template <block_processor C>
class AsBlock<C> final : public BlockProcessor<typename C::value_type> {
public:
	using value_type = typename C::value_type; ///< The sample type of the blocks.

	/// @brief Wrap @p component, held by value.
	explicit AsBlock(C component) : component_{std::move(component)} {}

	/// @brief The wrapped component, for its owner to configure.
	[[nodiscard]] C& component() { return component_; }
	/// @brief The wrapped component.
	[[nodiscard]] const C& component() const { return component_; }

	/// @brief The component's renderBlock.
	void renderBlock(Signal<value_type> input, Buffer<value_type> output) override {
		component_.renderBlock(input, output);
	}

private:
	C component_;
};

/// @brief A @ref block_mixer behind @ref BlockMixer.
template <block_mixer C>
class AsBlock<C> final : public BlockMixer<typename C::value_type> {
public:
	using value_type = typename C::value_type; ///< The sample type of the blocks.

	/// @brief Wrap @p component, held by value.
	explicit AsBlock(C component) : component_{std::move(component)} {}

	/// @brief The wrapped component, for its owner to configure.
	[[nodiscard]] C& component() { return component_; }
	/// @brief The wrapped component.
	[[nodiscard]] const C& component() const { return component_; }

	/// @brief The component's renderBlock.
	void renderBlock(Signal<value_type> input_a, Signal<value_type> input_b, Buffer<value_type> output) override {
		component_.renderBlock(input_a, input_b, output);
	}

private:
	C component_;
};

} // namespace deluge::dsp
