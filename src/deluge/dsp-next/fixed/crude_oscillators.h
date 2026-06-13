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
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/waves.h" // square, triangle
#include <cstdint>

namespace deluge::dsp::fixed {

/// @file
/// @brief The crude (non-band-limited) analytic oscillators: the cheap, scalar render used at
/// low pitches and as the CPU-dire fallback (where aliasing is least audible / CPU is tight).
///
/// Each is a stateful generator owning a @ref deluge::dsp::Phasor. render is **scalar** (one
/// sample at a time, calling the exact legacy analytic kernel) rather than the 4-lane SIMD of
/// the band-limited oscillators — this is the deliberately-light path, and it keeps the output
/// bit-exact with the legacy scalar loops. Each emits the **full-scale** raw wave (the value
/// the crude VCA, @ref CrudeAmplitude, consumes); the un-amplified path scales it by a single
/// per-wave shift (saw/square >> 1, triangle << 1). seek()/phase() let a hard-sync driver
/// reset and read back the phase.

/// @brief Crude sawtooth: the phase ramp itself is the wave.
class [[gnu::hot]] CrudeSaw : public BlockGenerator<q31> {
	Phasor phasor_;

public:
	CrudeSaw(uint32_t phase, uint32_t increment) : phasor_{phase, increment} {}
	void seek(uint32_t phase) { phasor_.seek(phase); }
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(Buffer output) override {
		for (q31& sample : output) {
			sample = static_cast<int32_t>(phasor_.advance());
		}
	}
};

/// @brief Crude square: a hard switch at the pulse-width threshold (the width also makes it a
/// (non-band-limited) pulse wave). @p pulse_width is the threshold phase passed to getSquare.
class [[gnu::hot]] CrudeSquare : public BlockGenerator<q31> {
	Phasor phasor_;
	uint32_t pulse_width_;

public:
	CrudeSquare(uint32_t phase, uint32_t increment, uint32_t pulse_width)
	    : phasor_{phase, increment}, pulse_width_{pulse_width} {}
	void seek(uint32_t phase) { phasor_.seek(phase); }
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(Buffer output) override {
		for (q31& sample : output) {
			sample = square(phasor_.advance(), pulse_width_);
		}
	}
};

/// @brief Crude triangle.
class [[gnu::hot]] CrudeTriangle : public BlockGenerator<q31> {
	Phasor phasor_;

public:
	CrudeTriangle(uint32_t phase, uint32_t increment) : phasor_{phase, increment} {}
	void seek(uint32_t phase) { phasor_.seek(phase); }
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(Buffer output) override {
		for (q31& sample : output) {
			sample = triangle(phasor_.advance());
		}
	}
};

} // namespace deluge::dsp::fixed
