/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "deluge-project/param_address.h"
#include "model/consequence/consequence.h"
#include "modulation/automation/auto_param.h"

class ModelStackWithAutoParam;

/// @brief The undo record of one param lane: its address and whichever of its states (before or after) the lane does
/// not hold now.
class ConsequenceParamChange final : public Consequence {
public:
	/// @param address    The lane's address (edits::addressFor of @p modelStack).
	/// @param modelStack The lane, whose value and nodes are captured.
	/// @param stealData  Take the lane's nodes (the caller is clearing them) instead of copying them.
	ConsequenceParamChange(deluge::project::ParamAddress address, ModelStackWithAutoParam const* modelStack,
	                       bool stealData);
	/// @brief Swap the stored state with the lane's, found by its address exactly (bridge::paramStackExact); a lane
	/// that no longer resolves is left alone.
	Error revert(TimeType time, ModelStack* modelStackWithSong) override;

	deluge::project::ParamAddress address;
	AutoParamState state;
};
