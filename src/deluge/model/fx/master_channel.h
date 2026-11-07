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

#include "effects/master_chain.h"
#include "model/fx/chain_conversions.h"
#include "model/fx/chain_view.h"
#include "model/fx/effects_binding.h"

namespace deluge::model {

/// @brief The song's master channel: its chain around the reverb, the chain's settings and the render's copy of them,
/// and which values its knobs edit. The engine renders it around the reverb.
class MasterChannel {
public:
	/// @brief A chain with the app's allocators, its settings at their defaults and projected.
	MasterChannel();
	/// @brief Gives the delay's tapes back to the arena.
	~MasterChannel();
	MasterChannel(const MasterChannel&) = delete;
	MasterChannel& operator=(const MasterChannel&) = delete;

	/// @return The master chain.
	[[nodiscard]] effects::MasterChain& chain() { return chain_; }
	/// @return The chain's core: the effect components every chain has.
	[[nodiscard]] effects::ChainCore& core() { return chain_.core(); }
	[[nodiscard]] const effects::ChainCore& core() const { return chain_.core(); }
	/// @return The chain's filters.
	[[nodiscard]] effects::Filter& filter() { return chain_.filter(); }
	/// @return The chain's settings and the render's copy of them.
	[[nodiscard]] EffectsBinding& binding() { return binding_; }
	[[nodiscard]] const EffectsBinding& binding() const { return binding_; }
	/// @return Which values the chain's knobs edit.
	[[nodiscard]] ChainView& view() { return view_; }
	[[nodiscard]] const ChainView& view() const { return view_; }
	/// @return The chain's remembered param conversions. Render tier only.
	[[nodiscard]] ChainConversions& conversions() { return conversions_; }

private:
	EffectsBinding binding_;
	ChainView view_;
	effects::MasterChain chain_;
	ChainConversions conversions_;
};

} // namespace deluge::model
