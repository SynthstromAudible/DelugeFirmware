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
#include "model/song/song.h"
#include "storage/flash_storage.h"

class Editor;
class InstrumentClip;
class AudioClip;
class Clip;
class ModelStack;
class ModelStackWithTimelineCounter;

enum SessionGridMode : uint8_t {
	SessionGridModeEdit,
	SessionGridModeLaunch,
	SessionGridModeMacros,
	SessionGridModeMaxElement // Keep as boundary
};

extern float getTransitionProgress();

constexpr uint32_t kGridHeight = kDisplayHeight;

class SessionView final : public ClipNavigationTimelineView {
public:
	SessionView();
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows);
	bool opened();
	void focusRegained();

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult clipCreationButtonPressed(hid::Button i, bool on, bool routine);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);
	ActionResult horizontalEncoderAction(int32_t offset);
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void removeClip(Clip* clip);
	void redrawClipsOnScreen(bool doRender = true);
	uint32_t getMaxZoom();
	void cloneClip(uint8_t yDisplayFrom, uint8_t yDisplayTo);
	bool renderRow(ModelStack* modelStack, uint8_t yDisplay, RGB thisImage[kDisplayWidth + kSideBarWidth],
	               uint8_t thisOccupancyMask[kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	void graphicsRoutine() override;
	void potentiallyUpdateCompressorLEDs();
	int32_t displayLoopsRemainingPopup(bool ephemeral = false);
	void potentiallyRenderClipLaunchPlayhead(bool reallyNoTickSquare, int32_t sixteenthNotesRemaining);
	void requestRendering(UI* ui, uint32_t whichMainRows = 0xFFFFFFFF, uint32_t whichSideRows = 0xFFFFFFFF);

	int32_t getClipPlaceOnScreen(Clip* clip);
	void drawStatusSquare(uint8_t yDisplay, RGB thisImage[]);
	void drawSectionSquare(uint8_t yDisplay, RGB thisImage[]);
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
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	void midiLearnFlash();

	void transitionToViewForClip(Clip* clip = NULL);
	void transitionToSessionView();
	void finishedTransitioningHere();
	void playbackEnded();
	void clipNeedsReRendering(Clip* clip);
	void sampleNeedsReRendering(Sample* sample);
	Clip* getClipOnScreen(int32_t yDisplay);
	Output* getOutputFromPad(int32_t x, int32_t y);
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	ActionResult verticalScrollOneSquare(int32_t direction);

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	// 7SEG only
	void redrawNumericDisplay();
	void clearNumericDisplay();
	void displayRepeatsTilLaunch();

	uint32_t selectedClipTimePressed;
	uint8_t selectedClipYDisplay;      // Where the clip is on screen
	uint8_t selectedClipPressYDisplay; // Where the user's finger actually is on screen
	uint8_t selectedClipPressXDisplay;
	bool clipWasSelectedWithShift; // Whether shift was held when clip pad started to be held
	bool performActionOnPadRelease;
	bool performActionOnSectionPadRelease; // Keep this separate from the above one because we don't want a mod encoder
	                                       // action to set this to false
	uint8_t sectionPressed;
	uint8_t masterCompEditMode;
	int8_t selectedMacro = -1;

	Clip* getClipForLayout();
	int32_t getClipIndexForLayout();

	void copyClipName(Clip* source, Clip* target, Output* targetOutput);

	// Members for grid layout
	inline bool gridFirstPadActive() { return (gridFirstPressedX != -1 && gridFirstPressedY != -1); }
	ActionResult gridHandlePads(int32_t x, int32_t y, int32_t on);
	ActionResult gridHandleScroll(int32_t offsetX, int32_t offsetY);

	// ui
	UIType getUIType() override { return UIType::SESSION; }
	UIModControllableContext getUIModControllableContext() override { return UIModControllableContext::SONG; }

	Clip* createNewClip(OutputType outputType, int32_t yDisplay);
	bool createClip{false};
	OutputType lastTypeCreated{OutputType::NONE};

	// Grid macros config mode
	void enterMacrosConfigMode();
	void exitMacrosConfigMode();
	char const* getMacroKindString(SessionMacroKind kind);

	// Midi learn mode
	void enterMidiLearnMode();
	void exitMidiLearnMode();

	// display tempo
	void displayPotentialTempoChange(UI* ui);
	void displayTempoBPM(deluge::hid::display::oled_canvas::Canvas& canvas, StringBuf& tempoBPM, bool clearArea);
	float lastDisplayedTempo = 0;

	// display root note and scale name
	void displayCurrentRootNoteAndScaleName(deluge::hid::display::oled_canvas::Canvas& canvas,
	                                        StringBuf& rootNoteAndScaleName, bool clearArea);
	int16_t lastDisplayedRootNote = 0;

	// convert instrument clip to audio clip
	void replaceInstrumentClipWithAudioClip(Clip* clip);

	// pulse selected clip in grid view
	void gridPulseSelectedClip();

private:
	// These and other (future) commandXXX methods perform actions triggered by HID, but contain
	// no dispatch logic.
	//
	// selectEncoderAction() triggered commands
	void commandChangeSectionRepeats(int8_t offset);
	void commandChangeClipPreset(int8_t offset);
	void commandChangeCurrentSectionRepeats(int8_t offset);
	void commandChangeLayout(int8_t offset);

private:
	void renderViewDisplay();
	void sectionPadAction(uint8_t y, bool on);
	void clipPressEnded();
	void drawSectionRepeatNumber();
	void beginEditingSectionRepeatsNum();
	void goToArrangementEditor();
	void rowNeedsRenderingDependingOnSubMode(int32_t yDisplay);
	void setCentralLEDStates();

	Clip* createNewAudioClip(int32_t yDisplay);
	Clip* createNewInstrumentClip(OutputType outputType, int32_t yDisplay);

	bool createNewTrackForAudioClip(AudioClip* newClip);
	bool createNewTrackForInstrumentClip(OutputType type, InstrumentClip* clip, bool copyDrumsFromClip);

	bool insertAndResyncNewClip(Clip* newClip, int32_t yDisplay);
	void resyncNewClip(Clip* newClip, ModelStackWithTimelineCounter* modelStackWithTimelineCounter);

	// Members regarding rendering different layouts
private:
	void selectLayout(int8_t offset);
	void renderLayoutChange(bool displayPopup = true);
	void selectSpecificLayout(SessionLayoutType layout);
	SessionLayoutType previousLayout;
	SessionGridMode previousGridModeActive;

	bool sessionButtonActive = false;
	bool sessionButtonUsed = false;
	bool horizontalEncoderPressed = false;
	bool viewingRecordArmingActive = false;
	// Members for grid layout
private:
	bool gridRenderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void gridRenderActionModes(int32_t y, RGB image[][kDisplayWidth + kSideBarWidth],
	                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	bool gridRenderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                        uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);

	RGB gridRenderClipColor(Clip* clip, int32_t x, int32_t y, bool renderPulse = true);

	ActionResult gridHandlePadsEdit(int32_t x, int32_t y, int32_t on, Clip* clip);
	ActionResult gridHandlePadsLaunch(int32_t x, int32_t y, int32_t on, Clip* clip);
	ActionResult gridHandlePadsLaunchImmediate(int32_t x, int32_t y, int32_t on, Clip* clip);
	ActionResult gridHandlePadsLaunchWithSelection(int32_t x, int32_t y, int32_t on, Clip* clip);
	void gridHandlePadsWithMidiLearnPressed(int32_t x, int32_t on, Clip* clip);
	ActionResult gridHandlePadsMacros(int32_t x, int32_t y, int32_t on, Clip* clip);
	void gridHandlePadsLaunchToggleArming(Clip* clip, bool immediate);

	void gridTransitionToSessionView();
	void gridTransitionToViewForClip(Clip* clip);

	SessionGridMode gridModeSelected = SessionGridModeEdit;
	SessionGridMode gridModeActive = SessionGridModeEdit;
	bool gridActiveModeUsed = false;

	int32_t gridFirstPressedX = -1;
	int32_t gridFirstPressedY = -1;
	int32_t gridSecondPressedX = -1;
	int32_t gridSecondPressedY = -1;
	inline bool gridSecondPadInactive() { return (gridSecondPressedX == -1 && gridSecondPressedY == -1); }

	inline void gridResetPresses(bool first = true, bool second = true) {
		if (first) {
			gridFirstPressedX = -1;
			gridFirstPressedY = -1;
		}
		if (second) {
			gridSecondPressedX = -1;
			gridSecondPressedY = -1;
		}
	}

	Clip* gridCloneClip(Clip* sourceClip);
	Clip* gridCreateClipInTrack(Output* targetOutput);
	AudioClip* gridCreateAudioClipWithNewTrack();
	InstrumentClip* gridCreateInstrumentClipWithNewTrack(OutputType type);
	Clip* gridCreateClip(uint32_t targetSection, Output* targetOutput = nullptr, Clip* sourceClip = nullptr);
	void gridClonePad(uint32_t sourceX, uint32_t sourceY, uint32_t targetX, uint32_t targetY);
	void setupNewClip(Clip* newClip);

	void gridStartSection(uint32_t section, bool instant);
	void gridToggleClipPlay(Clip* clip, bool instant);

	[[nodiscard]] const size_t gridTrackCount() const;
	uint32_t gridClipCountForTrack(Output* track);
	uint32_t gridTrackIndexFromTrack(Output* track, uint32_t maxTrack);
	Output* gridTrackFromIndex(uint32_t trackIndex, uint32_t maxTrack);
	int32_t gridYFromSection(uint32_t section);
	int32_t gridSectionFromY(uint32_t y);
	int32_t gridXFromTrack(uint32_t trackIndex);
	int32_t gridTrackIndexFromX(uint32_t x, uint32_t maxTrack);
	Output* gridTrackFromX(uint32_t x, uint32_t maxTrack);
	Clip* gridClipFromCoords(uint32_t x, uint32_t y);
	int32_t gridClipIndexFromCoords(uint32_t x, uint32_t y);
	Cartesian gridXYFromClip(Clip& clip);

	void gridSetDefaultMode() {
		switch (FlashStorage::defaultGridActiveMode) {
		case GridDefaultActiveModeGreen: {
			gridModeSelected = SessionGridModeLaunch;
			break;
		}
		case GridDefaultActiveModeBlue: {
			gridModeSelected = SessionGridModeEdit;
			break;
		}
		}
	}

	void setupTrackCreation() const;
	void exitTrackCreation();

	// selected clip pulsing in grid view

	/// @brief disable selected clip pulsing
	void gridStopSelectedClipPulsing();

	/// @brief reset blend position for pulse
	void gridResetSelectedClipPulseProgress();

	/// @brief render pulse for selected clip
	void gridSelectClipForPulsing(Clip& clip);

	/// @brief check if we should stop pulsing
	bool gridCheckForPulseStop();

	bool gridSelectedClipPulsing = false;   // are we doing any pulsing
	Clip* selectedClipForPulsing = nullptr; // selected clip we are pulsing
	RGB gridSelectedClipRenderedColour;     // last pulse colour we rendered
	bool blendDirection = false;            // direction we're blending towards
	int32_t progress = 0;                   // pulse blend slider position

	static constexpr int32_t kMinProgress = 1;                           // min position to reach in blend slider
	static constexpr int32_t kMaxProgressFull = (65535 / 100) * 60;      // max position to reach for unmuted clip
	static constexpr int32_t kMaxProgressDim = 1000;                     // max position to reach for muted clip
	static constexpr int32_t kPulseRate = 50;                            // speed of timer in ms
	static constexpr int32_t kBlendRate = 60;                            // speed of blending colours
	static constexpr int32_t kBlendOffsetFull = kPulseRate * kBlendRate; // amount to move slider for unmuted clip
	static constexpr int32_t kBlendOffsetDim = kPulseRate;               // amount to move slider for muted clip
};

extern SessionView sessionView;
