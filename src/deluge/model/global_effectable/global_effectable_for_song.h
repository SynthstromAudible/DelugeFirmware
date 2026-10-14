/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "engines/effects/master_chain.h"
#include "model/global_effectable/global_effectable.h"

class GlobalEffectableForSong final : public GlobalEffectable {
public:
	GlobalEffectableForSong();
	~GlobalEffectableForSong() override;

	[[nodiscard]] deluge::effects::MasterChain& chain() { return chain_; }
	[[nodiscard]] deluge::effects::ChainCore& core() override { return chain_.core(); }
	[[nodiscard]] const deluge::effects::ChainCore& core() const override { return chain_.core(); }
	[[nodiscard]] deluge::effects::Filter& filter() override { return chain_.filter(); }

	uint8_t* getModKnobMode() override { return &modKnobMode; }

	uint8_t modKnobMode;

private:
	/// @brief The song's master: its effects around the reverb.
	deluge::effects::MasterChain chain_;

protected:
	bool isSong() override { return true; }
};
