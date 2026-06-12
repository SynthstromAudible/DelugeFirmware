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

#include "modulation/midi/midi_param_vector.h"

MIDIParam* MIDIParamVector::getParamFromCC(int32_t cc) {
	int32_t i = searchExact(cc);
	if (i == -1) {
		return nullptr;
	}
	return getElement(i);
}

MIDIParam* MIDIParamVector::getOrCreateParamFromCC(int32_t cc, int32_t defaultValue, bool allowCreation) {
	int32_t i = search(cc, GREATER_OR_EQUAL);
	MIDIParam* param;
	if (i >= getNumElements() || getElement(i)->cc != cc) {
		if (!allowCreation) {
			return nullptr;
		}
		param = insertParam(i);
		if (!param) {
			return nullptr;
		}
		param->cc = cc;
		param->param.setCurrentValueBasicForSetup(defaultValue);
	}
	else {
		param = getElement(i);
	}
	return param;
}

MIDIParam* MIDIParamVector::insertParam(int32_t i) {
	Error error = insertAtIndex(i);
	if (error != Error::NONE) {
		return nullptr;
	}
	return getElementAddress(i);
}

MIDIParam* MIDIParamVector::getElement(int32_t i) {
	return getElementAddress(i);
}
