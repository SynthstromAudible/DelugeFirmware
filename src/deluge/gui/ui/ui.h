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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "hid/button.h"
#include "hid/display/oled_canvas/canvas.h"

class ClipMinder;
class MIDIDevice;
class RootUI;
class TimelineView;

extern uint32_t currentUIMode;
extern bool pendingUIRenderingLock;

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
// Gaps here
#define UI_MODE_WAITING_FOR_NEXT_FILE_TO_LOAD 27
#define UI_MODE_ADDING_DRUM_NOTEROW 28
#define UI_MODE_CREATING_CLIP 29
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
#define UI_MODE_HOLDING_STATUS_PAD 62
#define UI_MODE_IMPLODE_ANIMATION 63
#define UI_MODE_STEM_EXPORT 64
#define UI_MODE_HOLDING_SONG_BUTTON 65

#define EXCLUSIVE_UI_MODES_MASK ((uint32_t)255)

// Non-exclusive UI modes, which can (if the code allows) occur at the same time as other ones, including the
// "exclusive" ones above.
#define UI_MODE_QUANTIZE (1 << 27)
#define UI_MODE_STUTTERING (1 << 28)
#define UI_MODE_HORIZONTAL_SCROLL (1 << 29)
#define UI_MODE_AUDITIONING (1 << 30)
#define UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON ((uint32_t)1 << 31)

#define LONG_PRESS_DURATION 400
class UI {
public:
	virtual ~UI() = default;
	UI();

	virtual ActionResult padAction(int32_t x, int32_t y, int32_t velocity) { return ActionResult::DEALT_WITH; }
	virtual ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
		return ActionResult::NOT_DEALT_WITH;
	}
	virtual ActionResult horizontalEncoderAction(int32_t offset) { return ActionResult::DEALT_WITH; }
	virtual ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) { return ActionResult::DEALT_WITH; }
	virtual void selectEncoderAction(int8_t offset) {}
	virtual void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	virtual void modButtonAction(uint8_t whichButton, bool on);
	virtual void modEncoderButtonAction(uint8_t whichModEncoder, bool on);

	virtual void graphicsRoutine();
	virtual ActionResult timerCallback() { return ActionResult::DEALT_WITH; }

	virtual bool opened() {
		focusRegained();
		return true;
	}

	virtual void focusRegained() {}
	// the `display` and/or `chosenLanguage` object changed. redraw accordingly.
	virtual void displayOrLanguageChanged() {}
	virtual bool canSeeViewUnderneath() { return false; }
	/// Convert this clip to a clip minder. Returns true for views which manage a single clip,
	/// false for song level views
	virtual ClipMinder* toClipMinder() { return NULL; }

	/// \brief Convert this view to a TimelineView
	///
	/// \return \c nullptr if the view is not a TimelineView, otherwise \c this cast to a TimelineView
	virtual TimelineView* toTimelineView() { return nullptr; }

	virtual void scrollFinished() {}
	virtual bool pcReceivedForMidiLearn(MIDIDevice* fromDevice, int32_t channel, int32_t program) { return false; }

	virtual bool noteOnReceivedForMidiLearn(MIDIDevice* fromDevice, int32_t channel, int32_t note, int32_t velocity) {
		return false;
	} // Returns whether it was used, I think?

	virtual bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
		return false;
	} // Returning false means obey UI under me

	// When these return false it means they're transparent, showing what's underneath.
	// These *must* check whether image has been supplied - if not, just return, saying whether opaque or not.
	// Cos we need to be able to quiz these without actually getting any rendering done.
	virtual bool renderMainPads(uint32_t whichRows = 0, RGB image[][kDisplayWidth + kSideBarWidth] = NULL,
	                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = NULL,
	                            bool drawUndefinedArea = true) {
		return false;
	}
	virtual bool renderSidebar(uint32_t whichRows = 0, RGB image[][kDisplayWidth + kSideBarWidth] = NULL,
	                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = NULL) {
		return false;
	}
	// called when back is held, used to exit menus or similar full screen views completely
	/// returns whether a UI exited
	virtual bool exitUI() { return false; };
	void close();

	virtual void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) = 0;
	bool oledShowsUIUnderneath;

	/// \brief When entering a UI (e.g. automationView), you may wish to open a different UI
	///        based on the current context (e.g. automationViewArranger, automationViewAudioClip, etc.)
	virtual UI* getUI() { return this; }
	/// \brief What type of UI is this? e.g. UIType::ARRANGER
	virtual UIType getUIType() = 0;
	/// \brief What context does UI relate to? e.g. UIType could be AUTOMATION but UIContextType could be
	///		   ARRANGER, AUDIO CLIP, INSTRUMENT CLIP
	virtual UIType getUIContextType() { return getUIType(); }
	/// \brief What mod controllable context is this UI using? E.g. Automation View can use the Song
	///		   ModControllable when in Arranger but the Clip ModControllable when in a Clip.
	virtual UIModControllableContext getUIModControllableContext() { return UIModControllableContext::NONE; }
#if ENABLE_MATRIX_DEBUG
	const char* getUIName();
#endif

protected:
	UIType uiType;
};

// UIs
UI* getCurrentUI();
RootUI* getRootUI();
UI* getUIUpOneLevel(int32_t numLevelsUp);
static UI* getUIUpOneLevel() {
	return getUIUpOneLevel(1);
}
void closeUI(UI* ui);
bool openUI(UI* newUI);
void changeRootUI(UI* newUI);
bool changeUISideways(UI* newUI);
bool changeUIAtLevel(UI* newUI, int32_t level);
bool isUIOpen(UI* ui);
void setRootUILowLevel(UI* newUI);
void swapOutRootUILowLevel(UI* newUI);
void nullifyUIs();
bool currentUIIsClipMinderScreen();
bool rootUIIsClipMinderScreen();
std::pair<uint32_t, uint32_t> getUIGreyoutColsAndRows();

void uiNeedsRendering(UI* ui, uint32_t whichMainRows = 0xFFFFFFFF, uint32_t whichSideRows = 0xFFFFFFFF);
void renderingNeededRegardlessOfUI(uint32_t whichMainRows = 0xFFFFFFFF, uint32_t whichSideRows = 0xFFFFFFFF);
void clearPendingUIRendering();

void doAnyPendingUIRendering();

void renderUIsForOled();

// UI modes
bool isUIModeActive(uint32_t uiMode);
bool isUIModeActiveExclusively(uint32_t uiMode);
bool isUIModeWithinRange(const uint32_t* modes);
bool isNoUIModeActive();
void exitUIMode(uint32_t uiMode);
void enterUIMode(uint32_t uiMode);
