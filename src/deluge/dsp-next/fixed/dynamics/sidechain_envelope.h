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

#include "util/fixedpoint.h"

#include <algorithm>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief The envelope's rates for one block, as the owner resolved them (tempo-synced or not): position steps per
/// sample on a 2^23 stage.
struct SidechainRates {
	int32_t attack;
	int32_t release;
	bool instant_attack; ///< the attack is at its minimum: a hit jumps straight to release
};

/// @brief The sidechain's duck: a hit dips the level, which recovers over an attack then a curved release.
///
/// The level runs from ONE_Q31 (no duck) down to ONE_Q31 less the hit's strength. The attack eases down from where
/// the level stood to the hit's depth; the release comes back up to ONE_Q31 along a curve the shape param bends.
/// Each stage is a position on a 2^23 scale, advanced by the stage's rate per sample.
class SidechainEnvelope {
public:
	/// @brief Where the envelope is: off (no duck), falling into a hit, or recovering from it.
	enum class Stage : uint8_t { OFF, ATTACK, RELEASE };

	/// @brief Records a hit of @p strength (Q31) to take effect at the next render, combined with any pending one.
	void registerHit(int32_t strength);

	/// @brief Places the envelope where a hit of @p strength (Q31) @p samples_ago would have put it, dropping any
	/// pending hit.
	void registerHitRetrospectively(int32_t strength, uint32_t samples_ago, SidechainRates rates);

	/// @brief Takes any pending hit, then advances the envelope by @p num_samples.
	///
	/// A pending hit is taken only when it dips deeper than the level already is. A stage that ends inside the block
	/// moves on to the next stage at once, which starts at its position 0 and advances by the whole block.
	/// @param shape The shape param, Q31: from INT32_MIN (a straight release) up, the release curves more.
	/// @return The level after @p num_samples, minus ONE_Q31 (so 0 is no duck, negative ducks), Q31.
	[[nodiscard]] int32_t render(uint32_t num_samples, int32_t shape, SidechainRates rates);

	/// @brief Ends the duck: the next render outputs none unless a hit arrives.
	///
	/// The last level stays, so a hit pending at that render is weighed against it as usual.
	void stop() { stage_ = Stage::OFF; }

	/// @return The stage the envelope is in.
	[[nodiscard]] Stage stage() const { return stage_; }

private:
	/// A stage's length on the position scale: 2^23.
	static constexpr uint32_t kStageLength = 8388608;

	/// @brief Enters @p next from its start: an attack falls from the level now, a release rises from the hit's depth.
	void transition(Stage next);

	/// @return The attack's level at the current position, Q31.
	[[nodiscard]] int32_t attackLevel() const;

	/// @return The release's level at the current position, bent by @p shape (Q31), Q31.
	[[nodiscard]] int32_t releaseLevel(int32_t shape) const;

	Stage stage_{Stage::OFF};
	/// The position in the current stage, 0 to 2^23.
	uint32_t pos_{0};
	/// The level the last render returned, Q31.
	int32_t last_value_{ONE_Q31};
	/// The strength of the hit waiting for the next render, Q31; 0 is none.
	int32_t pending_hit_strength_{0};
	/// The hit's depth: the level at the bottom of the duck, Q31.
	int32_t offset_{0};
	/// How far the current stage travels: from the level at the hit to the depth in the attack, from the depth to
	/// ONE_Q31 in the release, Q31.
	int32_t height_{0};
};

/// @return Two hits' combined strength (Q31): half the larger plus half their sum, the sum clamped at full scale.
[[nodiscard]] constexpr int32_t combine_hit_strengths(int32_t a, int32_t b) {
	const uint32_t sum = std::min(static_cast<uint32_t>(a) + static_cast<uint32_t>(b), uint32_t{2147483647});
	return (std::max(a, b) >> 1) + static_cast<int32_t>(sum >> 1);
}

} // namespace deluge::dsp::fixed
