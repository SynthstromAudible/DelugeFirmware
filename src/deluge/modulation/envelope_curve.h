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

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace deluge::modulation {

/// 2^x for |x| < 126. Relative error under 2e-7, so it's as good as float gets for envelope work.
/// About 12 instructions on the A9; envelopes call it once per stage per render, not per sample.
[[gnu::always_inline]] constexpr float fastExp2(float x) {
	int32_t whole = static_cast<int32_t>(x); // truncates toward zero
	float frac = x - static_cast<float>(whole);
	if (frac < 0.f) { // floor for negative inputs, so frac is in [0, 1)
		frac += 1.f;
		whole -= 1;
	}
	// Weighted least-squares fit of 2^f on [0, 1] with p(0) = 1 exactly
	float p = 0.00185888436f;
	p = p * frac + 0.00903089405f;
	p = p * frac + 0.0557939352f;
	p = p * frac + 0.240164314f;
	p = p * frac + 0.693151594f;
	p = p * frac + 1.f;
	return std::bit_cast<float>(std::bit_cast<int32_t>(p) + (whole << 23));
}

/// A falling segment from 1 at x = 0 to 0 at x = 1, normalised so its length doesn't depend on its shape.
///
/// `k` is how many octaves (6 dB steps) an unnormalised exponential would fall over the segment:
///   - k = 0 is a straight line
///   - k > 0 is a decaying exponential, steep at the start (k = 8 is the classic Deluge decay and release, k = 4
///     the attack, read upside down)
///   - k < 0 bends the other way: slow at the start, steep at the end
/// The Deluge's original lookup tables decayTableSmall4 and decayTableSmall8 are exactly k = 4 and k = 8.
class EnvelopeCurve {
public:
	constexpr EnvelopeCurve() : EnvelopeCurve(0.f) {}
	constexpr explicit EnvelopeCurve(float k)
	    : k_(k), floor_(fastExp2(-k)), scale_((k == 0.f) ? 1.f : 1.f / (1.f - fastExp2(-k))) {}

	/// x in [0, 1]
	[[gnu::always_inline]] constexpr float falling(float x) const {
		if (k_ == 0.f) {
			return 1.f - x;
		}
		return (fastExp2(-k_ * x) - floor_) * scale_;
	}

	/// The rising mirror image: 0 at x = 0, 1 at x = 1. With k > 0 it's fast at the start, like an RC charge.
	[[gnu::always_inline]] constexpr float rising(float x) const { return 1.f - falling(x); }

	/// Inverse of rising(): where on the segment the curve reaches `value`. Not for the render path (uses log2).
	float risingInverse(float value) const {
		value = std::clamp(value, 0.f, 1.f);
		if (k_ == 0.f) {
			return value;
		}
		float x = -std::log2((1.f - value) / scale_ + floor_) / k_;
		return std::clamp(x, 0.f, 1.f);
	}

	constexpr float k() const { return k_; }

private:
	float k_;
	float floor_; // 2^-k, the unnormalised curve's value at x = 1
	float scale_; // 1 / (1 - 2^-k)
};

/// The shape of each stage of an envelope.
struct EnvelopeShape {
	EnvelopeCurve attack{4.f};
	EnvelopeCurve decay{8.f};
	EnvelopeCurve release{8.f};
};

/// The shape synth envelopes have always had
inline constexpr EnvelopeShape kClassicEnvelopeShape{};
/// Audio clips release along a shallower curve
inline constexpr EnvelopeShape kAudioClipEnvelopeShape{.release = EnvelopeCurve{4.f}};

} // namespace deluge::modulation
