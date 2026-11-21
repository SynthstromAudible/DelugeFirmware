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

#include "modulation/params/param_control_value.h"
#include "modulation/automation/auto_param_data.h"
#include "modulation/params/param_value.h"

namespace deluge::modulation::params {
int32_t readControlValue(ControlLayer layer, const ClipParamValue* scalar, const AutoParamData* automation,
                         int32_t missingValue) {
	if (layer == ControlLayer::PRESET) {
		return scalar != nullptr ? scalar->getPresetValue() : missingValue;
	}
	if (layer == ControlLayer::CLIP) {
		return scalar != nullptr ? scalar->getValue() : missingValue;
	}
	return automation != nullptr ? automation->getCurrentValue() : missingValue;
}

int32_t setClipValueFromControl(ClipParamValue& scalar, const AutoParamData& automation, int32_t value) {
	int32_t previous = automation.getCurrentValue();
	scalar.setValue(value);
	return previous;
}
} // namespace deluge::modulation::params
