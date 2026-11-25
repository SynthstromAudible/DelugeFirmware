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

#include "dsp/core/pack_f32.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

/// @file
/// @brief fp::Curve: the normalised envelope curve main's envelopes use (`modulation/envelope_curve.h` there), and the
/// shape of each stage from its one curve control.

namespace deluge::dsp::fp {

/// @brief A falling segment from 1 at x = 0 to 0 at x = 1, `(2^(-k·x) - 2^-k) / (1 - 2^-k)`, normalised so its length
/// does not depend on its shape; `1 - x` at k = 0.
/// @details Written as `expm1(a·(1 - x)) / expm1(a)` with a = k·ln 2, the same curve without the cancellation of
/// `2^(-k·x) - 2^-k`, which costs a gentle curve (k near 0) its precision in float. Both ends are exact.
[[nodiscard]] inline f32 curve_falling(f32 k, f32 x) {
	x = std::clamp(x, 0.f, 1.f);
	if (k == 0.f) {
		return 1.f - x;
	}
	const f32 a = k * std::numbers::ln2_v<f32>;
	return std::expm1(a * (1.f - x)) / std::expm1(a);
}

/// @brief @ref curve_falling over each lane of a Pack. The engine's Poly runs it at a stage's step only.
template <std::size_t N>
[[nodiscard]] Pack<f32, N> curve_falling(Pack<f32, N> k, Pack<f32, N> x) {
	std::array<f32, N> out{};
	for (std::size_t l = 0; l < N; ++l) {
		out[l] = curve_falling(k.get(l), x.get(l));
	}
	return Pack<f32, N>::load(out.data());
}

/// @brief One stage's curve: k octaves an unnormalised exponential would fall over the stage. k > 0 is steep at the
/// start, k < 0 bends the other way, k = 0 is a straight line. The Deluge's decay tables are k = 8 and k = 4.
struct Curve {
	f32 k = 0.f; ///< The curve's octaves.

	/// @brief The falling curve at @p x in [0, 1].
	[[nodiscard]] f32 falling(f32 x) const { return curve_falling(k, x); }
	/// @brief The rising curve, `1 - falling(x)`: 0 at x = 0, 1 at x = 1, steep at the start for k > 0 like an RC
	/// charge.
	[[nodiscard]] f32 rising(f32 x) const { return 1.f - falling(x); }
	/// @brief Where on the stage @ref rising reaches @p level (clamped to [0, 1]). Not for the render path: it takes a
	/// logarithm.
	[[nodiscard]] f32 rising_inverse(f32 level) const {
		level = std::clamp(level, 0.f, 1.f);
		if (k == 0.f) {
			return level;
		}
		const f32 a = k * std::numbers::ln2_v<f32>;
		return std::clamp(1.f - (std::log1p((1.f - level) * std::expm1(a)) / a), 0.f, 1.f);
	}
};

/// @brief The least curve control.
inline constexpr int32_t kMinCurve = -50;
/// @brief The greatest curve control.
inline constexpr int32_t kMaxCurve = 50;
/// @brief The curve control of the Deluge's original shape: decay and release k = 8, attack k = 4.
inline constexpr int32_t kClassicCurve = 32;

/// @brief Each stage's curve.
struct CurveShape {
	Curve attack{4.f};  ///< The attack's.
	Curve decay{8.f};   ///< The decay's.
	Curve release{8.f}; ///< The release's.

	/// @brief The shape of one curve control @p curve in [kMinCurve, kMaxCurve]: decay and release k = curve / 4,
	/// attack half that.
	[[nodiscard]] static constexpr CurveShape from_curve(int32_t curve) {
		const f32 k = static_cast<f32>(std::clamp(curve, kMinCurve, kMaxCurve)) * 0.25f;
		return {.attack = Curve{k * 0.5f}, .decay = Curve{k}, .release = Curve{k}};
	}
};

/// @brief Audio clips release along a shallower curve, k = 4; their other stages are classic.
inline constexpr CurveShape kAudioClipShape{.release = Curve{4.f}};

} // namespace deluge::dsp::fp
