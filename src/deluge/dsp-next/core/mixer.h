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

/// @brief Virtual block-level mixer interface (two input blocks to one output block), for a mixer chosen at
/// runtime.
/// @details The VCA is a mixer in this sense: it scales a raw wave block and sums it into the existing mix
/// (out = raw * amplitude + mix), reading both inputs. A concrete mixer reaches it only through @ref AsBlock.
template <typename T>
struct BlockMixer {
	using value_type = T; ///< The sample type of the blocks.
	virtual ~BlockMixer() = default;
	/// @brief Combine `output.size()` samples of @p input_a and @p input_b into @p output, which may be the same
	/// buffer as either input.
	virtual void renderBlock(Signal<T> input_a, Signal<T> input_b, Buffer<T> output) = 0;
};

namespace detail {

/// @brief @p D combines two vectors of arithmetic @p T per call.
template <typename D, typename T>
concept simd_mixer_render = std::is_arithmetic_v<T> && requires(D d, Argon<T> a, Argon<T> b) {
	{ d.render(a, b) } -> std::same_as<Argon<T>>;
};

/// @brief @p D combines two vectors of stereo frames per call.
template <typename D, typename T>
concept simd_stereo_mixer_render = std::same_as<T, StereoSample> && requires(D d, StereoVector a, StereoVector b) {
	{ d.render(a, b) } -> std::same_as<StereoVector>;
};

/// @brief @p D combines two @p T per call.
template <typename D, typename T>
concept scalar_mixer_render = requires(D d, T a, T b) {
	{ d.render(a, b) } -> std::same_as<T>;
};

} // namespace detail

/// @brief CRTP mixin: builds a mixer's `renderBlock` from its `render(a, b)`.
/// @tparam Derived the component, which defines `render(a, b)`.
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
/// Each step loads before it stores, so @p output may be the same buffer as either input.
///
/// The per-step lambdas are `[[gnu::always_inline]]` and the scalar path is a plain index loop, so nothing
/// sits between the block loop and `render` but the component's own code. The app's translation units build
/// with `-fno-inline-functions`, under which clang marks every function definition noinline unless it says
/// otherwise; a component's `render` is `[[gnu::always_inline]]` for the same reason.
template <typename Derived, typename T>
struct Mixer {
	using value_type = T; ///< The sample type of the blocks.

	/// @brief Combine `output.size()` samples of @p input_a and @p input_b into @p output, every one of them.
	/// @param input_a,input_b at least `output.size()` samples each; either may be the same buffer as @p output.
	void renderBlock(Signal<T> input_a, Signal<T> input_b, Buffer<T> output) {
		auto& self = static_cast<Derived&>(*this);
		const std::size_t n = output.size();
		if constexpr (detail::simd_mixer_render<Derived, T>) {
			const auto size = static_cast<std::ptrdiff_t>(n);
			const T* a = input_a.data();
			const T* b = input_b.data();
			T* out = output.data();
			argon::vectorize::for_each<T>(size, [&] [[gnu::always_inline]] (auto step) {
				step.Store(out, self.render(step.Load(a), step.Load(b)));
			});
		}
		else if constexpr (detail::simd_stereo_mixer_render<Derived, T>) {
			const auto size = static_cast<std::ptrdiff_t>(n);
			const q31* a = detail::interleaved(input_a);
			const q31* b = detail::interleaved(input_b);
			q31* out = detail::interleaved(output);
			argon::vectorize::for_each_interleaved<q31, 2>(size, [&] [[gnu::always_inline]] (auto step) {
				const auto [a_l, a_r] = step.Load(a);
				const auto [b_l, b_r] = step.Load(b);
				const StereoVector frames =
				    self.render(StereoVector{.l = a_l, .r = a_r}, StereoVector{.l = b_l, .r = b_r});
				step.Store(out, {frames.l, frames.r});
			});
		}
		else {
			static_assert(detail::scalar_mixer_render<Derived, T>,
			              "a Mixer defines render(a, b) taking and returning Argon<T>, StereoVector or T");
			const T* a = input_a.data();
			const T* b = input_b.data();
			T* out = output.data();
			for (std::size_t i = 0; i < n; ++i) {
				out[i] = self.render(a[i], b[i]);
			}
		}
	}
};

} // namespace deluge::dsp
