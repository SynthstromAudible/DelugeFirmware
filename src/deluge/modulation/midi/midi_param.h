/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "modulation/automation/auto_param.h"

class MIDIParam {
public:
	MIDIParam();

	uint8_t cc{};
	AutoParam param;

	/// M2-F: this CC's automation node storage, owned inline by the MIDIParam (its lifecycle is the CC's). The
	/// MIDIParamVector reorders/relocates by CC on every learn, and this store rides with the element through
	/// those moves — so owner-resolution (MIDIParamCollection::docForParam → `&nodes_`) stays correct without a
	/// pointer stored on the AutoParam. `param` is now just the RT interpolation cursor.
	deluge::project::DocAutoParam nodes_{};
};
