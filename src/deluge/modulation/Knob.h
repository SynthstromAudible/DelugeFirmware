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

#ifndef KNOB_H_
#define KNOB_H_

#include "r_typedefs.h"
#include "LearnedMIDI.h"
#include "ParamDescriptor.h"

class Knob {
public:
	Knob() {}

	virtual bool isRelative() = 0;
	virtual bool is14Bit() = 0;
	virtual bool topValueIs127() = 0;
	ParamDescriptor paramDescriptor;
};

class MIDIKnob : public Knob {
public:
	MIDIKnob() {}
	bool isRelative() { return relative; }
	bool is14Bit() { return (midiInput.noteOrCC == 128); }
	bool topValueIs127() { return (midiInput.noteOrCC < 128 && !relative); }
	LearnedMIDI midiInput;
	bool relative;
};

class ModKnob : public Knob {
public:
	bool isRelative() { return true; }
	bool is14Bit() { return false; }
	bool topValueIs127() { return false; }
};

#endif /* KNOB_H_ */
