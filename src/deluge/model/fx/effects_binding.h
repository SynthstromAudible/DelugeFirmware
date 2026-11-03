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
#include "model/snapshot/mod_controllable_snapshot.h"
#include <cstddef>
#include <optional>

namespace deluge::model {

/// @brief An effects owner's settings and the render's copy of them. The settings live in a record (a sound's
/// SoundState, a host chain's FxChain) while one is bound, else here. The render reads only the copy, which project()
/// replaces with the render excluded, together with the ring, grain and delay memory the settings need.
class EffectsBinding {
public:
	/// @brief Default settings: the delay synced at the pre-loaded (else current) song's tick magnitude, the default
	/// sidechain; the render's copy equal to them.
	EffectsBinding();

	/// @return The settings: the bound record's, else this binding's own.
	[[nodiscard]] const deluge::effects::EffectStates& effects() const { return bound_ != nullptr ? *bound_ : own_; }

	/// @brief Makes @p next the settings, in the bound record while there is one. Does not project.
	/// @param next The new settings.
	void store(const deluge::effects::EffectStates& next);

	/// @brief Keeps the settings in @p record, which takes the settings held now; nullptr keeps them here again,
	/// copying the record's back. Does not project.
	/// @param record The record that holds the settings, or nullptr.
	void bind(deluge::effects::EffectStates* record);

	/// @return The render's copy of the settings.
	[[nodiscard]] const deluge::snapshot::ModControllableSnapshot& snapshot() const { return snapshot_; }

	/// @brief Copies the settings into the render's copy with the render excluded, committing the ring, grain and
	/// delay memory @p core needs for them.
	/// @param core         The owner's chain core.
	/// @param delayReserve The delay tape the owner's settings need, in frames (see delayReserveFrames).
	/// @param modFXParam   A host chain's selected mod FX knob, which its soft off reads; nullopt for a sound, whose
	/// copy keeps the value it has.
	void project(deluge::effects::ChainCore& core, std::size_t delayReserve, std::optional<ModFXParam> modFXParam);

private:
	deluge::effects::EffectStates own_{};
	deluge::effects::EffectStates* bound_{nullptr};
	deluge::snapshot::ModControllableSnapshot snapshot_{};
};

} // namespace deluge::model
