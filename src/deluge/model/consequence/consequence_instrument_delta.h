/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "processing/sound/mod_matrix_host.h"

/// @brief The undo record of one cable topology edit on one sound: reverting applies the edit through the sound's mod
/// matrix, the sound found by its address exactly.
class ConsequenceInstrumentDelta final : public Consequence {
public:
	/// @param owner The sound, as an address with no param (edits::ownerAddressFor).
	/// @param edit  The edit.
	ConsequenceInstrumentDelta(deluge::project::ParamAddress owner, CableEdit edit);
	Error revert(TimeType time, ModelStack* modelStack) override;

private:
	deluge::project::ParamAddress owner_;
	CableEdit edit_;
};
