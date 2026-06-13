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

#include "dsp-next/core/types.h"
#include <concepts>

/// @file
/// @brief The component concepts: generator, processor, mixer, converter.
///
/// These describe the *static* interface a component exposes — a per-sample/vector
/// `render(...)` (the inlined hot path) and a block `renderBlock(...)`. Runtime
/// selection (the OscType registry) is done through the separate virtual block
/// interfaces (BlockGenerator / BlockProcessor / BlockMixer), not these concepts.
namespace deluge::dsp {

/// @brief Fills a block of samples it generates itself (e.g. an oscillator).
template <typename G>
concept block_generator = requires(G g, Buffer<typename G::value_type> out) {
	{ g.renderBlock(out) } -> std::same_as<void>;
};

/// @brief Generates one sample (or vector of samples) per call.
template <typename G>
concept sample_generator = requires(G g) {
	{ g.render() } -> std::same_as<typename G::value_type>;
};

template <typename G>
concept generator = block_generator<G> && sample_generator<G>;

/// @brief Transforms an input block into an output block (e.g. a filter, the VCA).
template <typename P>
concept block_processor =
    requires(P p, Signal<typename P::value_type> in, Buffer<typename P::value_type> out) {
	    { p.renderBlock(in, out) } -> std::same_as<void>;
    };

/// @brief Transforms one sample (or vector) per call.
template <typename P>
concept sample_processor = requires(P p, typename P::value_type s) {
	{ p.render(s) } -> std::same_as<typename P::value_type>;
};

template <typename P>
concept processor = block_processor<P> && sample_processor<P>;

/// @brief Combines two input blocks into one output block (e.g. unison/osc summing).
template <typename M>
concept block_mixer = requires(M m, Signal<typename M::value_type> a, Signal<typename M::value_type> b,
                               Buffer<typename M::value_type> out) {
	{ m.renderBlock(a, b, out) } -> std::same_as<void>;
};

/// @brief Combines two samples (or vectors) per call.
template <typename M>
concept sample_mixer = requires(M m, typename M::value_type a, typename M::value_type b) {
	{ m.render(a, b) } -> std::same_as<typename M::value_type>;
};

template <typename M>
concept mixer = block_mixer<M> && sample_mixer<M>;

/// @brief Converts a sample from one numeric domain to another (e.g. q31 <-> f32).
template <typename C>
concept converter = requires(C c, typename C::input_type s) {
	{ c.render(s) } -> std::same_as<typename C::output_type>;
};

} // namespace deluge::dsp
