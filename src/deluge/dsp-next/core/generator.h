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
#include <algorithm>
#include <argon.hpp>
#include <argon/vectorize/store.hpp>
#include <cstddef>

namespace deluge::dsp {

/// @brief Virtual block-level generator interface — the runtime dispatch point.
/// @details The OscType registry holds a `BlockGenerator<q31>*` and calls renderBlock()
/// once per block (~32-128 samples), so the virtual cost is negligible and per-sample
/// dispatch is never virtual. Concrete generators get this via the @ref Generator mixin.
template <typename T>
struct BlockGenerator {
	using value_type = T;
	virtual ~BlockGenerator() = default;
	virtual void renderBlock(Buffer<T> output) = 0;
};

/// @brief CRTP mixin: a SIMD generator defining a static `Argon<T> render()` (advancing
/// its own phase) gets a vectorised renderBlock for free and satisfies BlockGenerator.
/// @details `render()` stays non-virtual and inlinable — the hot path. A generator that
/// needs an exact, non-lane-aligned phase advance across the block overrides renderBlock.
///
/// The fill is **tail-safe**: full lane-width chunks are stored directly, then any
/// remaining `size % lanes` samples are produced by one more `render()` and stored
/// masked (only the valid lanes). This matters for arbitrary-length windows (e.g. osc
/// sync) — `argon::vectorize::store` alone rounds the size *down* to a lane multiple and
/// would silently drop the tail. For lane-multiple blocks (the common audio-block case)
/// there is no tail and the result is identical.
template <typename Derived, typename T>
struct Generator : BlockGenerator<T> {
	static constexpr std::size_t kLanes = argon::vectorize::store<T>::lanes;

	void renderBlock(Buffer<T> output) override {
		Derived* self = static_cast<Derived*>(this);
		T* out = output.data();
		std::size_t size = output.size();
		std::size_t whole = size & ~(kLanes - 1);

		for (std::size_t i = 0; i < whole; i += kLanes) {
			self->render().StoreTo(out + i);
		}
		if (whole != size) {
			T tail[kLanes];
			self->render().StoreTo(tail);
			std::copy_n(tail, size - whole, out + whole);
		}
	}
};

} // namespace deluge::dsp
