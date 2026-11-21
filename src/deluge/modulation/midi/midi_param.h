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
#include "modulation/automation/auto_param_access.h"

class MIDIParam {
public:
	MIDIParam();
	bool grabValueFromPos(uint32_t pos, int32_t loopLength) {
		return AutoParamPlayback(param).grabValueFromPos(pos, loopLength);
	}
	void clearAutomation() { AutoParamOwnership(param).deleteAutomationBasicForSetup(); }
	Error readFromFile(Deserializer& reader, int32_t limit) {
		return AutoParamSetup(param).readFromFile(reader, limit);
	}

	uint8_t cc;
	AutoParam param;
};
