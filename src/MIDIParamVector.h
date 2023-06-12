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

#ifndef MIDIPARAMVECTOR_H_
#define MIDIPARAMVECTOR_H_

#include "OrderedResizeableArray.h"

class MIDIParam;

class MIDIParamVector : public OrderedResizeableArray {
public:
	MIDIParamVector();
	MIDIParam* getElement(int i);
	MIDIParam* getParamFromCC(int cc);
	MIDIParam* insertParam(int i);
	MIDIParam* getOrCreateParamFromCC(int cc, int32_t defaultValue = 0, bool allowCreation = true);
};

#endif /* MIDIPARAMVECTOR_H_ */
