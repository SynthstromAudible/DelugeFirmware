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

#include "definitions_cxx.hpp"
#include "effects/chain_core.h"
#include "effects/state.h"
#include "model/fx/chain_view.h"
#include "model/fx/effects_binding.h"
#include "modulation/params/param.h"

class Sound;

namespace deluge::model {

/// @brief One owner's effects: its chain core, the binding that holds its settings and the render's copy, a host
/// chain's view, and the sound when the owner is one. A value: the owner outlives every copy.
struct EffectsOwner {
	effects::ChainCore* core{}; ///< The owner's chain core; never null.
	EffectsBinding* binding{};  ///< The owner's settings and render copy; never null.
	ChainView* view{};          ///< A host chain's view (a kit, an audio track, the master); null for a sound.
	::Sound* sound{};           ///< The sound, for a sound; null for a host chain.

	/// @return The owner's settings.
	[[nodiscard]] const effects::EffectStates& effects() const { return binding->effects(); }
	/// @return Whose unpatched params the owner's knobs name: UNPATCHED_SOUND for a sound, else UNPATCHED_GLOBAL.
	[[nodiscard]] modulation::params::Kind unpatchedKind() const {
		return sound != nullptr ? modulation::params::Kind::UNPATCHED_SOUND
		                        : modulation::params::Kind::UNPATCHED_GLOBAL;
	}
};

/// @brief Copies the owner's settings into the render's copy with the render excluded, with the delay reserve the
/// current song gives its delay (none with no song current or the delay OFF) and a host chain's selected mod FX knob.
/// @param owner The owner.
void projectEffects(EffectsOwner owner);

/// @brief Makes @p next the owner's settings, in its bound record while there is one, and projects them.
/// @param owner The owner.
/// @param next  The new settings.
void storeEffects(EffectsOwner owner, const effects::EffectStates& next);

/// @brief Keeps the owner's settings in @p record (nullptr: in the binding again, the record's copied back), and
/// projects them.
/// @param owner  The owner.
/// @param record The record that holds the settings, or nullptr.
void bindEffects(EffectsOwner owner, effects::EffectStates* record);

/// @brief Sets which mod FX value and filter a host chain's knobs edit, in its bound record while there is one, and
/// projects the settings with it.
/// @pre owner.view != nullptr.
/// @param owner The host chain's owner.
/// @param param Which mod FX value.
/// @param type  Which filter.
void setChainView(EffectsOwner owner, ModFXParam param, FilterType type);

/// @brief The host chain plays @p chain from now on: the record takes the settings and the view played now. Nothing
/// when it already plays @p chain.
/// @pre owner.view != nullptr.
/// @param owner The host chain's owner.
/// @param chain Its chain record, which outlives the binding (unbindChain() runs before it goes).
void bindChain(EffectsOwner owner, project::FxChain& chain);

/// @brief Takes back the settings and the view the bound record holds, before the record goes; nothing when unbound.
/// @pre owner.view != nullptr.
/// @param owner The host chain's owner.
void unbindChain(EffectsOwner owner);

/// @brief Makes the owner's settings its current ones with @p change applied to a copy of them, and projects them: a
/// direct change, for an owner whose settings are its own to set (no Edit, no record hand-over).
/// @param owner  The owner.
/// @param change Applied to the copy, as `change(effects::EffectStates&)`.
template <class Change>
void changeEffects(EffectsOwner owner, Change&& change) {
	effects::EffectStates next = owner.effects();
	change(next);
	storeEffects(owner, next);
}

} // namespace deluge::model
