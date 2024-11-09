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

// === INCLUDES	=== //

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

// === CONSTANTS === //
constexpr uint32_t kGridHeight = kDisplayHeight;

// === EXTERN STUFF === //
extern float getTransitionProgress();
extern const uint8_t numDefaultClipGroupColours;
extern const uint8_t defaultClipGroupColours[];

// === THE CLASS === //
class SongView final : public ClipNavigationTimelineView {
public:
	// Constructor
	SongView();

	// Pulicly used properties
	int8_t selectedMacro = -1;
	uint8_t selectedClipYDisplay;      // Where the clip is on screen
	uint8_t selectedClipPressYDisplay; // Where the user's finger actually is on screen
	uint8_t selectedClipPressXDisplay;
	bool performActionOnPadRelease;
	int16_t lastDisplayedRootNote = 0;
	float lastDisplayedTempo = 0;

	// Inherited from UI, general
	bool opened();
	void focusRegained();
	const char* getName() { return "song_view"; }
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows);
	UIType getUIType() { return UIType::SONG_VIEW; }

	// Inherited from UI, rendering
	void graphicsRoutine();
	ActionResult timerCallback();
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	// Inherited from UI, physical interactions
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	ActionResult horizontalEncoderAction(int32_t offset);
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	void selectEncoderAction(int8_t offset);
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
	void modButtonAction(uint8_t whichButton, bool on);
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);

	// === UI INFO & STATE === //
	uint32_t getMaxZoom();
	int32_t getClipPlaceOnScreen(Clip* clip);
	uint32_t getMaxLength();
	bool setupScroll(uint32_t oldScroll);
	uint32_t getClipLocalScroll(Clip* loopable, uint32_t overviewScroll, uint32_t xZoom);
	uint32_t getGreyedOutRowsNotRepresentingOutput(Output* output);
	void finishedTransitioningHere();
	void playbackEnded();
	void clipNeedsReRendering(Clip* clip);
	void sampleNeedsReRendering(Sample* sample);
	Clip* getClipOnScreen(int32_t yDisplay);
	Output* getOutputFromPad(int32_t x, int32_t y);
	Clip* getClipForLayout();
	OutputType lastTypeCreated{OutputType::NONE};

	// === UI INPUT === //
	ActionResult clipCreationButtonPressed(hid::Button i, bool on, bool routine);

	// === DO STUFF IN THE UI === //
	void removeClip(Clip* clip);
	void redrawClipsOnScreen(bool doRender = true);
	bool renderRow(ModelStack* modelStack, uint8_t yDisplay, RGB thisImage[kDisplayWidth + kSideBarWidth],
	               uint8_t thisOccupancyMask[kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	int32_t displayLoopsRemainingPopup(bool ephemeral = false);
	void cloneClip(uint8_t yDisplayFrom, uint8_t yDisplayTo);
	void transitionToViewForClip(Clip* clip = NULL);
	void transitionToSongView();
	ActionResult verticalScrollOneSquare(int32_t direction);
	Clip* createNewClip(OutputType outputType, int32_t yDisplay);
	bool createClip{false};
	void enterMidiLearnMode();
	void exitMidiLearnMode();
	void replaceInstrumentClipWithAudioClip(Clip* clip);

	// === RENDERING === //
	void requestRendering(UI* ui, uint32_t whichMainRows = 0xFFFFFFFF, uint32_t whichSideRows = 0xFFFFFFFF);
	void drawStatusSquare(uint8_t yDisplay, RGB thisImage[]);
	void drawSectionSquare(uint8_t yDisplay, RGB thisImage[]);
	void midiLearnFlash();
	void displayPotentialTempoChange(UI* ui);
	void displayTempoBPM(deluge::hid::display::oled_canvas::Canvas& canvas, StringBuf& tempoBPM, bool clearArea);
	void displayCurrentRootNoteAndScaleName(deluge::hid::display::oled_canvas::Canvas& canvas,
	                                        StringBuf& rootNoteAndScaleName, bool clearArea);
	void potentiallyRenderClipLaunchPlayhead(bool reallyNoTickSquare, int32_t sixteenthNotesRemaining);
	void flashPlayRoutine();

	// === NOT SURE === //
	bool calculateZoomPinSquares(uint32_t oldScroll, uint32_t newScroll, uint32_t newZoom, uint32_t oldZoom);
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow);
	void setLedStates();
	void editNumRepeatsTilLaunch(int32_t offset);

	// === 7SEG === //
	void redrawNumericDisplay();
	void clearNumericDisplay();
	void displayRepeatsTilLaunch();

	// === PUBLIC GRID STUFF === //
	void enterMacrosConfigMode();
	void exitMacrosConfigMode();
	char const* getMacroKindString(SessionMacroKind kind);
	inline bool gridFirstPadActive() { return (gridFirstPressedX != -1 && gridFirstPressedY != -1); }
	ActionResult gridHandlePads(int32_t x, int32_t y, int32_t on);
	ActionResult gridHandleScroll(int32_t offsetX, int32_t offsetY);

private:
	// These and other (future) commandXXX methods perform actions triggered by HID, but contain
	// no dispatch logic.
	//
	// selectEncoderAction() triggered commands
	void commandChangeSectionRepeats(int8_t offset);
	void commandChangeClipPreset(int8_t offset);
	void commandChangeCurrentSectionRepeats(int8_t offset);
	void commandChangeLayout(int8_t offset);

	// === PROPERTIES === //
	uint32_t selectedClipTimePressed;

	bool clipWasSelectedWithShift;         // Whether shift was held when clip pad started to be held
	bool performActionOnSectionPadRelease; // Keep this separate from the above one because we don't want a mod encoder
	                                       // action to set this to false
	uint8_t sectionPressed;
	uint8_t masterCompEditMode;
	bool songViewButtonActive = false;
	bool songViewButtonUsed = false;
	bool horizontalEncoderPressed = false;
	bool viewingRecordArmingActive = false;

	// === UI RENDER === //
	void renderViewDisplay(); // render oled
	void drawSectionRepeatNumber();
	void rowNeedsRenderingDependingOnSubMode(int32_t yDisplay);
	void setCentralLEDStates();

	// === UI === //
	void sectionPadAction(uint8_t y, bool on); // input
	void clipPressEnded();                     // state

	// === DO STUFF IN THE UI === //
	void goToArrangementEditor();
	void beginEditingSectionRepeatsNum();
	Clip* createNewAudioClip(int32_t yDisplay);
	Clip* createNewInstrumentClip(OutputType outputType, int32_t yDisplay);
	bool createNewTrackForAudioClip(AudioClip* newClip);
	bool createNewTrackForInstrumentClip(OutputType type, InstrumentClip* clip, bool copyDrumsFromClip);
	bool insertAndResyncNewClip(Clip* newClip, int32_t yDisplay);
	void resyncNewClip(Clip* newClip, ModelStackWithTimelineCounter* modelStackWithTimelineCounter);

	// Members regarding rendering different layouts
	void selectLayout(int8_t offset);
	void renderLayoutChange(bool displayPopup = true);
	void selectSpecificLayout(SongViewLayout layout);
	SongViewLayout previousLayout;
	SongViewGridLayoutMode previousGridModeActive;

	// Members for grid layout
	bool gridRenderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void gridRenderActionModes(int32_t y, RGB image[][kDisplayWidth + kSideBarWidth],
	                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	bool gridRenderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                        uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);

	RGB gridRenderClipColor(Clip* clip);

	ActionResult gridHandlePadsEdit(int32_t x, int32_t y, int32_t on, Clip* clip);
	ActionResult gridHandlePadsLaunch(int32_t x, int32_t y, int32_t on, Clip* clip);
	ActionResult gridHandlePadsLaunchImmediate(int32_t x, int32_t y, int32_t on, Clip* clip);
	ActionResult gridHandlePadsLaunchWithSelection(int32_t x, int32_t y, int32_t on, Clip* clip);
	void gridHandlePadsWithMidiLearnPressed(int32_t x, int32_t on, Clip* clip);
	ActionResult gridHandlePadsMacros(int32_t x, int32_t y, int32_t on, Clip* clip);
	void gridHandlePadsLaunchToggleArming(Clip* clip, bool immediate);

	void gridTransitionToSessionView();
	void gridTransitionToViewForClip(Clip* clip);

	SongViewGridLayoutMode gridModeSelected = SongViewGridLayoutMode::Edit;
	SongViewGridLayoutMode gridModeActive = SongViewGridLayoutMode::Edit;
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

	const uint32_t gridTrackCount();
	uint32_t gridClipCountForTrack(Output* track);
	uint32_t gridTrackIndexFromTrack(Output* track, uint32_t maxTrack);
	Output* gridTrackFromIndex(uint32_t trackIndex, uint32_t maxTrack);
	int32_t gridYFromSection(uint32_t section);
	int32_t gridSectionFromY(uint32_t y);
	int32_t gridXFromTrack(uint32_t trackIndex);
	int32_t gridTrackIndexFromX(uint32_t x, uint32_t maxTrack);
	Output* gridTrackFromX(uint32_t x, uint32_t maxTrack);
	Clip* gridClipFromCoords(uint32_t x, uint32_t y);

	inline void gridSetDefaultMode() {
		switch (FlashStorage::defaultGridActiveMode) {
		case SongViewGridLayoutModeSelection::DefaultLaunch: {
			gridModeSelected = SongViewGridLayoutMode::Launch;
			break;
		}
		case SongViewGridLayoutModeSelection::DefaultEdit: {
			gridModeSelected = SongViewGridLayoutMode::Edit;
			break;
		}
		}
	}
	void setupTrackCreation() const;
	void exitTrackCreation();
};

// === GLOBAL VARIABLE === //
extern SongView songView;
