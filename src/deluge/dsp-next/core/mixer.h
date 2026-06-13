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
#include <argon.hpp>
#include <argon/vectorize/load.hpp>
#include <argon/vectorize/store.hpp>

namespace deluge::dsp {

/// @brief Virtual block-level mixer interface (two input blocks -> one output block).
/// @details The VCA is a mixer in this sense: it scales a raw wave block and sums it
/// into the existing mix (out = raw * amplitude + mix), reading both inputs.
template <typename T>
struct BlockMixer {
	using value_type = T;
	virtual ~BlockMixer() = default;
	virtual void renderBlock(Signal<T> input_a, Signal<T> input_b, Buffer<T> output) = 0;
};

/// @brief CRTP mixin: a SIMD mixer defining a static `Argon<T> render(Argon<T>, Argon<T>)`
/// gets a vectorised renderBlock for free. `render()` stays non-virtual and inlinable.
template <typename Derived, typename T>
struct Mixer : BlockMixer<T> {
	void renderBlock(Signal<T> input_a, Signal<T> input_b, Buffer<T> output) override {
		auto a_view = argon::vectorize::load(input_a);
		auto b_view = argon::vectorize::load(input_b);
		auto out_view = argon::vectorize::store(output);
		auto a_it = a_view.begin();
		auto b_it = b_view.begin();
		auto out_it = out_view.begin();
		for (; out_it != out_view.end(); ++a_it, ++b_it, ++out_it) {
			*out_it = static_cast<Derived*>(this)->render(*a_it, *b_it);
		}
	}
};

} // namespace deluge::dsp
