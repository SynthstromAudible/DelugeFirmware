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

#include <InstrumentClip.h>
#include <sound.h>
#include "PatchCable.h"

PatchCable::PatchCable() {
}

void PatchCable::setup(uint8_t newFrom, uint8_t newTo, int32_t newAmount) {
	from = newFrom;
	destinationParamDescriptor.setToHaveParamOnly(newTo);
	initAmount(newAmount);
}

bool PatchCable::isActive() {
	return param.containsSomething(0);
}

void PatchCable::initAmount(int32_t value) {
	param.nodes.empty();
	param.currentValue = value;
}

void PatchCable::makeUnusable() {
	destinationParamDescriptor.setToNull();
}
