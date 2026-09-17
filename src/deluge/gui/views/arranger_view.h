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
#include "gui/colour/colour.h"
#include "gui/views/timeline_view.h"

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
namespace deluge::storage {
struct AcquireResult;
}

class ArrangerView final : public TimelineView {
public:
	ArrangerView();
	bool opened() override;
	void focusRegained() override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult handleEditPadAction(int32_t x, int32_t y, int32_t velocity);
	ActionResult handleStatusPadAction(int32_t y, int32_t velocity, UI* ui);
	ActionResult handleAuditionPadAction(int32_t y, int32_t velocity, UI* ui);
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	void selectEncoderAction(int8_t offset) override;
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;

	void repopulateOutputsOnScreen(bool doRender = true);
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	void drawMuteSquare(int32_t yDisplay, RGB thisImage[]);
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	bool renderRow(ModelStack* modelStack, int32_t yDisplay, int32_t xScroll, uint32_t xZoom, RGB* thisImage,
	               uint8_t thisOccupancyMask[], int32_t renderWidth);
	void editPadAction(int32_t x, int32_t y, bool on);
	ActionResult horizontalEncoderAction(int32_t offset) override;
	void shiftAutomationHorizontally(int32_t offset, int32_t scroll_amount, Action* action);
	void shiftClipsHorizontally(int32_t offset, int32_t scroll_amount, Action* action);
	uint32_t getMaxLength() override;
	uint32_t getMaxZoom() override;
	void graphicsRoutine() override;
	[[nodiscard]] int32_t getNavSysId() const override { return NAVIGATION_ARRANGEMENT; }
	void navigateThroughPresets(int32_t offset);
	void notifyActiveClipChangedOnOutput(Output* output);
	ActionResult timerCallback() override;
	void reassessWhetherDoingAutoScroll(int32_t pos = -1);
	void autoScrollOnPlaybackEnd();
	bool initiateXScroll(int32_t newScrollPos);
	[[nodiscard]] bool supportsTriplets() const override { return false; }
	bool putDraggedClipInstanceInNewPosition(Output* output);
	void tellMatrixDriverWhichRowsContainSomethingZoomable() override;
	void scrollFinished() override;
	void notifyPlaybackBegun() override;
	uint32_t getGreyedOutRowsNotRepresentingOutput(Output* output) override;
	void playbackEnded() override;
	void clipNeedsReRendering(Clip* clip) override;

	/// @brief The new-instrument pad gesture's follow-up, run on the storage owner.
	///
	/// Runs the WHOLE remainder of the pad press, not just the row placement: the audition sub-mode,
	/// the output name, the note and the mod-controllable all live here, because the pad handler
	/// returns the instant it dispatches and on the Embassy backing there is no Output on the row yet
	/// for it to act on. The audition half is gated on the finger still being down on the same pad --
	/// nothing else records a release for this gesture, since the press never entered
	/// UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION in the first place.
	/// @param yPressed yPressedEffective captured at dispatch (see PresetRequest).
	/// @param yPressedRaw The audition-pad row the press landed on, before that clamp -- what the pad
	///        matrix is asked about.
	/// @param ui The UI the press came through, whose rows the repaint marks; null falls back to this
	///        view, matching exitSubModeWithoutAction().
	/// @param acquired The Synth InstrumentLoad acquired, or why there is none.
	void completeNewInstrument(int32_t yPressed, int32_t yPressedRaw, UI* ui,
	                           const deluge::storage::AcquireResult& acquired);
	/// @brief changeOutputType()'s follow-up: inline for MIDI/CV, which read no card; for Synth/Kit, on the interaction
	///        tier once InstrumentLoad has acquired the Instrument.
	/// @param acquired Synth/Kit: what InstrumentLoad acquired. nullptr for MIDI/CV.
	void completeChangeOutputType(OutputType newOutputType, Instrument* oldInstrument, int32_t yPressed,
	                              const deluge::storage::AcquireResult* acquired = nullptr);
	/// @brief navigateThroughPresets()'s follow-up; see completeChangeOutputType() for the tier split.
	/// @param acquired Synth/Kit: what InstrumentLoad acquired. nullptr for MIDI/CV.
	void completeNavigateThroughPresets(int32_t offset, Output* output, int32_t yPressed,
	                                    const deluge::storage::AcquireResult* acquired = nullptr);
	void exitSubModeWithoutAction(UI* ui = nullptr);
	bool transitionToArrangementEditor();
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override;
	void setLedStates();
	ActionResult verticalScrollOneSquare(int32_t direction);
	ActionResult horizontalScrollOneSquare(int32_t direction);

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	Output* outputsOnScreen[kDisplayHeight]{};
	int8_t yPressedEffective{};
	int8_t yPressedActual{};
	int8_t xPressed{};
	bool actionOnDepress{};
	uint32_t pressTime{};
	int32_t desiredLength{};
	int32_t originallyPressedClipActualLength{};
	bool pressedHead{};

	int32_t pressedClipInstanceIndex{};
	Output* pressedClipInstanceOutput{};
	int32_t pressedClipInstanceXScrollWhenLastInValidPosition{};
	bool pressedClipInstanceIsInValidPosition{};

	bool blinkOn{};

	bool doingAutoScrollNow;
	bool mustRedrawTickSquares{};

	int32_t autoScrollNumSquaresBehind{};

	int32_t lastInteractedOutputIndex;
	int32_t lastInteractedPos;
	uint8_t lastInteractedSection;
	ClipInstance* lastInteractedClipInstance;

	int32_t lastInteractedArrangementPos;

	int32_t lastTickSquare{};

	int32_t xScrollWhenPlaybackStarted{};

	// ui
	UIType getUIType() override { return UIType::ARRANGER; }
	UIModControllableContext getUIModControllableContext() override { return UIModControllableContext::SONG; }

	Clip* getClipForSelection();

	void requestRendering(UI* ui, uint32_t whichMainRows = 0xFFFFFFFF, uint32_t whichSideRows = 0xFFFFFFFF);

private:
	void changeOutputType(OutputType newOutputType);
	void moveClipToSession();
	void auditionPadAction(bool on, int32_t y, UI* ui);
	void beginAudition(Output* output);
	void endAudition(Output* output, bool evenIfPlaying = false);
	ModelStackWithNoteRow* getNoteRowForAudition(ModelStack* modelStack, Kit* kit);
	Drum* getDrumForAudition(Kit* kit);
	void drawAuditionSquare(int32_t yDisplay, RGB thisImage[]);
	void setNoSubMode();
	void outputActivated(Output* output);
	void outputDeactivated(Output* output);
	void transitionToClipView(ClipInstance* clipInstance);
	void deleteClipInstance(Output* output, int32_t clipInstanceIndex, ClipInstance* clipInstance, Action* action,
	                        bool clearingWholeArrangement = false);
	void clearArrangement();
	void interactWithClipInstance(Output* output, int32_t yDisplay, ClipInstance* clipInstance);
	void rememberInteractionWithClipInstance(int32_t yDisplay, ClipInstance* clipInstance);
	void deleteOutput();
	void auditionEnded();
	void goToSongView();
	void changeOutputToAudio();
	bool renderRowForOutput(ModelStack* modelStack, Output* output, int32_t xScroll, uint32_t xZoom, RGB* image,
	                        uint8_t occupancyMask[], int32_t renderWidth, int32_t ignoreI);
	/// @brief Take the Synth a new-instrument gesture acquired: off the hibernation list, and set up without a clip.
	/// @return The Instrument, or nullptr when the acquisition failed (its error shown).
	Instrument* createNewInstrument(bool* instrumentAlreadyInSong, const deluge::storage::AcquireResult& acquired);
	/// @brief The new-instrument acquisition gesture: dispatch onto the storage owner a request to
	///        create a SYNTH Instrument for the empty row at `yPressed` and place it on screen (see
	///        completeNewInstrument(), which runs the dispatched request).
	///
	/// Extracted verbatim from the pad-press handler's `if (!output)` branch (auditionPadAction) so
	/// the song-preview A/B harness's BrowserListingAbProbe can drive the SAME gesture entry point a
	/// pad press uses, rather than restating it. `yPressedEffective` is always written (the clamp
	/// runs before dispatch) and captured into the request as `yPressed` -- the completion must not
	/// read the member back (spec section 6). `outputsOnScreen[yPressedEffective]` is left untouched
	/// by this call; it is written once (if ever) by completeNewInstrument(), which is also where the
	/// REST of the press now lives -- the handler returns straight after this and reads nothing back.
	/// @param yPressed The row the gesture was pressed on, before clamping.
	/// @param ui The UI the pad press came through -- ArrangerView, AutomationView or PerformanceView
	///        all drive these pads. Carried to the completion so its repaint marks the UI that is
	///        actually on screen; null means this view.
	void beginNewInstrumentAcquisition(int32_t yPressed, UI* ui = nullptr);
	uint32_t doActualRender(int32_t xScroll, uint32_t xZoom, uint32_t whichRows, RGB* image,
	                        uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth,
	                        int32_t imageWidth);
	void renderDisplay();

	// edit pad actions
	void cloneClipInstanceToWhite(Output* output, int32_t x, int32_t y, int32_t xScroll);
	void createNewClipInstance(Output* output, int32_t x, int32_t y, int32_t squareStart, int32_t squareEnd,
	                           int32_t xScroll);
	ClipInstance* createClipInstance(Output* output, int32_t y, int32_t squareStart);
	Clip* getClipForNewClipInstance(Output* output, Output* lastOutputInteractedWith, ClipInstance* lastClipInstance);
	Clip* getClipFromSection(Output* output);
	void adjustClipInstanceLength(Output* output, int32_t x, int32_t y, int32_t squareStart, int32_t squareEnd);
	void deleteClipInstance(Output* output, ClipInstance* clipInstance);
	void createNewClipForClipInstance(Output* output, ClipInstance* clipInstance);
	void recordEditPadPress(Output* output, ClipInstance* clipInstance, int32_t x, int32_t y, int32_t xScroll);

	friend class BrowserListingAbProbe;
	friend class PresetAcquireProbe; // the preset-load gate's AQ legs drive the row audition's navigation
};

extern ArrangerView arrangerView;
