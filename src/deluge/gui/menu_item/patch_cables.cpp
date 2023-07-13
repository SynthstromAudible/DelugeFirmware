#include "patch_cables.h"
#include "gui/ui/sound_editor.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "util/functions.h"
#include "hid/display/oled.h"
#include "source_selection/regular.h"
#include "source_selection/range.h"
#include "patch_cable_strength/regular.h"
#include "patch_cable_strength/range.h"

namespace menu_item {

static char textbuf[OLED_MENU_NUM_OPTIONS_VISIBLE][24];

int PatchCables::selectedRowOnScreen;

void PatchCables::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentValue = 0;
	selectedRowOnScreen = 0;
	scrollPos = 0;

	if (navigatedBackwardFrom != nullptr) {
		PatchCableSet *set = soundEditor.currentParamManager->getPatchCableSet();
			soundEditor.currentValue = savedVal;
			if (savedVal >= set->numPatchCables) {
				soundEditor.currentValue = 0;
			}
			scrollPos = getMax(0, soundEditor.currentValue-1);
	}

#if !HAVE_OLED
	drawValue();
#endif
}

void PatchCables::readValueAgain() {
#if HAVE_OLED
	renderUIsForOled();
#else
	drawValue();
#endif
}

#if HAVE_OLED
void PatchCables::drawPixelsForOled() {
	char const* itemNames[OLED_MENU_NUM_OPTIONS_VISIBLE];
	for (int i = 0; i < OLED_MENU_NUM_OPTIONS_VISIBLE; i++) {
		itemNames[i] = nullptr;
	}

	PatchCableSet *set = soundEditor.currentParamManager->getPatchCableSet();

	for (int i = 0; i < OLED_MENU_NUM_OPTIONS_VISIBLE; i++) {
		int thisOption = scrollPos+i;
		if (thisOption >= set->numPatchCables) {
			break;
		}

		if (thisOption == soundEditor.currentValue) {
			selectedRowOnScreen = i;
		}

		PatchCable *cable = &set->patchCables[thisOption];
		int src = cable->from;
		int src2 = -1;
		ParamDescriptor desc = cable->destinationParamDescriptor;
		if (!desc.isJustAParam()) {
			src2 = desc.getTopLevelSource();
		}
		int dest = desc.getJustTheParam();

		char* buf = textbuf[i];

		const char* src_name = sourceToStringShort(src);  // exactly 4 chars
		const char* src2_name = sourceToStringShort(src2);
		const char* dest_name = getPatchedParamDisplayNameForOled(dest);

		memcpy(buf, src_name, 4);
		buf[4] = ' ';
		memcpy(buf+5, src2_name, 4);
		buf[9] = ' ';
		strncpy(buf+10, dest_name, (sizeof textbuf[i]) - 10);
		buf[(sizeof textbuf[i])-1] = 0;

		itemNames[i] = buf;
	}

	drawItemsForOled(itemNames, selectedRowOnScreen);
}

#else

void PatchCables::drawValue() {
	PatchCableSet *set = soundEditor.currentParamManager->getPatchCableSet();
	if (set->numPatchCables == 0) {
		numericDriver.setText("none", false, false);
		return;
	}

	char const* text;
	PatchCable *cable = &set->patchCables[currentValue];
	ParamDescriptor desc = cable->destinationParamDescriptor;
	const char* dest_name = getPatchedParamDisplayNameForOled(dest);

	// TODO: this is very limited. We should flash the pads associated with the src and dest
	numericDriver.setText(dest_name, false, false);
}

#endif

void PatchCables::selectEncoderAction(int offset) {
	int newValue = soundEditor.currentValue + offset;

	PatchCableSet *set = soundEditor.currentParamManager->getPatchCableSet();

#if HAVE_OLED
	if (newValue >= set->numPatchCables || newValue < 0) {
		return;
	}
#else
	if (newValue >= set->numPatchCables) {
		newValue -= set->numPatchCables;
	}
	else if (newValue < 0) {
		newValue += set->numPatchCables;
	}
#endif

	soundEditor.currentValue = newValue;

#if HAVE_OLED
	if (soundEditor.currentValue < scrollPos) {
		scrollPos = soundEditor.currentValue;
	} else if (soundEditor.currentValue >= scrollPos + OLED_MENU_NUM_OPTIONS_VISIBLE) {
		scrollPos++;
	}

	// char bufer[20];
	// intToString(scrollPos, bufer, 4);
	// OLED::popupText(bufer);

	renderUIsForOled();
#else
	drawValue();
#endif
}


MenuItem* PatchCables::selectButtonPress() {
	PatchCableSet *set = soundEditor.currentParamManager->getPatchCableSet();
	int val = soundEditor.currentValue;

	if (val >= set->numPatchCables) {
		// There were no items. If the user wants to create some, they need
		// to select a source anyway, so take them back there.
		return MenuItem::selectButtonPress();
	}
	PatchCable *cable = &set->patchCables[val];
	savedVal = val;
	ParamDescriptor desc = cable->destinationParamDescriptor;
	int dest = desc.getJustTheParam();
	soundEditor.patchingParamSelected = dest;
	if (cable->destinationParamDescriptor.isJustAParam()) {
		source_selection::regularMenu.s = cable->from;
		return &patch_cable_strength::regularMenu;
	} else {
		int src2 = desc.getTopLevelSource();
		source_selection::regularMenu.s = src2;
		source_selection::rangeMenu.s = cable->from;
		return &patch_cable_strength::rangeMenu;
	}
}

}
