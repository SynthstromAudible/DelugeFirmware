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

#include <ActionClipState.h>
#include <InstrumentClip.h>
#include "definitions.h"
#include "kit.h"

ActionClipState::ActionClipState() {
}

ActionClipState::~ActionClipState() {
}

void ActionClipState::grabFromClip(Clip* thisClip) {
	//modKnobMode = thisClip->modKnobMode;

	if (thisClip->type == CLIP_TYPE_INSTRUMENT) {
		InstrumentClip* instrumentClip = (InstrumentClip*)thisClip;
		yScrollSessionView[BEFORE] = instrumentClip->yScroll;
		affectEntire = instrumentClip->affectEntire;
		wrapEditing = instrumentClip->wrapEditing;
		wrapEditLevel = instrumentClip->wrapEditLevel;

		if (thisClip->output->type != INSTRUMENT_TYPE_KIT) {
			selectedDrumIndex = -1;
		}
		else {
			Kit* kit = (Kit*)thisClip->output;
			if (!kit->selectedDrum) {
				selectedDrumIndex = -1;
			}
			else {
				selectedDrumIndex = kit->getDrumIndex(kit->selectedDrum);
			}
		}
	}
}
