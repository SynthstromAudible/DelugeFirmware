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
#include "modulation/midi/midi_param.h"
#include <new>

MIDIParamVector::MIDIParamVector() : OrderedResizeableArray(sizeof(MIDIParam), 8) {
}

MIDIParam* MIDIParamVector::getParamFromCC(int32_t cc) {
	int32_t i = searchExact(cc);
	if (i == -1) {
		return NULL;
	}
	else {
		return getElement(i);
	}
}

MIDIParam* MIDIParamVector::getOrCreateParamFromCC(int32_t cc, int32_t defaultValue, bool allowCreation) {
	int32_t i = search(cc, GREATER_OR_EQUAL);
	MIDIParam* param;
	if (i >= getNumElements()) {
doesntExistYet:
		if (!allowCreation) {
			return NULL;
		}
		param = insertParam(i);
		if (!param) {
			return NULL;
		}
		param->cc = cc;
		param->param.setCurrentValueBasicForSetup(defaultValue);
	}
	else {
		param = getElement(i);
		if (param->cc != cc) {
			goto doesntExistYet;
		}
	}
	return param;
}

MIDIParam* MIDIParamVector::insertParam(int32_t i) {
	Error error;
	error = insertAtIndex(i);
	if (error != Error::NONE) {
		return NULL;
	}
	else {
		void* address = getElementAddress(i);
		MIDIParam* param = new (address) MIDIParam();
		return param;
	}
}

MIDIParam* MIDIParamVector::getElement(int32_t i) {
	return (MIDIParam*)getElementAddress(i);
}
