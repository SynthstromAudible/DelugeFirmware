#include "patch_cables.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "patch_cable_strength/range.h"
#include "patch_cable_strength/regular.h"
#include "processing/sound/sound.h"
#include "source_selection/range.h"
#include "source_selection/regular.h"
#include "util/container/static_vector.hpp"
#include "util/functions.h"

namespace deluge::gui::menu_item {

void PatchCables::beginSession(MenuItem* navigatedBackwardFrom) {
	currentValue = 0;

	if (navigatedBackwardFrom != nullptr) {
		PatchCableSet* set = soundEditor.currentParamManager->getPatchCableSet();
		currentValue = savedVal;
		if (savedVal >= set->numPatchCables) {
			currentValue = 0;
		}
	}

	if (display->haveOLED()) {
		scrollPos = std::max((int32_t)0, currentValue - 1);
	}

	renderOptions();
	readValueAgain();
}

void PatchCables::readValueAgain() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
	blinkShortcutsSoon();
}

void PatchCables::renderOptions() {
	options.clear();
	PatchCableSet* set = soundEditor.currentParamManager->getPatchCableSet();

	for (int i = 0; i < set->numPatchCables; i++) {
		PatchCable* cable = &set->patchCables[i];
		PatchSource src = cable->from;
		PatchSource src2 = PatchSource::NOT_AVAILABLE;
		ParamDescriptor desc = cable->destinationParamDescriptor;
		if (!desc.isJustAParam()) {
			src2 = desc.getTopLevelSource();
		}
		int dest = desc.getJustTheParam();

		const int item_max_len = 30;
		static char bufs[kMaxNumPatchCables][item_max_len];
		char* buf = bufs[i];

		const char* src_name = sourceToStringShort(src); // exactly 4 chars
		const char* dest_name = patchedParamToStringShort(dest);

		memcpy(buf, src_name, 4);
		buf[4] = ' ';
		int off = 0;
		if (!desc.isJustAParam()) {
			const char* src2_name = sourceToStringShort(src2);
			memcpy(buf + 5, src2_name, 4);
			buf[9] = ' ';
			off = 5;
		}

		int32_t param_value = cable->param.getCurrentValue();
		int32_t level = ((int64_t)param_value * 5000 + (1 << 29)) >> 30;

		floatToString((float)level / 100, buf + off + 5, 2, 2);
		//fmt::vformat_to_n(buf + off + 5, 5, "{:4}", fmt::make_format_args();

		buf[off + 9] = ' ';
		strncpy(buf + off + 10, dest_name, item_max_len - 10 - off);
		buf[item_max_len - 1] = 0;

		options.push_back(buf);
	}
}

void PatchCables::drawPixelsForOled() {
	drawItemsForOled(options, currentValue - scrollPos, scrollPos);
}

void PatchCables::drawValue() {
	PatchCableSet* set = soundEditor.currentParamManager->getPatchCableSet();
	if (set->numPatchCables == 0) {
		display->setText("none", false, false);
		return;
	}

	display->setScrollingText(options[currentValue].begin());
}

void PatchCables::selectEncoderAction(int32_t offset) {
	int32_t newValue = currentValue + offset;

	PatchCableSet* set = soundEditor.currentParamManager->getPatchCableSet();

	if (display->haveOLED()) {
		if (newValue >= set->numPatchCables || newValue < 0) {
			return;
		}
	}
	else {
		if (newValue >= set->numPatchCables) {
			newValue -= set->numPatchCables;
		}
		else if (newValue < 0) {
			newValue += set->numPatchCables;
		}
	}

	currentValue = newValue;

	if (display->haveOLED()) {
		if (currentValue < scrollPos) {
			scrollPos = currentValue;
		}
		else if (currentValue >= scrollPos + kOLEDMenuNumOptionsVisible) {
			scrollPos++;
		}
	}

	readValueAgain(); // redraw
}

void PatchCables::blinkShortcutsSoon() {
	// some throttling so menu scrolling doesn't become a lightning storm of flashes
	uiTimerManager.setTimer(TIMER_UI_SPECIFIC, display->haveOLED() ? 500 : 200);
	uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
}

ActionResult PatchCables::timerCallback() {
	blinkShortcuts();
	return ActionResult::DEALT_WITH;
}

void PatchCables::blinkShortcuts() {
	PatchCableSet* set = soundEditor.currentParamManager->getPatchCableSet();
	PatchCable* cable = &set->patchCables[currentValue];
	ParamDescriptor desc = cable->destinationParamDescriptor;
	int dest = desc.getJustTheParam();

	if (dest == ::Param::Global::VOLUME_POST_REVERB_SEND || dest == ::Param::Local::VOLUME) {
		dest = ::Param::Global::VOLUME_POST_FX;
	}

	int32_t x, y;
	if (soundEditor.findPatchedParam(dest, &x, &y)) {
		soundEditor.setupShortcutBlink(x, y, 3);
	}

	PatchSource src = cable->from;
	PatchSource src2 = PatchSource::NOT_AVAILABLE;
	if (!desc.isJustAParam()) {
		src2 = desc.getTopLevelSource();
	}
	blinkSrc = src;
	blinkSrc2 = src2;
	soundEditor.updateSourceBlinks(this);

	soundEditor.blinkShortcut();
}

uint8_t PatchCables::shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) {
	if (s == blinkSrc) {
		*colour = 0b110;
		return 0;
	}
	else if (s == blinkSrc2) {
		return 3; // something #patchingoverhaul2021
	}
	return 255;
}

MenuItem* PatchCables::selectButtonPress() {
	PatchCableSet* set = soundEditor.currentParamManager->getPatchCableSet();
	int val = currentValue;

	if (val >= set->numPatchCables) {
		// There were no items. If the user wants to create some, they need
		// to select a source anyway, so take them back there.
		return MenuItem::selectButtonPress();
	}
	PatchCable* cable = &set->patchCables[val];
	savedVal = val;
	ParamDescriptor desc = cable->destinationParamDescriptor;
	int dest = desc.getJustTheParam();
	soundEditor.patchingParamSelected = dest;

	options.clear();
	if (cable->destinationParamDescriptor.isJustAParam()) {
		source_selection::regularMenu.s = cable->from;
		return &patch_cable_strength::regularMenu;
	}
	else {
		PatchSource src2 = desc.getTopLevelSource();
		source_selection::regularMenu.s = src2;
		source_selection::rangeMenu.s = cable->from;
		return &patch_cable_strength::rangeMenu;
	}
}

} // namespace deluge::gui::menu_item
