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

#include "dsp-next/core/stereo_vector.h"
#include "dsp-next/core/types.h"
#include <argon.hpp>
#include <argon/vectorize/for_each.hpp>
#include <concepts>
#include <cstddef>
#include <type_traits>

namespace deluge::dsp {

/// @brief Virtual block-level generator interface, for a generator chosen at runtime.
/// @details Called once per block, so the virtual cost is negligible and per-sample dispatch is never
/// virtual. A concrete generator reaches it only through @ref AsBlock.
template <typename T>
struct BlockGenerator {
	using value_type = T; ///< The sample type of the block.
	virtual ~BlockGenerator() = default;
	/// @brief Fill @p output with the next `output.size()` samples.
	virtual void render_block(Buffer<T> output) = 0;
};

namespace detail {

/// @brief @p D renders a vector of arithmetic @p T per call.
template <typename D, typename T>
concept simd_generator_render = std::is_arithmetic_v<T> && requires(D d) {
	{ d.render() } -> std::same_as<Argon<T>>;
};

/// @brief @p D renders a vector of stereo frames per call.
template <typename D, typename T>
concept simd_stereo_generator_render = std::same_as<T, StereoSample> && requires(D d) {
	{ d.render() } -> std::same_as<StereoVector>;
};

/// @brief @p D renders one @p T per call.
template <typename D, typename T>
concept scalar_generator_render = requires(D d) {
	{ d.render() } -> std::same_as<T>;
};

} // namespace detail

/// @brief CRTP mixin: builds a generator's `render_block` from its `render()`.
/// @tparam Derived the component, which defines `render()`.
/// @tparam T the sample type of the block (`q31`, `f32` or @ref StereoSample).
/// @details Non-virtual: the mixin adds no vptr and no state. The path is chosen from the signature of
/// `Derived::render()`:
/// - SIMD, when it returns `Argon<T>`: one `render()` per vector, through `argon::vectorize::for_each`.
/// - SIMD stereo, when `T` is @ref StereoSample and it returns @ref StereoVector: one `render()` per vector
///   of frames, interleaved on store through `argon::vectorize::for_each_interleaved`.
/// - Scalar, when it returns `T`: one `render()` per sample.
///
/// Every sample of the block is written. On the SIMD paths a block that is not a lane multiple ends with one
/// partial step: `render()` runs once more and only the lanes inside the block are stored. `render()` is
/// therefore called exactly `ceil(size / lanes)` times, so a generator's state (its phase) advances by whole
/// vectors, the partial one included.
///
/// The per-step lambdas are `[[gnu::always_inline]]` and the scalar path is a plain index loop, so nothing
/// sits between the block loop and `render` but the component's own code. The app's translation units build
/// with `-fno-inline-functions`, under which clang marks every function definition noinline unless it says
/// otherwise; a component's `render` is `[[gnu::always_inline]]` for the same reason.
template <typename Derived, typename T>
struct Generator {
	using value_type = T; ///< The sample type of the block.

	/// @brief Fill @p output with the next `output.size()` samples, every one of them.
	void render_block(Buffer<T> output) {
		auto& self = static_cast<Derived&>(*this);
		const std::size_t n = output.size();
		if constexpr (detail::simd_generator_render<Derived, T>) {
			const auto size = static_cast<std::ptrdiff_t>(n);
			T* out = output.data();
			argon::vectorize::for_each<T>(size,
			                              [&] [[gnu::always_inline]] (auto step) { step.Store(out, self.render()); });
		}
		else if constexpr (detail::simd_stereo_generator_render<Derived, T>) {
			const auto size = static_cast<std::ptrdiff_t>(n);
			q31* out = detail::interleaved(output);
			argon::vectorize::for_each_interleaved<q31, 2>(size, [&] [[gnu::always_inline]] (auto step) {
				const StereoVector frames = self.render();
				step.Store(out, {frames.l, frames.r});
			});
		}
		else {
			static_assert(detail::scalar_generator_render<Derived, T>,
			              "a Generator defines render() returning Argon<T>, StereoVector or T");
			T* out = output.data();
			for (std::size_t i = 0; i < n; ++i) {
				out[i] = self.render();
			}
		}
	}
};

} // namespace deluge::dsp
