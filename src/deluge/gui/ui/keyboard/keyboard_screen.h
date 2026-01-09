/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/ui/keyboard/notes_state.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/ui.h"
#include "hid/button.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/instrument/instrument.h"

class ModelStack;
class Instrument;

namespace deluge::gui::ui::keyboard {

constexpr int32_t kMaxNumKeyboardPadPresses = 10;

class KeyboardScreen final : public RootUI, public InstrumentClipMinder {
public:
	KeyboardScreen();

	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	void selectEncoderAction(int8_t offset) override;

	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                    bool drawUndefinedArea = false) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;

	void flashDefaultRootNote();
	void openedInBackground();
	void exitAuditionMode();

	uint8_t highlightedNotes[kHighestKeyboardNote] = {0};
	uint8_t nornsNotes[kHighestKeyboardNote] = {0};

	inline void requestRendering() { uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF); }

	/// Helper methods for keyboard layouts to access rendering system
	inline void requestMainPadsRendering() { uiNeedsRendering(this, 0xFFFFFFFF, 0); }
	inline void requestSpecificRowsRendering(uint32_t rowMask) { uiNeedsRendering(this, rowMask, 0); }
	inline void requestSidebarRendering() { uiNeedsRendering(this, 0, 0xFFFFFFFF); }

	void killColumnSwitchKey(int32_t column);

	// ui
	UIType getUIType() override { return UIType::KEYBOARD_SCREEN; }
	UIType getUIContextType() override { return UIType::INSTRUMENT_CLIP; }
	UIModControllableContext getUIModControllableContext() override { return UIModControllableContext::CLIP; }
	void checkNewInstrument(Instrument* newInstrument);

private:
	bool opened() override;
	void focusRegained() override;
	void displayOrLanguageChanged() final;

	void evaluateActiveNotes();
	void updateActiveNotes();

	void noteOff(ModelStack& modelStack, Instrument& activeInstrument, bool clipIsActiveOnInstrument, int32_t note);

	ClipMinder* toClipMinder() override { return this; }
	void setLedStates();
	void graphicsRoutine() override;
	bool getAffectEntire() override;

	void unscrolledPadAudition(int32_t velocity, int32_t note, bool shiftButtonDown);

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override {
		InstrumentClipMinder::renderOLED(canvas);
	}

	void selectLayout(int8_t offset);
	void enterScaleMode(int32_t selectedRootNote = kDefaultCalculateRootNote);
	void exitScaleMode();
	void drawNoteCode(int32_t noteCode);

	PressedPad pressedPads[kMaxNumKeyboardPadPresses];
	NotesState lastNotesState;
	NotesState currentNotesState;

	bool keyboardButtonActive = false;
	bool keyboardButtonUsed = false;
	bool yEncoderActive = false;
	bool xEncoderActive = false;
};
}; // namespace deluge::gui::ui::keyboard

// TODO: should get moved into namespace once project namespacing is complete
extern deluge::gui::ui::keyboard::KeyboardScreen keyboardScreen;
