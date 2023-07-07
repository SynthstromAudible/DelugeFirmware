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

#include "modulation/midi/midi_knob_array.h"
#include "modulation/knob.h"
#include <new>

MidiKnobArray::MidiKnobArray() : ResizeableArray(sizeof(MIDIKnob)) {
}

MIDIKnob* MidiKnobArray::insertKnob(int i) {
	int error = insertAtIndex(i);
	if (error) {
		return NULL;
	}
	else {
		void* address = getElementAddress(i);
		MIDIKnob* knob = new (address) MIDIKnob();
		return knob;
	}
}

MIDIKnob* MidiKnobArray::insertKnobAtEnd() {
	return insertKnob(getNumElements());
}

MIDIKnob* MidiKnobArray::getElement(int i) {
	return (MIDIKnob*)getElementAddress(i);
}
