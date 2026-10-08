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

#include "dsp-next/fixed/dynamics/sidechain_envelope.h"
#include "engines/effects/state.h"

#include <cstdint>

namespace deluge::effects {

/// @brief An owner's sidechain: its SidechainState's rates, synced to the tempo when its sync level is set.
///
/// The owner hands in its state and the playback handler's tick inverse at every call, so a knob turn or a tempo
/// change takes effect from the next block.
class Sidechain {
public:
	/// @brief The attack param's minimum (attackRateTable[0] << 2): a hit at it skips the attack.
	///
	/// An attack rate in position steps per sample on the envelope's 2^23 stage scale.
	static constexpr int32_t kInstantAttack = 262144 << 2;

	/// @return @p state's rates at @p time_per_tick_inverse.
	/// @param time_per_tick_inverse The playback handler's getTimePerInternalTickInverse(): 2^31 ÷ (1.5 × the samples
	/// per internal tick), so internal ticks per sample at Q31, divided by 1.5.
	///
	/// Unsynced, the attack and release are the state's own. Synced, each is scaled by the tick inverse and shifted by
	/// the sync level: the attack by 7 less the level's index (a left shift at the 1/128 and 1/256 levels), the
	/// release by 9 less it.
	[[nodiscard]] static dsp::fixed::SidechainRates rates(const SidechainState& state, uint32_t time_per_tick_inverse);

	/// @brief Records a hit of @p strength (Q31) for the next render, combined with any already pending.
	void registerHit(int32_t strength) { envelope_.registerHit(strength); }

	/// @brief Places the envelope where a hit of @p strength (Q31) @p samples_ago would have put it, at @p state's
	/// rates for @p time_per_tick_inverse.
	void registerHitRetrospectively(int32_t strength, uint32_t samples_ago, const SidechainState& state,
	                                uint32_t time_per_tick_inverse) {
		envelope_.registerHitRetrospectively(strength, samples_ago, rates(state, time_per_tick_inverse));
	}

	/// @brief Advances the envelope by @p num_samples at @p state's rates for @p time_per_tick_inverse.
	/// @param shape The shape param, Q31: the release's curve.
	/// @return The duck after the block, Q31: 0 is none, negative ducks.
	[[nodiscard]] int32_t render(uint32_t num_samples, int32_t shape, const SidechainState& state,
	                             uint32_t time_per_tick_inverse) {
		return envelope_.render(num_samples, shape, rates(state, time_per_tick_inverse));
	}

	/// @brief Ends the duck, as for an owner that will not render for a while; see dsp::fixed::SidechainEnvelope::stop.
	void stop() { envelope_.stop(); }

private:
	dsp::fixed::SidechainEnvelope envelope_;
};

} // namespace deluge::effects
