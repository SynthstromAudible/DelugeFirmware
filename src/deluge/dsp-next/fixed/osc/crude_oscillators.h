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

#include "dsp-next/core/generator.h"
#include "dsp-next/fixed/bandlimited/waves.h" // square, triangle
#include <cstdint>

namespace deluge::dsp::fixed {

/// @file
/// @brief The crude (non-band-limited) analytic oscillators: the cheap, scalar render used at
/// low pitches and as the CPU-dire fallback (where aliasing is least audible / CPU is tight).
///
/// Each is a stateful generator templated on its **phase source** @p Phase — a plain
/// @ref deluge::dsp::Phasor for free-running, or a @ref SyncedPhasor for hard sync (which
/// resets the phase per sample). The generator body is identical for both; it only ever calls
/// `phase.advance()`, so sync is just a different phase source (CTAD picks the type from the
/// constructor argument). render is **scalar** (one sample at a time, the exact legacy analytic
/// kernel) rather than the 4-lane SIMD of the band-limited oscillators — the deliberately-light
/// path, bit-exact with the legacy scalar loops. Each emits the **full-scale** raw wave (the
/// value the crude VCA, @ref CrudeAmplitude, consumes); the un-amplified path scales it by a
/// single per-wave shift (saw/square >> 1, triangle << 1). phase() reads the phase back (for
/// the synced arms to store per voice).

/// @brief Crude sawtooth: the phase ramp itself is the wave.
template <typename Phase>
class [[gnu::hot]] CrudeSaw : public BlockGenerator<q31> {
	Phase phasor_;

public:
	explicit CrudeSaw(Phase phasor) : phasor_{phasor} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(Buffer output) override {
		for (q31& sample : output) {
			sample = static_cast<int32_t>(phasor_.advance());
		}
	}
};
template <typename Phase>
CrudeSaw(Phase) -> CrudeSaw<Phase>;

/// @brief Crude square: a hard switch at the pulse-width threshold (the width also makes it a
/// (non-band-limited) pulse wave). @p pulse_width is the threshold phase passed to square().
template <typename Phase>
class [[gnu::hot]] CrudeSquare : public BlockGenerator<q31> {
	Phase phasor_;
	uint32_t pulse_width_;

public:
	CrudeSquare(Phase phasor, uint32_t pulse_width) : phasor_{phasor}, pulse_width_{pulse_width} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(Buffer output) override {
		for (q31& sample : output) {
			sample = square(phasor_.advance(), pulse_width_);
		}
	}
};
template <typename Phase>
CrudeSquare(Phase, uint32_t) -> CrudeSquare<Phase>;

/// @brief Crude triangle.
template <typename Phase>
class [[gnu::hot]] CrudeTriangle : public BlockGenerator<q31> {
	Phase phasor_;

public:
	explicit CrudeTriangle(Phase phasor) : phasor_{phasor} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(Buffer output) override {
		for (q31& sample : output) {
			sample = triangle(phasor_.advance());
		}
	}
};
template <typename Phase>
CrudeTriangle(Phase) -> CrudeTriangle<Phase>;

} // namespace deluge::dsp::fixed
