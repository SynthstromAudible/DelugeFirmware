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

#include "gui/ui/root_ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "util/misc.h"
#include <utility>

using deluge::hid::display::OLED;

UI::UI() {
	oledShowsUIUnderneath = false;
}

void UI::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	view.modEncoderAction(whichModEncoder, offset);
}

void UI::modButtonAction(uint8_t whichButton, bool on) {
	view.modButtonAction(whichButton, on);
}

void UI::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	view.modEncoderButtonAction(whichModEncoder, on);
}

void UI::graphicsRoutine() {
	if (getRootUI() && canSeeViewUnderneath()) {
		getRootUI()->graphicsRoutine();
	}
}

void UI::close() {
	closeUI(this);
}

constexpr size_t kUiNavigationHistoryLength = 16;
static std::array<UI*, kUiNavigationHistoryLength> uiNavigationHierarchy;

int32_t numUIsOpen = 0; // Will be 0 again during song load / swap

UI* lastUIBeforeNullifying = nullptr;

/**
 * @brief Get the greyout rows and columns for the current UI
 *
 * @return std::pair<uint32_t, uint32_t> a pair with [rows, columns]
 */
std::pair<uint32_t, uint32_t> getUIGreyoutColsAndRows() {
	uint32_t cols = 0;
	uint32_t rows = 0;
	for (int32_t u = numUIsOpen - 1; u >= 0; u--) {
		bool useThis = uiNavigationHierarchy[u]->getGreyoutColsAndRows(&cols, &rows);
		if (useThis) {
			return std::make_pair(cols, rows);
		}
	}
	return std::make_pair(0, 0);
}

bool changeUIAtLevel(UI* newUI, int32_t level) {
	UI* oldUI = getCurrentUI();
	UI* oldRootUI = uiNavigationHierarchy[level];
	int32_t oldNumUIs = numUIsOpen;
	uiNavigationHierarchy[level] = newUI;
	numUIsOpen = level + 1;

	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	PadLEDs::reassessGreyout();
	bool success = newUI->opened();

	if (!success) {
		numUIsOpen = oldNumUIs;
		uiNavigationHierarchy[level] = oldRootUI;
		PadLEDs::reassessGreyout();
		oldUI->focusRegained();
	}
	return success;
}

// Called when we navigate between "root" UIs, like sessionView, instrumentClipView, automationView,
// performanceView, etc.
void changeRootUI(UI* newUI) {
	newUI = newUI->getUI();
	uiNavigationHierarchy[0] = newUI;
	numUIsOpen = 1;

	if (currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW) {
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	}
	PadLEDs::reassessGreyout();
	newUI->opened(); // These all can't fail, I guess.

	if (display->haveOLED()) {
		renderUIsForOled();
	}
}

// Only called when setting up blank song, so don't worry about this
void setRootUILowLevel(UI* newUI) {
	newUI = newUI->getUI();
	uiNavigationHierarchy[0] = newUI;
	numUIsOpen = 1;
	PadLEDs::reassessGreyout();
}

bool changeUISideways(UI* newUI) {
	newUI = newUI->getUI();
	bool success = changeUIAtLevel(newUI, numUIsOpen - 1);
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	return success;
}

UI* getCurrentUI() {
	if (numUIsOpen == 0) {
		return lastUIBeforeNullifying; // Very ugly work-around to stop everything breaking
	}
	return uiNavigationHierarchy[numUIsOpen - 1];
}

// This will be NULL while waiting to swap songs, so you'd better check for this anytime you're gonna call a function on
// the result!
RootUI* getRootUI() {
	if (numUIsOpen == 0) {
		return NULL;
	}
	return (RootUI*)uiNavigationHierarchy[0];
}

bool currentUIIsClipMinderScreen() {
	UI* currentUI = getCurrentUI();
	return (currentUI != nullptr && (currentUI->toClipMinder() != nullptr));
}

bool rootUIIsClipMinderScreen() {
	UI* rootUI = getRootUI();
	return (rootUI != nullptr && (rootUI->toClipMinder() != nullptr));
}

void swapOutRootUILowLevel(UI* newUI) {
	newUI = newUI->getUI();
	uiNavigationHierarchy[0] = newUI;
}

UI* getUIUpOneLevel(int32_t numLevelsUp) {
	if (numUIsOpen < (1 + numLevelsUp)) {
		return NULL;
	}
	else {
		return uiNavigationHierarchy[numUIsOpen - 1 - numLevelsUp];
	}
}

// If UI not found, chaos
void closeUI(UI* uiToClose) {

	bool redrawMainPads = false;
	bool redrawSidebar = false;

	int32_t u;
	for (u = numUIsOpen - 1; u >= 1; u--) {

		UI* thisUI = uiNavigationHierarchy[u];
		redrawMainPads |= thisUI->renderMainPads();
		redrawSidebar |= thisUI->renderSidebar();

		if (thisUI == uiToClose) {
			break;
		}
	}

	UI* newUI = uiNavigationHierarchy[u - 1];
	numUIsOpen = u;

	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	PadLEDs::reassessGreyout();
	newUI->focusRegained();
	if (display->haveOLED()) {
		renderUIsForOled();
	}

	bool redrawMainPadsOrig = redrawMainPads;
	bool redrawSidebarOrig = redrawSidebar;

	for (u = numUIsOpen - 1; u >= 0; u--) {
		if (!redrawMainPads && !redrawSidebar) {
			break;
		}

		UI* thisUI = uiNavigationHierarchy[u];
		if (redrawMainPads) {
			redrawMainPads = !thisUI->renderMainPads(0xFFFFFFFF, PadLEDs::image, PadLEDs::occupancyMask);
		}
		if (redrawSidebar) {
			redrawSidebar = !thisUI->renderSidebar(0xFFFFFFFF, PadLEDs::image, PadLEDs::occupancyMask);
		}
	}

	if (redrawMainPadsOrig) {
		PadLEDs::sendOutMainPadColours();
	}
	if (redrawSidebarOrig) {
		PadLEDs::sendOutSidebarColours();
	}
}

bool openUI(UI* newUI) {
	newUI = newUI->getUI();
	UI* oldUI = getCurrentUI();
	uiNavigationHierarchy[numUIsOpen] = newUI;
	numUIsOpen++;

	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	PadLEDs::reassessGreyout();
	bool success = newUI->opened();

	if (!success) {
		numUIsOpen--;
		PadLEDs::reassessGreyout();
		oldUI->focusRegained(); // Or maybe we should instead let the caller deal with this failure, and call this if
		                        // they wish?
	}
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	return success;
}

bool isUIOpen(UI* ui) {
	for (int32_t u = 0; u < numUIsOpen; u++) {
		if (uiNavigationHierarchy[u] == ui) {
			return true;
		}
	}
	return false;
}
bool doesOLEDNeedRendering = false;

void nullifyUIs() {
	lastUIBeforeNullifying = getCurrentUI();
	numUIsOpen = 0;
	doesOLEDNeedRendering = false;
}

void renderUIsForOled() {
	doesOLEDNeedRendering = true;
}

uint32_t whichMainRowsNeedRendering = 0;
uint32_t whichSideRowsNeedRendering = 0;

void clearPendingUIRendering() {
	whichMainRowsNeedRendering = whichSideRowsNeedRendering = 0;
}

void renderingNeededRegardlessOfUI(uint32_t whichMainRows, uint32_t whichSideRows) {
	whichMainRowsNeedRendering |= whichMainRows;
	whichSideRowsNeedRendering |= whichSideRows;
}

void uiNeedsRendering(UI* ui, uint32_t whichMainRows, uint32_t whichSideRows) {

	// We might be in the middle of an audio routine or something, so just see whether the selected bit of the UI is
	// visible

	for (int32_t u = numUIsOpen - 1; u >= 0; u--) {
		UI* thisUI = uiNavigationHierarchy[u];
		if (ui == thisUI) {
			whichMainRowsNeedRendering |= whichMainRows;
			whichSideRowsNeedRendering |= whichSideRows;
			break;
		}

		if (whichMainRows && thisUI->renderMainPads()) {
			whichMainRows = 0;
		}
		if (whichSideRows && thisUI->renderSidebar()) {
			whichSideRows = 0;
		}

		if (!whichMainRows && !whichSideRows) {
			break;
		}
	}
}

void doAnyPendingGridRendering() {

	if (!whichMainRowsNeedRendering && !whichSideRowsNeedRendering) {
		return;
	}

	if (currentUIMode == UI_MODE_HORIZONTAL_SCROLL || currentUIMode == UI_MODE_HORIZONTAL_ZOOM) {
		return;
	}
	// Make a local copy of our instructions
	uint32_t mainRowsNow = whichMainRowsNeedRendering;
	uint32_t sideRowsNow = whichSideRowsNeedRendering;

	// Clear the overall instructions - so it may now be written to again during this function call
	clearPendingUIRendering();

	for (int32_t u = numUIsOpen - 1; u >= 0; u--) {

		if (!mainRowsNow && !sideRowsNow) {
			break;
		}

		UI* thisUI = uiNavigationHierarchy[u];

		if (mainRowsNow) {
			bool usedUp = thisUI->renderMainPads(mainRowsNow, PadLEDs::image, PadLEDs::occupancyMask);
			if (usedUp) {
				if (!whichMainRowsNeedRendering) {
					PadLEDs::sendOutMainPadColours();
				}
				mainRowsNow = 0;
			}
		}

		if (sideRowsNow) {
			bool usedUp = thisUI->renderSidebar(sideRowsNow, PadLEDs::image, PadLEDs::occupancyMask);
			if (usedUp) {
				if (!whichSideRowsNeedRendering) {
					PadLEDs::sendOutSidebarColours();
				}
				sideRowsNow = 0;
			}
		}
	}
}

void doAnyPendingOLEDRendering() {
	if (doesOLEDNeedRendering) {
		int32_t u = numUIsOpen - 1;
		while ((u > 0) && uiNavigationHierarchy[u]->oledShowsUIUnderneath) {
			u--;
		}

		OLED::clearMainImage();
		u = std::max(u, 0L);
		for (; u < numUIsOpen; u++) {
			OLED::stopScrollingAnimation();
			uiNavigationHierarchy[u]->renderOLED(deluge::hid::display::OLED::main);
		}

		// Don't need to mark dirty because clearMainImage has already done that for us

		doesOLEDNeedRendering = false;
	}

	OLED::sendMainImage();
}

bool pendingUIRenderingLock = false;

void doAnyPendingUIRendering() {
	if (pendingUIRenderingLock) {
		return; // There's no point going in here multiple times inside each other
	}

	if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= (kNumBytesInMainPadRedraw + kNumBytesInSidebarRedraw) * 2) {
		return; // Trialling the *2 to fix flickering when flicking through presets very fast
	}

	pendingUIRenderingLock = true;

	doAnyPendingGridRendering();
	doAnyPendingOLEDRendering();

	pendingUIRenderingLock = false;
}

uint32_t currentUIMode = 0;

bool isUIModeActive(uint32_t uiMode) {
	if (uiMode > EXCLUSIVE_UI_MODES_MASK) {
		return (currentUIMode & uiMode);
	}
	else {
		uint32_t exclusivesOnly = currentUIMode & EXCLUSIVE_UI_MODES_MASK;
		return (exclusivesOnly == uiMode);
	}
}

bool isUIModeActiveExclusively(uint32_t uiMode) {
	return (currentUIMode == uiMode);
}

// Checks that all of the currently active UI modes are within the list of modes provided. As well as making things
// tidy, the main point of this is to still return true when more than one of the modes on the list provided is active.
// Terminate the list with a 0.
bool isUIModeWithinRange(const uint32_t* modes) {
	uint32_t exclusivesOnly = currentUIMode & EXCLUSIVE_UI_MODES_MASK;
	uint32_t nonExclusivesOnly = currentUIMode & ~EXCLUSIVE_UI_MODES_MASK;
	while (*modes) {
		// If looking at an exclusive mode...
		if (*modes <= EXCLUSIVE_UI_MODES_MASK) {
			if (*modes == exclusivesOnly) {
				exclusivesOnly = 0;
			}
		}

		// Or if looking at a non-exclusive mode...
		else {
			nonExclusivesOnly &= ~*modes;
		}
		modes++;
	}

	return (!exclusivesOnly && !nonExclusivesOnly);
}

bool isNoUIModeActive() {
	return !currentUIMode;
}

// You can safely call this even if you don't know whether said UI mode is active.
void exitUIMode(uint32_t uiMode) {
	if (uiMode > EXCLUSIVE_UI_MODES_MASK) {
		currentUIMode = currentUIMode & ~uiMode;
	}
	else {
		if ((currentUIMode & EXCLUSIVE_UI_MODES_MASK) == uiMode) {
			currentUIMode = currentUIMode & ~EXCLUSIVE_UI_MODES_MASK;
		}
	}
}

void enterUIMode(uint32_t uiMode) {
	if (uiMode > EXCLUSIVE_UI_MODES_MASK) {
		currentUIMode |= uiMode;
	}
	else {
		currentUIMode = (currentUIMode & ~EXCLUSIVE_UI_MODES_MASK) | uiMode;
	}
}

#if ENABLE_MATRIX_DEBUG
EnumStringMap<UIType, util::to_underlying(UIType::UI_TYPE_COUNT)> uiTypeMap = {
    {{{UIType::ARRANGER, "arranger"},
      {UIType::AUDIO_CLIP, "audio_clip"},
      {UIType::AUDIO_RECORDER, "audio_recorder"},
      {UIType::AUTOMATION, "automation"},
      {UIType::CONTEXT_MENU, "context_menu"},
      {UIType::DX_BROWSER, "dx_browser"},
      {UIType::INSTRUMENT_CLIP, "instrument_clip"},
      {UIType::KEYBOARD_SCREEN, "keyboard_screen"},
      {UIType::LOAD_INSTRUMENT_PRESET, "load_instrument_preset"},
      {UIType::LOAD_MIDI_DEVICE_DEFINITION, "load_midi_device_definition"},
      {UIType::LOAD_PATTERN, "load_pattern"},
      {UIType::LOAD_SONG, "load_song"},
      {UIType::PERFORMANCE, "performance"},
      {UIType::RENAME, "rename"},
      {UIType::SAMPLE_BROWSER, "sample_browser"},
      {UIType::SAMPLE_MARKER_EDITOR, "sample_marker_editor"},
      {UIType::SAVE_INSTRUMENT_PRESET, "save_instrument_preset"},
      {UIType::SAVE_KIT_ROW, "save_kit_row"},
      {UIType::SAVE_MIDI_DEVICE_DEFINITION, "save_midi_device_definition"},
      {UIType::SAVE_PATTERN, "save_pattern"},
      {UIType::SAVE_SONG, "save_song"},
      {UIType::SESSION, "session"},
      {UIType::SLICER, "slicer"},
      {UIType::SOUND_EDITOR, "sound_editor"}}}};

const char* UI::getUIName() {
	return uiTypeMap(getUIType());
}
#endif
