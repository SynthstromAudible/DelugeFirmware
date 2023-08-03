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
#include "gui/views/clip_navigation_timeline_view.h"
#include "hid/button.h"

class Editor;
class InstrumentClip;
class Clip;
class ModelStack;

extern float getTransitionProgress();

// Clip Group colours
extern const uint8_t numDefaultClipGroupColours;
extern const uint8_t defaultClipGroupColours[];

class SessionView final : public ClipNavigationTimelineView {
public:
	SessionView();
	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);
	bool opened();
	void focusRegained();

	ActionResult buttonAction(hid::Button b, bool on, bool inCardRoutine);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);
	ActionResult horizontalEncoderAction(int32_t offset);
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void removeClip(uint8_t yDisplay);
	void redrawClipsOnScreen(bool doRender = true);
	uint32_t getMaxZoom();
	void cloneClip(uint8_t yDisplayFrom, uint8_t yDisplayTo);
	bool renderRow(ModelStack* modelStack, uint8_t yDisplay, uint8_t thisImage[kDisplayWidth + kSideBarWidth][3],
	               uint8_t thisOccupancyMask[kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	void graphicsRoutine();

	int32_t getClipPlaceOnScreen(Clip* clip);
	void drawStatusSquare(uint8_t yDisplay, uint8_t thisImage[][3]);
	void drawSectionSquare(uint8_t yDisplay, uint8_t thisImage[][3]);
	bool calculateZoomPinSquares(uint32_t oldScroll, uint32_t newScroll, uint32_t newZoom, uint32_t oldZoom);
	uint32_t getMaxLength();
	bool setupScroll(uint32_t oldScroll);
	uint32_t getClipLocalScroll(Clip* loopable, uint32_t overviewScroll, uint32_t xZoom);
	void flashPlayRoutine();

	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
	void modButtonAction(uint8_t whichButton, bool on);
	void selectEncoderAction(int8_t offset);
	ActionResult timerCallback();
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow);
	void setLedStates();
	void editNumRepeatsTilLaunch(int32_t offset);
	uint32_t getGreyedOutRowsNotRepresentingOutput(Output* output);
	bool renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	void midiLearnFlash();

	void transitionToViewForClip(Clip* clip = NULL);
	void finishedTransitioningHere();
	void playbackEnded();
	void clipNeedsReRendering(Clip* clip);
	void sampleNeedsReRendering(Sample* sample);
	Clip* getClipOnScreen(int32_t yDisplay);
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	ActionResult verticalScrollOneSquare(int32_t direction);

	// OLED only
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);

	// 7SEG only
	void redrawNumericDisplay();

	uint32_t selectedClipTimePressed;
	uint8_t selectedClipYDisplay;      // Where the clip is on screen
	uint8_t selectedClipPressYDisplay; // Where the user's finger actually is on screen
	uint8_t selectedClipPressXDisplay;
	bool performActionOnPadRelease;
	bool
	    performActionOnSectionPadRelease; // Keep this separate from the above one because we don't want a mod encoder action to set this to false
	uint8_t sectionPressed;
	uint8_t masterCompEditMode;

private:
	void sectionPadAction(uint8_t y, bool on);
	void clipPressEnded();
	void drawSectionRepeatNumber();
	void beginEditingSectionRepeatsNum();
	Clip* createNewInstrumentClip(int32_t yDisplay);
	void goToArrangementEditor();
	void replaceInstrumentClipWithAudioClip();
	void replaceAudioClipWithInstrumentClip(InstrumentType instrumentType);
	void rowNeedsRenderingDependingOnSubMode(int32_t yDisplay);
	void setCentralLEDStates();
};

extern SessionView sessionView;
