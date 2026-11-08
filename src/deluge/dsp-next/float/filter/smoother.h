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

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/pack_f32.h"
#include "dsp-next/core/sample.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

/// @file
/// @brief fp::Smoother: a value moved toward a target set each block, exponentially or linearly, over its value type.
namespace deluge::dsp::fp {

/// @brief Moves y toward a per-block target. EXPONENTIAL: y ← y + a·(target − y), a = 1 − exp(−1/time).
/// LINEAR: a ramp from the block's start value that reaches the target at the block's last sample.
/// @details A processor kernel whose input is ignored (it is the cursor a Mono block needs), so it runs in Mono and
/// Poly as any processor does. LINEAR's ramp spans the block being rendered: `prepare` takes its length from the
/// block's n, so sample t (counting from 1) is the target itself once t reaches n, and a rounded ramp value before
/// it, so the block's last sample is exactly the target and no earlier one is, for any block length. EXPONENTIAL with
/// `time` ≤ 0 sets y to the target exactly. The target is not clamped.
struct Smoother {
	using value_type = f32; ///< The smoothed value.
	using step_type = f32;  ///< One value per sample.

	/// @brief How y moves.
	enum class Mode : uint8_t {
		EXPONENTIAL, ///< A one-pole toward the target.
		LINEAR,      ///< A ramp landing on the target at the block's end.
	};
	using Modes = ModeList<Mode::EXPONENTIAL, Mode::LINEAR>;

	/// @brief What the owner sets each block.
	struct Settings {
		f32 target = 0.f;              ///< Where y goes.
		f32 time = 0.f;                ///< EXPONENTIAL: the time constant, in samples; 0 or less jumps to the target.
		Mode mode = Mode::EXPONENTIAL; ///< How y moves.
	};

	/// @brief The target and the rate, over V.
	template <class V>
	struct CoefficientsT {
		V target{};                    ///< Where y goes.
		V rate{};                      ///< EXPONENTIAL: a, and y is the target where a ≥ 1.
		Mode mode = Mode::EXPONENTIAL; ///< Shared by every lane.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&CoefficientsT::target, &CoefficientsT::rate};
	};
	using Coefficients = CoefficientsT<f32>;

	/// @brief y, and for LINEAR the ramp's start and its position, over V.
	template <class V>
	struct StateT {
		V y{};     ///< The value.
		V start{}; ///< LINEAR: y at the block's start.
		V t{};     ///< LINEAR: samples into the block, as a value.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&StateT::y, &StateT::start, &StateT::t};
	};
	using State = StateT<f32>;

	/// @brief LINEAR's ramp for the block being rendered: one value for every lane, since every lane's block has the
	/// same length.
	struct Block {
		f32 length = 1.f; ///< The block's length in samples, at least 1.
		f32 step = 1.f;   ///< 1/length: how far one sample moves along the ramp.
	};

	/// @brief The coefficients for @p settings.
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		f32 rate = 1.f;
		if (settings.mode == Mode::EXPONENTIAL && settings.time > 0.f) {
			rate = 1.f - std::exp(-1.f / settings.time);
		}
		return {.target = settings.target, .rate = rate, .mode = settings.mode};
	}

	/// @brief Starts a block of @p n samples' ramp at the current value, over V. Runs every block, in either mode.
	template <class V>
	static Block prepare(const CoefficientsT<V>& /*c*/, StateT<V>& st, std::size_t n) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		st.start = st.y;
		st.t = V{};
		const f32 length = std::max(static_cast<f32>(n), 1.f);
		return {.length = length, .step = 1.f / length};
	}

	/// @brief One sample over V; the input is ignored.
	template <Mode M, class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& c, const Block& b, StateT<V>& st,
	                                                   std::type_identity_t<V> /*x*/) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		if constexpr (M == Mode::EXPONENTIAL) {
			st.y = select(c.rate >= V{1.f}, c.target, mul_add(st.y, c.rate, c.target - st.y));
		}
		else {
			st.t = st.t + V{1.f};
			// Where t has reached the block's length the sample is the target itself, so the ramp ends exactly on it.
			st.y = select(st.t >= V{b.length}, c.target, mul_add(st.start, st.t * V{b.step}, c.target - st.start));
		}
		return st.y;
	}
};

static_assert(processor_kernel<Smoother>);

} // namespace deluge::dsp::fp
