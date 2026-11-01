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

#include "dsp-next/core/control.h"
#include "dsp-next/fixed/dynamics/duck_envelope.h"
#include "dsp-next/fixed/dynamics/duck_gain.h"
#include "effects/state.h"

#include <cstdint>

namespace deluge::effects {

/// @brief One block's duck: its length, the hit pending for it, the shape and depth params and the rates.
struct DuckBlock {
	uint32_t frames;             ///< The block's length in samples.
	int32_t hit;                 ///< The hit pending for this block, Q31; 0 for none.
	int32_t shape;               ///< The shape param, Q31: the release's curve.
	int32_t depth;               ///< The sidechain volume as a patch cable's depth, Q29.
	const SidechainState& state; ///< The attack, release and sync.
	uint32_t tick_inverse;       ///< The playback handler's getTimePerInternalTickInverse().
};

/// @brief An owner's sidechain: the duck envelope at its SidechainState's rates, synced to the tempo when its sync
/// level is set.
///
/// The owner hands in its state and the playback handler's tick inverse at every call, so a knob turn or a tempo
/// change takes effect from the next block, while the duck's position carries over.
class Sidechain {
public:
	/// @brief The attack param's minimum (attackRateTable[0] << 2): a hit at it skips the attack.
	///
	/// An attack rate in position steps per sample on the envelope's 2^23 stage scale.
	static constexpr int32_t kInstantAttack = 262144 << 2;

	/// @brief The duck envelope's settings for @p state at @p time_per_tick_inverse and @p shape.
	/// @param state The sidechain's attack, release and sync.
	/// @param time_per_tick_inverse The playback handler's getTimePerInternalTickInverse(): 2^31 ÷ (1.5 × the samples
	/// per internal tick), so internal ticks per sample at Q31, divided by 1.5.
	/// @param shape The shape param, Q31: the release's curve, passed through.
	///
	/// Unsynced, the attack and release are the state's own. Synced, each is scaled by the tick inverse and shifted by
	/// the sync level: the attack by 7 less the level's index (a left shift at the 1/128 and 1/256 levels), the
	/// release by 9 less it.
	/// @return The settings to configure the duck envelope with.
	[[nodiscard]] static dsp::fixed::DuckEnvelope::Settings settings(const SidechainState& state,
	                                                                 uint32_t time_per_tick_inverse, dsp::q31 shape);

	/// @brief Records a hit of @p strength (Q31) for the next render, combined with any already pending.
	/// @param strength The hit's strength, Q31.
	void registerHit(int32_t strength) { duck_.on<&dsp::fixed::DuckEnvelope::hit>(strength); }

	/// @brief Places the duck where a hit of @p strength (Q31) @p samples_ago would have put it, at @p state's rates
	/// for @p time_per_tick_inverse.
	/// @details The placement reads the rates alone: dsp::fixed::DuckEnvelope::hit_at takes no shape, and the next
	/// render configures the shape it is given.
	/// @param strength              The hit's strength, Q31.
	/// @param samples_ago           How many samples before now the hit happened.
	/// @param state                 The sidechain's attack, release and sync.
	/// @param time_per_tick_inverse The playback handler's getTimePerInternalTickInverse().
	void registerHitRetrospectively(int32_t strength, uint32_t samples_ago, const SidechainState& state,
	                                uint32_t time_per_tick_inverse);

	/// @brief Advances the duck by @p num_samples at @p state's rates for @p time_per_tick_inverse.
	/// @param num_samples           The block's length in samples.
	/// @param shape                 The shape param, Q31: the release's curve.
	/// @param state                 The sidechain's attack, release and sync.
	/// @param time_per_tick_inverse The playback handler's getTimePerInternalTickInverse().
	/// @return The duck after the block, Q31: 0 is none, negative ducks.
	[[nodiscard]] int32_t render(uint32_t num_samples, int32_t shape, const SidechainState& state,
	                             uint32_t time_per_tick_inverse);

	/// @brief Registers @p b's hit, advances the duck by its block and gives the level at @p S's scale.
	/// @tparam S The owner's volume scale.
	/// @param b The block's duck inputs.
	/// @return The ducked level at @p S's scale: 2^27 (CLIP) or 2^29 (REVERB_RETURN) unducked.
	template <dsp::fixed::DuckScale S>
	[[nodiscard]] int32_t level(const DuckBlock& b) {
		if (b.hit != 0) {
			registerHit(b.hit);
		}
		return dsp::fixed::duck_gain<S>(render(b.frames, b.shape, b.state, b.tick_inverse), b.depth);
	}

	/// @brief Ends the duck, as for an owner that will not render for a while; see dsp::fixed::DuckEnvelope::stop.
	void stop() { duck_.on<&dsp::fixed::DuckEnvelope::stop>(); }

private:
	dsp::Control<dsp::fixed::DuckEnvelope> duck_; ///< The duck, configured from the owner's state at every call.
};

} // namespace deluge::effects
