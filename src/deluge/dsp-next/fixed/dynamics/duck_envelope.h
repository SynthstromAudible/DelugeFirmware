/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/dynamics/hit_strength.h"
#include "util/fixedpoint.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief DuckEnvelope: the sidechain's duck as a control kernel, with its attack and release curves.
namespace deluge::dsp::fixed {

/// @brief The sidechain's duck, as a control kernel: a hit dips the level, which recovers over an attack then a curved
/// release. `Control<DuckEnvelope>` runs it a block at a time.
/// @details The level runs from ONE_Q31 (no duck) down to the hit's floor, ONE_Q31 less the hit's strength. The
/// attack eases the level down from where it stood to the floor; the release brings it back up from the floor to
/// ONE_Q31, along a curve the shape bends. Each stage is a position from 0 to @ref kStageLength, advanced by the
/// stage's step per sample; a stage ends when its position reaches the length.
///
/// **A block.** `advance` takes the pending hit, then moves the stage on by the whole block and returns the level at
/// the block's end, less ONE_Q31: 0 is no duck, negative ducks. The owner applies that one value to the block.
///
/// **Quirks carried for bit-exactness:**
/// - *The overrun is dropped.* A stage that ends inside a block hands over to the next, which starts at position 0
///   and advances by the whole block, not by the samples the ended stage left over (see @ref advance).
/// - *A hit no deeper than the level held is ignored.* A pending hit is taken only when its floor is strictly below
///   the level the last block returned (see @ref advance).
/// - *The instant attack.* When the attack is at its minimum, a hit skips the attack and starts the release from its
///   floor (see @ref Settings::instant_attack).
/// - *A retrospective hit attacks from no duck*, wherever the level stood (see @ref hit_at).
/// - *`hit_at` and `stop` keep the level* the last block returned, so a hit pending at the next block is weighed
///   against it (see @ref hit_at and @ref stop).
struct DuckEnvelope {
	using value_type = q31; ///< The level less ONE_Q31, Q31: 0 is no duck, negative ducks.
	using step_type = q31;  ///< A control kernel steps a scalar: the block is its step.

	/// @brief Where the envelope is: off (no duck), falling into a hit, or recovering from it.
	enum class Stage : uint8_t { OFF, ATTACK, RELEASE };

	/// @brief The bits of a stage's position: a stage runs from 0 to 2^23.
	static constexpr uint32_t kStagePositionBits = 23;
	/// @brief A stage's length on the position scale: 2^23.
	static constexpr uint32_t kStageLength = uint32_t{1} << kStagePositionBits;
	/// @brief The shape's bias to an unsigned value: INT32_MIN maps to 0, INT32_MAX to 2^32 - 1.
	static constexpr uint32_t kShapeBias = uint32_t{1} << 31;
	/// @brief The shift from the biased shape (2^32 full range) to curvedness units of 2^16: the full range is just
	/// under two units, the middle shape (0) exactly one.
	static constexpr uint32_t kShapeToCurvednessShift = 15;

	/// @brief What the owner sets, resolved (tempo-synced or not) before each block.
	struct Settings {
		int32_t attack{0};  ///< The attack's position steps per sample, on the 2^23 stage scale.
		int32_t release{0}; ///< The release's position steps per sample, on the 2^23 stage scale.
		q31 shape{0};       ///< The release's curve, Q31: INT32_MIN releases straight, higher curves more.
		/// The attack is at its minimum: a hit starts the release at once, at its floor, with no attack.
		bool instant_attack{false};
	};

	/// @brief What `advance` and the events read, computed from Settings by @ref coefficients.
	struct Coefficients {
		uint32_t attack_step{0};    ///< The attack's position steps per sample; 0 never ends the attack.
		uint32_t release_step{0};   ///< The release's position steps per sample; 0 never ends the release.
		bool instant_attack{false}; ///< A hit starts the release at once (see Settings::instant_attack).
		int32_t curve_extent16{0};  ///< The shape, biased positive, at 2^16 per curvedness unit (0 to 131071).
	};

	/// @brief The duck between blocks. State{} is no duck: off, at ONE_Q31, nothing pending.
	struct State {
		Stage stage{Stage::OFF}; ///< The stage the duck is in.
		uint32_t pos{0};         ///< The position in the stage, 0 to 2^23.
		q31 level{ONE_Q31};      ///< The level the last block returned (plus ONE_Q31), Q31; ONE_Q31 is no duck.
		q31 floor{0};            ///< The duck's bottom: ONE_Q31 less the hit's strength, Q31.
		/// How far the stage travels, Q31: from the level at the hit down to the floor in the attack, from the floor
		/// up to ONE_Q31 in the release.
		q31 depth{0};
		PendingHit pending{}; ///< The hit waiting for the next block.
	};

	/// @brief The coefficients for @p s: the steps as unsigned positions, and the shape's curve extent.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		return {
		    .attack_step = static_cast<uint32_t>(s.attack),
		    .release_step = static_cast<uint32_t>(s.release),
		    .instant_attack = s.instant_attack,
		    .curve_extent16 =
		        static_cast<int32_t>((static_cast<uint32_t>(s.shape) + kShapeBias) >> kShapeToCurvednessShift),
		};
	}

	/// @brief Takes the pending hit, then moves the duck on by a block of @p n samples.
	/// @details
	/// **The pending hit** is taken only when its floor is strictly below the level the last block returned: a hit
	/// no deeper than the duck already under way, including one exactly as deep, is dropped and changes nothing. A
	/// hit taken starts the attack, from the level held down to its floor, or with an instant attack the release,
	/// from its floor.
	///
	/// **The block.** The stage's position advances by @p n times its step, modulo 2^32. A stage whose position
	/// reaches @ref kStageLength ends inside the block and hands over: the attack to the release, the release to
	/// off. The next stage starts at position 0 and advances by the whole block, so the samples past the end of the
	/// ended stage are dropped, not carried. Off is no duck: the level returns to ONE_Q31.
	/// @param n The block's length in samples; may be 0.
	/// @return The level at the block's end less ONE_Q31, Q31: 0 is no duck, negative ducks.
	[[nodiscard]] static q31 advance(const Coefficients& c, State& s, std::size_t n);

	/// @brief Records a hit of @p strength (Q31) for the next block, combined with any already pending.
	static void hit(State& s, q31 strength);

	/// @brief Places the duck where a hit of @p strength (Q31) @p samples_ago would have put it, dropping any pending
	/// hit. A seek: the level is left for the next block to recompute.
	/// @details The level stays what the last block returned until the next block recomputes it, so a @ref hit
	/// between this and that block is weighed against the old level, not the placed duck's, as after @ref stop.
	///
	/// The hit's attack starts from no duck, travelling the hit's whole strength, wherever the level stood.
	/// Each stage lasts its length over its step in whole samples, rounded down: a hit landing in a stage's last,
	/// partial sample is placed at the next stage's start. The attack is placed by its step even when the attack is
	/// instant. A zero step is a stage that never ends, so a hit at a zero rate stays in that stage, at position 0.
	/// The owner's settings give a zero rate: a song file's rate of 0, or a long synced rate at a slow tempo.
	static void hit_at(const Coefficients& c, State& s, q31 strength, uint32_t samples_ago);

	/// @brief Ends the duck: the next block returns no duck unless a hit arrives.
	/// @details The level is kept, not reset to ONE_Q31, so a hit pending at the next block is weighed against the
	/// level the duck stood at, and one no deeper is dropped.
	static void stop(State& s);
};

static_assert(control_kernel<DuckEnvelope>);
static_assert(!std::is_polymorphic_v<DuckEnvelope>);

/// @brief The attack's level at position @p pos: from @p floor plus @p depth at 0, easing down to @p floor at the
/// stage's end, slowly at first and then quickly, along the 4-curve decay table.
/// @param depth How far the attack falls, Q31.
/// @param floor The level it falls to, Q31.
/// @param pos The position in the stage, 0 to 2^23.
/// @return The level, Q31.
[[nodiscard]] q31 attack_level(q31 depth, q31 floor, uint32_t pos);

/// @brief The release's level at position @p pos: from ONE_Q31 less @p depth at 0 back up to ONE_Q31 at the stage's
/// end.
/// @details The release blends a straight line with the 8-curve decay table, which holds the duck and rises late.
/// Its curvedness starts at @p curve_extent16, capped at one unit (2^16, fully curved), and falls by one unit over
/// the stage, so a lower shape straightens sooner. Once the curvedness falls below zero the release is the straight
/// line alone.
///
/// *The two straight lines differ.* The blend's straight line reads the position at 2^15 per stage, dropping its low
/// 8 bits; the straight line alone reads it at full precision. At a curvedness of exactly 0 the blend is all
/// straight line, and still drops those bits.
/// @param depth How far the release rises, Q31.
/// @param pos The position in the stage, 0 to 2^23.
/// @param curve_extent16 The shape's curve extent, at 2^16 per curvedness unit (Coefficients::curve_extent16).
/// @return The level, Q31.
[[nodiscard]] q31 release_level(q31 depth, uint32_t pos, int32_t curve_extent16);

} // namespace deluge::dsp::fixed
