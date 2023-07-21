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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/views/timeline_view.h"
#include "hid/button.h"

class ArrangementRow;
class ClipInstance;
class InstrumentClip;
class Instrument;
class ParamManagerForTimeline;
class Instrument;
class ArpeggiatorSettings;
class NoteRow;
class Kit;
class Action;
class Drum;
class ModelStack;
class ModelStackWithNoteRow;

class ArrangerView final : public TimelineView {
public:
	ArrangerView();
	bool opened();
	void focusRegained();
	ActionResult padAction(int x, int y, int velocity) override;
	ActionResult buttonAction(hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult verticalEncoderAction(int offset, bool inCardRoutine) override;
	void selectEncoderAction(int8_t offset);

	void repopulateOutputsOnScreen(bool doRender = true);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void drawMuteSquare(int yDisplay, uint8_t thisImage[][3]);
	bool renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	bool renderRow(ModelStack* modelStack, int yDisplay, int32_t xScroll, uint32_t xZoom, uint8_t* thisImage,
	               uint8_t thisOccupancyMask[], int renderWidth);
	void editPadAction(int x, int y, bool on);
	ActionResult horizontalEncoderAction(int offset) override;
	uint32_t getMaxLength();
	unsigned int getMaxZoom();
	void graphicsRoutine();
	int getNavSysId() { return NAVIGATION_ARRANGEMENT; }
	void navigateThroughPresets(int offset);
	void notifyActiveClipChangedOnOutput(Output* output);
	ActionResult timerCallback() override;
	void reassessWhetherDoingAutoScroll(int32_t pos = -1);
	void autoScrollOnPlaybackEnd();
	bool initiateXScroll(int32_t newScrollPos);
	bool supportsTriplets() { return false; }
	bool putDraggedClipInstanceInNewPosition(Output* output);
	void tellMatrixDriverWhichRowsContainSomethingZoomable();
	void scrollFinished();
	void notifyPlaybackBegun();
	uint32_t getGreyedOutRowsNotRepresentingOutput(Output* output);
	void playbackEnded();
	void clipNeedsReRendering(Clip* clip);
	void exitSubModeWithoutAction();
	bool transitionToArrangementEditor();
	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);
	void setLedStates();
	ActionResult verticalScrollOneSquare(int direction);
	ActionResult horizontalScrollOneSquare(int direction);
#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
#endif

	Output* outputsOnScreen[kDisplayHeight];
	int8_t yPressedEffective;
	int8_t yPressedActual;
	int8_t xPressed;
	bool actionOnDepress;
	uint32_t pressTime;
	int32_t desiredLength;
	int32_t originallyPressedClipActualLength;
	bool pressedHead;

	int pressedClipInstanceIndex;
	Output* pressedClipInstanceOutput;
	int32_t pressedClipInstanceXScrollWhenLastInValidPosition;
	bool pressedClipInstanceIsInValidPosition;

	bool blinkOn;

	bool doingAutoScrollNow;
	bool mustRedrawTickSquares;

	int autoScrollNumSquaresBehind;

	int lastInteractedOutputIndex;
	int lastInteractedPos;
	uint8_t lastInteractedSection;

	int lastTickSquare;

	int32_t xScrollWhenPlaybackStarted;

private:
	void changeInstrumentType(InstrumentType newInstrumentType);
	void moveClipToSession();
	void auditionPadAction(bool on, int y);
	void beginAudition(Output* output);
	void endAudition(Output* output, bool evenIfPlaying = false);
	ModelStackWithNoteRow* getNoteRowForAudition(ModelStack* modelStack, Kit* kit);
	Drum* getDrumForAudition(Kit* kit);
	void drawAuditionSquare(int yDisplay, uint8_t thisImage[][3]);
	void setNoSubMode();
	void outputActivated(Output* output);
	void outputDeactivated(Output* output);
	void transitionToClipView(ClipInstance* clipInstance);
	void deleteClipInstance(Output* output, int clipInstanceIndex, ClipInstance* clipInstance, Action* action,
	                        bool clearingWholeArrangement = false);
	void clearArrangement();
	void rememberInteractionWithClipInstance(int yDisplay, ClipInstance* clipInstance);
	void deleteOutput();
	void auditionEnded();
	void goToSongView();
	void changeOutputToAudio();
	bool renderRowForOutput(ModelStack* modelStack, Output* output, int32_t xScroll, uint32_t xZoom, uint8_t* image,
	                        uint8_t occupancyMask[], int renderWidth, int ignoreI);
	Instrument* createNewInstrument(InstrumentType newInstrumentType, bool* instrumentAlreadyInSong);
	void changeOutputToInstrument(InstrumentType newInstrumentType);
	uint32_t doActualRender(int32_t xScroll, uint32_t xZoom, uint32_t whichRows, uint8_t* image,
	                        uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int renderWidth, int imageWidth);
};

extern ArrangerView arrangerView;
