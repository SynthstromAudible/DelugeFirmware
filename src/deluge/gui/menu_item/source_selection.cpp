/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "source_selection.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "patch_cable_strength.h"
#include "modulation/patch/patch_cable_set.h"

namespace menu_item {
const uint8_t sourceMenuContents[] = {
    PATCH_SOURCE_ENVELOPE_0, PATCH_SOURCE_ENVELOPE_1, PATCH_SOURCE_LFO_GLOBAL, PATCH_SOURCE_LFO_LOCAL,
    PATCH_SOURCE_VELOCITY,   PATCH_SOURCE_NOTE,       PATCH_SOURCE_COMPRESSOR, PATCH_SOURCE_RANDOM,
    PATCH_SOURCE_X,          PATCH_SOURCE_Y,          PATCH_SOURCE_AFTERTOUCH,
};

SourceSelection::SourceSelection() {
}

uint8_t SourceSelection::shouldDrawDotOnValue() {
	return soundEditor.currentParamManager->getPatchCableSet()->isSourcePatchedToDestinationDescriptorVolumeInspecific(
	           s, getDestinationDescriptor())
	           ? 3
	           : 255;
}

#if HAVE_OLED

int SourceSelection::selectedRowOnScreen;

void SourceSelection::drawPixelsForOled() {

	char const* itemNames[OLED_MENU_NUM_OPTIONS_VISIBLE];
	for (int i = 0; i < OLED_MENU_NUM_OPTIONS_VISIBLE; i++) {
		itemNames[i] = NULL;
	}

	selectedRowOnScreen = 0;

	int thisOption = scrollPos;
	int i = 0;

	while (i < OLED_MENU_NUM_OPTIONS_VISIBLE) {
		if (thisOption >= NUM_PATCH_SOURCES) {
			break;
		}

		int sHere = sourceMenuContents[thisOption];

		if (sourceIsAllowed(sHere)) {
			itemNames[i] = getSourceDisplayNameForOLED(sHere);
			if (thisOption == soundEditor.currentValue) {
				selectedRowOnScreen = i;
			}
			i++;
		}
		else {
			if (thisOption == scrollPos) {
				scrollPos++;
			}
		}
		thisOption++;
	}

	drawItemsForOled(itemNames, selectedRowOnScreen);
}

#else

void SourceSelection::drawValue() {
	char const* text;
	switch (sourceMenuContents[soundEditor.currentValue]) {
	case PATCH_SOURCE_LFO_GLOBAL:
		text = "LFO1";
		break;

	case PATCH_SOURCE_LFO_LOCAL:
		text = "LFO2";
		break;

	case PATCH_SOURCE_ENVELOPE_0:
		text = "ENV1";
		break;

	case PATCH_SOURCE_ENVELOPE_1:
		text = "ENV2";
		break;

	case PATCH_SOURCE_COMPRESSOR:
		text = "SIDE";
		break;

	case PATCH_SOURCE_VELOCITY:
		text = "VELOCITY";
		break;

	case PATCH_SOURCE_NOTE:
		text = "NOTE";
		break;

	case PATCH_SOURCE_RANDOM:
		text = "RANDOM";
		break;

	case PATCH_SOURCE_AFTERTOUCH:
		text = "AFTERTOUCH";
		break;

	case PATCH_SOURCE_X:
		text = "X";
		break;

	case PATCH_SOURCE_Y:
		text = "Y";
		break;
	}

	uint8_t drawDot = shouldDrawDotOnValue();

	numericDriver.setText(text, false, drawDot);
}

#endif

void SourceSelection::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentValue = 0;

	if (navigatedBackwardFrom) {
		while (sourceMenuContents[soundEditor.currentValue] != s) {
			soundEditor.currentValue++;
		}
		// Scroll pos will be retained from before.
	}
	else {
		int firstAllowedIndex = NUM_PATCH_SOURCES - 1;
		while (true) {
			s = sourceMenuContents[soundEditor.currentValue];

			// If patching already exists on this source, we use this as the initial one to show to the user
			if (soundEditor.currentParamManager->getPatchCableSet()
			        ->isSourcePatchedToDestinationDescriptorVolumeInspecific(s, getDestinationDescriptor())) {
				break;
			}

			// Note down the first "allowed" or "editable" source
			if (soundEditor.currentValue < firstAllowedIndex && sourceIsAllowed(s)) {
				firstAllowedIndex = soundEditor.currentValue;
			}

			soundEditor.currentValue++;
#if HAVE_OLED
			scrollPos = soundEditor.currentValue;
#endif

			if (soundEditor.currentValue >= NUM_PATCH_SOURCES) {
				soundEditor.currentValue = firstAllowedIndex;
#if HAVE_OLED
				scrollPos = soundEditor.currentValue;
#endif
				s = sourceMenuContents[soundEditor.currentValue];
				break;
			}
		}
	}

#if !HAVE_OLED
	drawValue();
#endif
}

void SourceSelection::readValueAgain() {
#if HAVE_OLED
	renderUIsForOled();
#else
	drawValue();
#endif
}

void SourceSelection::selectEncoderAction(int offset) {
	bool isAllowed;
	int newValue = soundEditor.currentValue;
	do {
		newValue += offset;

#if HAVE_OLED
		if (newValue >= NUM_PATCH_SOURCES || newValue < 0) {
			return;
		}
#else
		if (newValue >= NUM_PATCH_SOURCES)
			newValue -= NUM_PATCH_SOURCES;
		else if (newValue < 0)
			newValue += NUM_PATCH_SOURCES;
#endif
		s = sourceMenuContents[newValue];

	} while (!sourceIsAllowed(s));

	soundEditor.currentValue = newValue;

#if HAVE_OLED
	if (soundEditor.currentValue < scrollPos) {
		scrollPos = soundEditor.currentValue;
	}
	else if (offset >= 0 && selectedRowOnScreen == OLED_MENU_NUM_OPTIONS_VISIBLE - 1) {
		scrollPos++;
	}

	renderUIsForOled();
#else
	drawValue();
#endif
}

bool SourceSelection::sourceIsAllowed(int source) {
	ParamDescriptor destinationDescriptor = getDestinationDescriptor();

	// If patching to another cable's range...
	if (!destinationDescriptor.isJustAParam()) {
		// Global source - can control any range
		if (source < FIRST_LOCAL_SOURCE) {
			return true;
		}

		// Local source - range must be for cable going to local param
		else {
			return destinationDescriptor.getJustTheParam() < FIRST_GLOBAL_PARAM;
		}
	}

	int p = destinationDescriptor.getJustTheParam();

	// Check that this source is allowed to be patched to the selected param
	if (p == PARAM_GLOBAL_VOLUME_POST_FX) {
		return (soundEditor.currentSound->maySourcePatchToParam(
		            source, PARAM_GLOBAL_VOLUME_POST_FX, (ParamManagerForTimeline*)soundEditor.currentParamManager)
		            != PATCH_CABLE_ACCEPTANCE_DISALLOWED
		        || soundEditor.currentSound->maySourcePatchToParam(
		               source, PARAM_LOCAL_VOLUME, (ParamManagerForTimeline*)soundEditor.currentParamManager)
		               != PATCH_CABLE_ACCEPTANCE_DISALLOWED
		        || soundEditor.currentSound->maySourcePatchToParam(
		               source, PARAM_GLOBAL_VOLUME_POST_REVERB_SEND,
		               (ParamManagerForTimeline*)soundEditor.currentParamManager)
		               != PATCH_CABLE_ACCEPTANCE_DISALLOWED);
	}
	else {
		return (soundEditor.currentSound->maySourcePatchToParam(
		            source, p, (ParamManagerForTimeline*)soundEditor.currentParamManager)
		        != PATCH_CABLE_ACCEPTANCE_DISALLOWED);
	}
}

uint8_t SourceSelection::getIndexOfPatchedParamToBlink() {
	return soundEditor.patchingParamSelected;
}

uint8_t SourceSelection::shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) {
	return soundEditor.currentParamManager->getPatchCableSet()->isSourcePatchedToDestinationDescriptorVolumeInspecific(
	           s, getDestinationDescriptor())
	           ? 3
	           : 255;
}

} // namespace menu_item
