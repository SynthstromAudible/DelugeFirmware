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

#include "modulation/midi/label/midi_label_vector.h"
#include "modulation/midi/label/midi_label.h"
#include <new>

MIDILabelVector::MIDILabelVector() : OrderedResizeableArray(sizeof(MIDILabel), 8) {
}

MIDILabel* MIDILabelVector::getLabelFromCC(int32_t cc) {
	int32_t i = searchExact(cc);
	if (i == -1) {
		return NULL;
	}
	else {
		return getElement(i);
	}
}

void MIDILabelVector::setOrCreateLabelForCC(int32_t cc, const char* name, bool allowCreation) {
	int32_t i = search(cc, GREATER_OR_EQUAL);
	MIDILabel* label;
	if (i >= getNumElements()) {
doesntExistYet:
		if (!allowCreation) {
			return;
		}
		label = insertLabel(i);
		if (!label) {
			return;
		}
		label->cc = cc;
		label->name.set(name);
	}
	else {
		label = getElement(i);
		if (label->cc != cc) {
			goto doesntExistYet;
		}
		label->name.set(name);
	}
}

MIDILabel* MIDILabelVector::insertLabel(int32_t i) {
	Error error = insertAtIndex(i);
	if (error != Error::NONE) {
		return NULL;
	}
	else {
		void* address = getElementAddress(i);
		MIDILabel* label = new (address) MIDILabel();
		return label;
	}
}

MIDILabel* MIDILabelVector::getElement(int32_t i) {
	return (MIDILabel*)getElementAddress(i);
}
