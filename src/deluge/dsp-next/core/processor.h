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

/// @brief Virtual block-level processor interface (input block to output block), for a processor chosen at
/// runtime.
/// @details A concrete processor reaches it only through @ref AsBlock.
template <typename T>
struct BlockProcessor {
	using value_type = T; ///< The sample type of the blocks.
	virtual ~BlockProcessor() = default;
	/// @brief Process `output.size()` samples of @p input into @p output, which may be the same buffer.
	virtual void render_block(Signal<T> input, Buffer<T> output) = 0;
};

namespace detail {

/// @brief @p D processes a vector of arithmetic @p T per call.
template <typename D, typename T>
concept simd_processor_render = std::is_arithmetic_v<T> && requires(D d, Argon<T> v) {
	{ d.render(v) } -> std::same_as<Argon<T>>;
};

/// @brief @p D processes a vector of stereo frames per call.
template <typename D, typename T>
concept simd_stereo_processor_render = std::same_as<T, StereoSample> && requires(D d, StereoVector v) {
	{ d.render(v) } -> std::same_as<StereoVector>;
};

/// @brief @p D processes one @p T per call.
template <typename D, typename T>
concept scalar_processor_render = requires(D d, T s) {
	{ d.render(s) } -> std::same_as<T>;
};

} // namespace detail

/// @brief CRTP mixin: builds a processor's `render_block` from its `render(...)`.
/// @tparam Derived the component, which defines `render(...)`.
/// @tparam T the sample type of the blocks (`q31`, `f32` or @ref StereoSample).
/// @details Non-virtual: the mixin adds no vptr and no state. The path is chosen from the signature of
/// `Derived::render`:
/// - SIMD, when it takes and returns `Argon<T>`: one `render` per vector, through `argon::vectorize::for_each`.
/// - SIMD stereo, when `T` is @ref StereoSample and it takes and returns @ref StereoVector: one `render` per
///   vector of frames, de-interleaved on load and re-interleaved on store through
///   `argon::vectorize::for_each_interleaved`.
/// - Scalar, when it takes and returns `T`: one `render` per sample.
///
/// Every sample of the block is written. On the SIMD paths a block that is not a lane multiple ends with one
/// partial step: lanes past the end load as zero, go through `render` with the rest, and are not stored.
/// Each step loads before it stores, so @p input and @p output may be the same buffer.
///
/// The per-step lambdas are `[[gnu::always_inline]]` and the scalar path is a plain index loop, so nothing
/// sits between the block loop and `render` but the component's own code. The app's translation units build
/// with `-fno-inline-functions`, under which clang marks every function definition noinline unless it says
/// otherwise; a component's `render` is `[[gnu::always_inline]]` for the same reason.
template <typename Derived, typename T>
struct Processor {
	using value_type = T; ///< The sample type of the blocks.

	/// @brief Process `output.size()` samples of @p input into @p output, every one of them.
	/// @param input at least `output.size()` samples; may be the same buffer as @p output.
	void render_block(Signal<T> input, Buffer<T> output) {
		auto& self = static_cast<Derived&>(*this);
		const std::size_t n = output.size();
		if constexpr (detail::simd_processor_render<Derived, T>) {
			const auto size = static_cast<std::ptrdiff_t>(n);
			const T* in = input.data();
			T* out = output.data();
			argon::vectorize::for_each<T>(
			    size, [&] [[gnu::always_inline]] (auto step) { step.Store(out, self.render(step.Load(in))); });
		}
		else if constexpr (detail::simd_stereo_processor_render<Derived, T>) {
			const auto size = static_cast<std::ptrdiff_t>(n);
			const q31* in = detail::interleaved(input);
			q31* out = detail::interleaved(output);
			argon::vectorize::for_each_interleaved<q31, 2>(size, [&] [[gnu::always_inline]] (auto step) {
				const StereoVector frames = self.render(detail::to_stereo(step.Load(in)));
				step.Store(out, {frames.l, frames.r});
			});
		}
		else {
			static_assert(detail::scalar_processor_render<Derived, T>,
			              "a Processor defines render taking and returning Argon<T>, StereoVector or T");
			const T* in = input.data();
			T* out = output.data();
			for (std::size_t i = 0; i < n; ++i) {
				out[i] = self.render(in[i]);
			}
		}
	}
};

} // namespace deluge::dsp
