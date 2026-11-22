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
#include "modulation/automation/auto_param_access.h"
#include "modulation/midi/midi_param.h"
#include "modulation/params/param_control_value.h"
#include <new>

MIDIParamVector::MIDIParamVector() : OrderedResizeableArray(sizeof(MIDIParam), 32) {
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
	if (cc < 0 || cc > 255) {
		return nullptr;
	}
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
		const int32_t* preset = presetValueFor(cc);
		AutoParamSetup(param->param).setCurrentValueBasicForSetup((preset != nullptr) ? *preset : defaultValue);
		param->clipValue.bindPreset(preset);
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
	Error error = insertAtIndex(i);
	if (error != Error::NONE) {
		return NULL;
	}
	else {
		void* address = getElementAddress(i);
		MIDIParam* param = new (address) MIDIParam();
		rebindAfterMove();
		return param;
	}
}

MIDIParam* MIDIParamVector::getElement(int32_t i) {
	return (MIDIParam*)getElementAddress(i);
}

void MIDIParamVector::rebindAfterMove() {
	for (int32_t i = 0; i < getNumElements(); ++i) {
		auto* entry = getElement(i);
		AutoParamOwnership(entry->param).rebindClipValueAfterMove(&entry->clipValue);
	}
}

Error MIDIParamVector::beenCloned() {
	Error error = OrderedResizeableArray::beenCloned();
	if (error == Error::NONE) {
		rebindAfterMove();
	}
	return error;
}

void MIDIParamVector::deleteAtKey(int32_t key) {
	OrderedResizeableArray::deleteAtKey(key);
	rebindAfterMove();
}

PLACE_SDRAM_TEXT void MIDIParamVector::bindPresetValues(const int32_t* values) {
	presetValues_ = values;
	for (int32_t i = 0; i < getNumElements(); ++i) {
		auto* entry = getElement(i);
		entry->clipValue.bindPreset(presetValueFor(entry->cc));
	}
}

int32_t MIDIParamVector::getValueForControl(int32_t cc, deluge::modulation::params::ControlLayer layer) {
	using namespace deluge::modulation::params;
	if (cc < 0 || cc > 255) {
		return 0;
	}
	auto* entry = getParamFromCC(cc);
	return readControlValue(layer, entry != nullptr ? &entry->clipValue : nullptr,
	                        layer == ControlLayer::LIVE && entry != nullptr ? &entry->param.data() : nullptr,
	                        presetValueFor(cc) != nullptr ? *presetValueFor(cc) : 0);
}
