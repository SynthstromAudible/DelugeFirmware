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

#include "model/clip/clip_instance.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_clip_instance_change.h"
#include "playback/mode/session.h"
#include "util/functions.h"
#include <new>

ClipInstance::ClipInstance() {
	// TODO Auto-generated constructor stub
}

RGB ClipInstance::getColour() {
	if (!clip || clip->isArrangementOnlyClip()) {
		return RGB::monochrome(128);
	}

	return defaultClipSectionColours[clip->section];
}

void ClipInstance::change(Action* action, Output* output, int32_t newPos, int32_t newLength, Clip* newClip) {
	if (action) {
		void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceClipInstanceChange));

		if (consMemory) {
			ConsequenceClipInstanceChange* newConsequence =
			    new (consMemory) ConsequenceClipInstanceChange(output, this, newPos, newLength, newClip);
			action->addConsequence(newConsequence);
		}
	}

	pos = newPos;
	length = newLength;
	clip = newClip;
}
