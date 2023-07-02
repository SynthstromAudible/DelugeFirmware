/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#ifndef MENUITEMWITHCCLEARNING_H_
#define MENUITEMWITHCCLEARNING_H_

#include "modulation/params/param_descriptor.h"

class MenuItemWithCCLearning {
public:
	MenuItemWithCCLearning();

	virtual ParamDescriptor getLearningThing() = 0;
	void unlearnAction();
	bool allowsLearnMode() { return true; }
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel);
};

#endif /* MENUITEMWITHCCLEARNING_H_ */
