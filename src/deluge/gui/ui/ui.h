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

#ifndef UI_h
#define UI_h

#include "definitions.h"

class RootUI;
class ClipMinder;
class MIDIDevice;

extern uint32_t currentUIMode;

// Exclusive UI modes - only one of these can be active at a time.
#define UI_MODE_NONE 0
#define UI_MODE_HORIZONTAL_ZOOM 2
#define UI_MODE_VERTICAL_SCROLL 1
#define UI_MODE_INSTRUMENT_CLIP_COLLAPSING 4
#define UI_MODE_INSTRUMENT_CLIP_EXPANDING 5
#define UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING 7
#define UI_MODE_CLIP_PRESSED_IN_SONG_VIEW 9
#define UI_MODE_MIDI_LEARN 11
#define UI_MODE_NOTES_PRESSED 12
#define UI_MODE_SCALE_MODE_BUTTON_PRESSED 14
#define UI_MODE_SOLO_BUTTON_HELD 15
#define UI_MODE_TAP_TEMPO 18
// Gaps here
#define UI_MODE_WAITING_FOR_NEXT_FILE_TO_LOAD 27
#define UI_MODE_ADDING_DRUM_NOTEROW 28
// Gaps here
#define UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED 33
#define UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES 34
#define UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED 35
#define UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED 36
#define UI_MODE_LOADING_SONG_NEW_SONG_PLAYING 37
#define UI_MODE_SELECTING_MIDI_CC 38
#define UI_MODE_HOLDING_SECTION_PAD 40
#define UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION 41
#define UI_MODE_HOLDING_ARRANGEMENT_ROW 42
#define UI_MODE_EXPLODE_ANIMATION 43
#define UI_MODE_ANIMATION_FADE 44
#define UI_MODE_RECORD_COUNT_IN 45
#define UI_MODE_HOLDING_SAMPLE_MARKER 46
#define UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS 47
#define UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR 48
#define UI_MODE_HOLDING_BACKSPACE 49
#define UI_MODE_VIEWING_RECORD_ARMING 50
#define UI_MODE_HOLDING_SAVE_BUTTON 51
#define UI_MODE_HOLDING_LOAD_BUTTON 52
#define UI_MODE_PREDICTING_QWERTY_TEXT 53
#define UI_MODE_AUDIO_CLIP_EXPANDING 54
#define UI_MODE_AUDIO_CLIP_COLLAPSING 55
#define UI_MODE_MODULATING_PARAM_HOLDING_ENCODER_DOWN 58
#define UI_MODE_PATCHING_SOURCE_HOLDING_BUTTON_DOWN 59
#define UI_MODE_MACRO_SETTING_UP 60
#define UI_MODE_DRAGGING_KIT_NOTEROW 61

#define EXCLUSIVE_UI_MODES_MASK ((uint32_t)255)

// Non-exclusive UI modes, which can (if the code allows) occur at the same time as other ones, including the "exclusive" ones above.
#define UI_MODE_STUTTERING (1 << 28)
#define UI_MODE_HORIZONTAL_SCROLL (1 << 29)
#define UI_MODE_AUDITIONING (1 << 30)
#define UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON ((uint32_t)1 << 31)

class UI {
public:
	UI();

	virtual int padAction(int x, int y, int velocity) { return ACTION_RESULT_DEALT_WITH; }
	virtual int buttonAction(int x, int y, bool on, bool inCardRoutine) { return ACTION_RESULT_NOT_DEALT_WITH; }
	virtual int horizontalEncoderAction(int offset) { return ACTION_RESULT_DEALT_WITH; }
	virtual int verticalEncoderAction(int offset, bool inCardRoutine) { return ACTION_RESULT_DEALT_WITH; }
	virtual void selectEncoderAction(int8_t offset) {}
	virtual void modEncoderAction(int whichModEncoder, int offset);
	virtual void modButtonAction(uint8_t whichButton, bool on);
	virtual void modEncoderButtonAction(uint8_t whichModEncoder, bool on);

	virtual void graphicsRoutine();
	virtual int timerCallback() { return ACTION_RESULT_DEALT_WITH; }

	virtual bool opened() {
		focusRegained();
		return true;
	}
	virtual void focusRegained() {}
	virtual bool canSeeViewUnderneath() { return false; }
	virtual ClipMinder* toClipMinder() { return NULL; }
	virtual void scrollFinished() {}
	virtual bool noteOnReceivedForMidiLearn(MIDIDevice* fromDevice, int channel, int note, int velocity) {
		return false;
	} // Returns whether it was used, I think?

	virtual bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
		return false;
	} // Returning false means obey UI under me

	// When these return false it means they're transparent, showing what's underneath.
	// These *must* check whether image has been supplied - if not, just return, saying whether opaque or not.
	// Cos we need to be able to quiz these without actually getting any rendering done.
	virtual bool renderMainPads(uint32_t whichRows = 0, uint8_t image[][displayWidth + sideBarWidth][3] = NULL,
	                            uint8_t occupancyMask[][displayWidth + sideBarWidth] = NULL,
	                            bool drawUndefinedArea = true) {
		return false;
	}
	virtual bool renderSidebar(uint32_t whichRows = 0, uint8_t image[][displayWidth + sideBarWidth][3] = NULL,
	                           uint8_t occupancyMask[][displayWidth + sideBarWidth] = NULL) {
		return false;
	}

	void close();

#if HAVE_OLED
	virtual void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) = 0;
	bool oledShowsUIUnderneath;
#endif
};

// UIs
UI* getCurrentUI();
RootUI* getRootUI();
UI* getUIUpOneLevel(int numLevelsUp);
static UI* getUIUpOneLevel() {
	return getUIUpOneLevel(1);
}
void closeUI(UI* ui);
bool openUI(UI* newUI);
void changeRootUI(UI* newUI);
bool changeUISideways(UI* newUI);
bool changeUIAtLevel(UI* newUI, int level);
bool isUIOpen(UI* ui);
void setRootUILowLevel(UI* newUI);
void swapOutRootUILowLevel(UI* newUI);
void nullifyUIs();
bool rootUIIsTimelineView();
bool rootUIIsClipMinderScreen();
void getUIGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);

void uiNeedsRendering(UI* ui, uint32_t whichMainRows = 0xFFFFFFFF, uint32_t whichSideRows = 0xFFFFFFFF);
void renderingNeededRegardlessOfUI(uint32_t whichMainRows = 0xFFFFFFFF, uint32_t whichSideRows = 0xFFFFFFFF);
void clearPendingUIRendering();

void doAnyPendingUIRendering();

#if HAVE_OLED
void renderUIsForOled();
#endif

// UI modes
bool isUIModeActive(uint32_t uiMode);
bool isUIModeActiveExclusively(uint32_t uiMode);
bool isUIModeWithinRange(const uint32_t* modes);
bool isNoUIModeActive();
void exitUIMode(uint32_t uiMode);
void enterUIMode(uint32_t uiMode);

#endif
