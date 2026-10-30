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

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/bandlimited/waves.h"  // square, triangle_half_scale
#include "dsp-next/fixed/osc/sine_table.h"     // kSineTable
#include "dsp-next/fixed/sync/synced_phasor.h" // SyncedPhasor
#include <cstdint>
#include <type_traits>

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
///
/// Each is a plain @ref block_generator with no vptr. The arithmetic is per sample, but each keeps a hand-written
/// block loop rather than a scalar `render()` under the @ref Generator mixin; giving them `render()` is a
/// follow-up. Runtime selection goes through @ref AsBlock.

/// @brief Crude sawtooth: the phase ramp itself is the wave.
template <typename Phase>
class [[gnu::hot]] CrudeSaw {
	Phase phasor_;

public:
	using value_type = q31; ///< Full-scale q31 wave samples.

	explicit CrudeSaw(Phase phasor) : phasor_{phasor} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	/// Forward a per-block pitch change to the phase source (a Phasor takes one increment; a SyncedPhasor takes
	/// the audible + resetter increments), keeping the accumulated phase.
	template <typename... Increments>
	void set_increment(Increments... increments) {
		phasor_.set_increment(increments...);
	}

	/// @brief Fill @p output with the next `output.size()` samples, one phase step each.
	void render_block(Buffer output) {
		for (q31& sample : output) {
			sample = static_cast<int32_t>(phasor_.advance());
		}
	}
};
template <typename Phase>
CrudeSaw(Phase) -> CrudeSaw<Phase>;
static_assert(block_generator<CrudeSaw<Phasor>>, "a crude saw fills a block");
static_assert(!std::is_polymorphic_v<CrudeSaw<Phasor>>, "a crude saw fills a block without a vptr");
static_assert(block_generator<CrudeSaw<SyncedPhasor>>, "a crude saw fills a block");
static_assert(!std::is_polymorphic_v<CrudeSaw<SyncedPhasor>>, "a crude saw fills a block without a vptr");

/// @brief Crude square: a hard switch at the pulse-width threshold (the width also makes it a
/// (non-band-limited) pulse wave). @p pulse_width is the threshold phase passed to square().
template <typename Phase>
class [[gnu::hot]] CrudeSquare {
	Phase phasor_;
	uint32_t pulse_width_;

public:
	using value_type = q31; ///< Full-scale q31 wave samples.

	CrudeSquare(Phase phasor, uint32_t pulse_width) : phasor_{phasor}, pulse_width_{pulse_width} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	/// Forward a per-block pitch change to the phase source (a Phasor takes one increment; a SyncedPhasor takes
	/// the audible + resetter increments), keeping the accumulated phase.
	template <typename... Increments>
	void set_increment(Increments... increments) {
		phasor_.set_increment(increments...);
	}

	/// @brief Fill @p output with the next `output.size()` samples, one phase step each.
	void render_block(Buffer output) {
		for (q31& sample : output) {
			sample = square(phasor_.advance(), pulse_width_);
		}
	}
};
template <typename Phase>
CrudeSquare(Phase, uint32_t) -> CrudeSquare<Phase>;
static_assert(block_generator<CrudeSquare<Phasor>>, "a crude square fills a block");
static_assert(!std::is_polymorphic_v<CrudeSquare<Phasor>>, "a crude square fills a block without a vptr");
static_assert(block_generator<CrudeSquare<SyncedPhasor>>, "a crude square fills a block");
static_assert(!std::is_polymorphic_v<CrudeSquare<SyncedPhasor>>, "a crude square fills a block without a vptr");

/// @brief Crude triangle.
template <typename Phase>
class [[gnu::hot]] CrudeTriangle {
	Phase phasor_;

public:
	using value_type = q31; ///< Full-scale q31 wave samples.

	explicit CrudeTriangle(Phase phasor) : phasor_{phasor} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	/// Forward a per-block pitch change to the phase source (a Phasor takes one increment; a SyncedPhasor takes
	/// the audible + resetter increments), keeping the accumulated phase.
	template <typename... Increments>
	void set_increment(Increments... increments) {
		phasor_.set_increment(increments...);
	}

	/// @brief Fill @p output with the next `output.size()` samples, one phase step each.
	void render_block(Buffer output) {
		for (q31& sample : output) {
			sample = triangle_half_scale(phasor_.advance());
		}
	}
};
template <typename Phase>
CrudeTriangle(Phase) -> CrudeTriangle<Phase>;
static_assert(block_generator<CrudeTriangle<Phasor>>, "a crude triangle fills a block");
static_assert(!std::is_polymorphic_v<CrudeTriangle<Phasor>>, "a crude triangle fills a block without a vptr");
static_assert(block_generator<CrudeTriangle<SyncedPhasor>>, "a crude triangle fills a block");
static_assert(!std::is_polymorphic_v<CrudeTriangle<SyncedPhasor>>, "a crude triangle fills a block without a vptr");

/// @brief Crude sine: the @ref kSineTable LUT evaluated at the running phase. The cheap, non-band-limited
/// sine — full-scale, the same value the LFO/envelope read, but here as a free-running owned-phasor oscillator.
template <typename Phase>
class [[gnu::hot]] CrudeSine {
	Phase phasor_;

public:
	using value_type = q31; ///< Full-scale q31 wave samples.

	explicit CrudeSine(Phase phasor) : phasor_{phasor} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	/// Forward a per-block pitch change to the phase source (a Phasor takes one increment; a SyncedPhasor takes
	/// the audible + resetter increments), keeping the accumulated phase.
	template <typename... Increments>
	void set_increment(Increments... increments) {
		phasor_.set_increment(increments...);
	}

	/// @brief Fill @p output with the next `output.size()` samples, one phase step each.
	void render_block(Buffer output) {
		for (q31& sample : output) {
			sample = kSineTable.value(phasor_.advance());
		}
	}
};
template <typename Phase>
CrudeSine(Phase) -> CrudeSine<Phase>;
static_assert(block_generator<CrudeSine<Phasor>>, "a crude sine fills a block");
static_assert(!std::is_polymorphic_v<CrudeSine<Phasor>>, "a crude sine fills a block without a vptr");
static_assert(block_generator<CrudeSine<SyncedPhasor>>, "a crude sine fills a block");
static_assert(!std::is_polymorphic_v<CrudeSine<SyncedPhasor>>, "a crude sine fills a block without a vptr");

} // namespace deluge::dsp::fixed
