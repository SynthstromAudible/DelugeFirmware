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
#include "storage/owner.h"
#include "util/etl_string.h"

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
	// The song-preview A/B gate drives this view's real gestures (grid coordinates, the Grid-layout
	// preset-navigation command) rather than the readers underneath them, which is the only way the
	// dispatch/completion split is actually under test. Same pattern, and the same reason, as
	// Browser's and ArrangerView's friend declarations. The preset-load gate's AQ legs do the same.
	friend class BrowserListingAbProbe;
	friend class PresetAcquireProbe;

public:
	SessionView();
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override;
	bool opened() override;
	void focusRegained() override;

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult clipCreationButtonPressed(hid::Button i, bool on, bool routine);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	void removeClip(Clip* clip);
	void redrawClipsOnScreen(bool doRender = true);
	uint32_t getMaxZoom() override;
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
	bool calculateZoomPinSquares(uint32_t oldScroll, uint32_t newScroll, uint32_t newZoom, uint32_t oldZoom) override;
	uint32_t getMaxLength() override;
	bool setupScroll(uint32_t oldScroll) override;
	uint32_t getClipLocalScroll(Clip* loopable, uint32_t overviewScroll, uint32_t xZoom);
	void flashPlayRoutine();

	void modEncoderButtonAction(uint8_t whichModEncoder, bool on) override;
	void modButtonAction(uint8_t whichButton, bool on) override;
	void selectEncoderAction(int8_t offset) override;
	ActionResult timerCallback() override;
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) override;
	void setLedStates();
	/// @brief The Rows-layout output-type button's follow-up (`:620`), reached via
	///        View::completeChangeOutputTypeFor()'s `SessionRowsChangeType` arm -- on the storage owner
	///        for Synth/Kit, inline for MIDI/CV. Runs the redraw that used to fire unconditionally,
	///        right after the (now possibly-deferred) swap, at the row the gesture was made on.
	/// @param newOutputType The type that was switched to.
	/// @param clip The clip the gesture was made on (Song-owned; safe across the op's yields).
	/// @param doBlink Whether the redrawn output name should blink.
	/// @return Whether the swap actually succeeded.
	bool completeChangeOutputTypeRows(OutputType newOutputType, InstrumentClip* clip, bool doBlink,
	                                  const deluge::storage::AcquireResult* acquired = nullptr);

	/// @brief The Grid-layout output-type button's follow-up -- on the storage owner for Synth/Kit,
	///        inline for MIDI/CV.
	///
	/// Holds everything buttonAction()'s Grid arm used to run straight after
	/// Song::changeOutputType(): the name/mod-controllable update, and the row redraw that used to sit
	/// unconditionally after the whole if/else.
	/// @param newOutputType The type being switched to.
	/// @param oldInstrument The Instrument being replaced. Song-owned, so it survives the op's yields,
	///                      and re-checked for liveness before the follow-up runs.
	void completeChangeOutputType(OutputType newOutputType, Instrument* oldInstrument,
	                              const deluge::storage::AcquireResult* acquired = nullptr);

	/// @brief The Grid-layout preset-navigation gesture's follow-up -- on the storage owner for
	///        Synth/Kit, inline for MIDI/CV.
	/// @param offset    Scroll direction, as commandChangeClipPreset() received it.
	/// @param oldOutput The Output being navigated away from. Song-owned, so it survives the op's
	///                  yields.
	void completeNavigate(int32_t offset, Output* oldOutput, const deluge::storage::AcquireResult* acquired = nullptr);

	/// @brief The empty-pad "create a clip here" gesture's follow-up (Rows layout), on the storage
	///        owner.
	///
	/// Runs createNewInstrumentClip() -- unchanged, and still blocking, which is legal here -- and
	/// then padAction()'s own tail for that branch, including the held-clip state it used to enter
	/// via `goto startHoldingDown`.
	/// @param type      The clip type the gesture decided to create.
	/// @param xDisplay  The pad column the gesture was made on.
	/// @param yDisplay  The pad row the gesture was made on.
	/// @param clipIndex `yDisplay + songViewYScroll` as it read when the pad went down.
	/// @param shiftHeld Whether SHIFT was down when the pad went down; drives the new clip's colour
	///        (`clipWasSelectedWithShift`). Snapshotted at dispatch rather than read live here,
	///        because "here" is now on the far side of a card read.
	/// @param acquired Synth/Kit: what InstrumentLoad acquired. nullptr for MIDI.
	void completeNewTrack(OutputType type, int32_t xDisplay, int32_t yDisplay, int32_t clipIndex, bool shiftHeld,
	                      const deluge::storage::AcquireResult* acquired = nullptr);

	/// @brief The grid new-track gesture's ENTER follow-up, on the storage owner.
	/// @param type            The clip type chosen in the picker.
	/// @param packedXY        The press coordinates, `y << 16 | x`.
	/// @param padStillPressed Whether the starting pad was still down when the gesture dispatched.
	///                        Snapshotted, because the dispatching caller resets gridFirstPressedX/Y
	///                        immediately afterwards.
	/// @param acquired Synth/Kit: what InstrumentLoad acquired. nullptr for Audio, MIDI and CV.
	void completeNewTrackGridEnter(OutputType type, uint32_t packedXY, bool padStillPressed,
	                               const deluge::storage::AcquireResult* acquired = nullptr);

	/// @brief The grid new-track gesture's SELECT follow-up, on the storage owner.
	///
	/// Deliberately separate from completeNewTrackGridEnter(): the two share the create-and-place
	/// half, but their post-create tails (gridNewTrackClipEntered() vs gridNewTrackClipSelected())
	/// are different functions and must not be collapsed into one.
	/// @param type            The clip type chosen in the picker.
	/// @param packedXY        The press coordinates, `y << 16 | x`.
	/// @param padStillPressed Whether the starting pad was still down when the gesture dispatched.
	/// @param acquired Synth/Kit: what InstrumentLoad acquired. nullptr for Audio, MIDI and CV.
	void completeNewTrackGridSelect(OutputType type, uint32_t packedXY, bool padStillPressed,
	                                const deluge::storage::AcquireResult* acquired = nullptr);

	/// @brief gridClonePad()'s follow-up when the copy lands in an EMPTY track column, on the storage
	///        owner.
	/// @param targetSection The section the copy is dropped into.
	/// @param sourceClip    The clip being copied. Song-owned, so it survives the op's yields.
	/// @param acquired What InstrumentLoad acquired for the new track.
	void completeNewTrackClone(uint32_t targetSection, Clip* sourceClip,
	                           const deluge::storage::AcquireResult* acquired);
	void editNumRepeatsTilLaunch(int32_t offset);
	uint32_t getGreyedOutRowsNotRepresentingOutput(Output* output) override;
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	void midiLearnFlash() override;

	void transitionToViewForClip(Clip* clip = nullptr);
	void transitionToSessionView();
	void finishedTransitioningHere();
	void playbackEnded() override;
	void clipNeedsReRendering(Clip* clip) override;
	void sampleNeedsReRendering(Sample* sample) override;
	Clip* getClipOnScreen(int32_t yDisplay);
	Output* getOutputFromPad(int32_t x, int32_t y);
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;
	ActionResult verticalScrollOneSquare(int32_t direction);

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	uint32_t selectedClipTimePressed{};
	uint8_t selectedClipYDisplay{};      // Where the clip is on screen
	uint8_t selectedClipPressYDisplay{}; // Where the user's finger actually is on screen
	uint8_t selectedClipPressXDisplay{};
	bool clipWasSelectedWithShift{}; // Whether shift was held when clip pad started to be held
	bool performActionOnPadRelease{};
	bool performActionOnSectionPadRelease{}; // Keep this separate from the above one because we don't want a mod
	                                         // encoder action to set this to false
	uint8_t sectionPressed{};
	uint8_t masterCompEditMode{};
	int8_t selectedMacro = -1;

	Clip* getClipForLayout();
	int32_t getClipIndexForLayout();

	void copyClipName(Clip* source, Clip* target, Output* targetOutput);

	// Members for grid layout
	inline bool gridFirstPadActive() { return (gridFirstPressedX != -1 && gridFirstPressedY != -1); }
	ActionResult gridHandlePads(int32_t x, int32_t y, int32_t on);
	ActionResult gridHandleScroll(int32_t offsetX, int32_t offsetY);

#ifdef DELUGE_HOST
	/// @brief The scenarios' entry to the new-track gesture: they acquire the preset themselves, on the storage owner
	///        (deluge::harness::createNewInstrumentClip()), and hand the result over here -- which is what the
	///        gesture's own acquisition does from the interaction tier. Host-only: no device image carries it.
	Clip* createNewInstrumentClipAcquired(OutputType type, int32_t yDisplay,
	                                      const deluge::storage::AcquireResult* acquired) {
		return createNewInstrumentClip(type, yDisplay, acquired);
	}
#endif

	// ui
	UIType getUIType() override { return UIType::SESSION; }
	UIModControllableContext getUIModControllableContext() override { return UIModControllableContext::SONG; }

	Clip* createNewClip(OutputType outputType, int32_t yDisplay,
	                    const deluge::storage::AcquireResult* acquired = nullptr);
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
	void displayTempoBPM(deluge::hid::display::oled_canvas::Canvas& canvas, etl::istring& tempoBPM, bool clearArea);
	float lastDisplayedTempo = 0;

	// display root note and scale name
	void displayCurrentRootNoteAndScaleName(deluge::hid::display::oled_canvas::Canvas& canvas,
	                                        etl::istring& rootNoteAndScaleName, bool clearArea);
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
	/// @param acquired Synth/Kit: what InstrumentLoad acquired, or what a scenario's own fixture loader did
	///        (deluge::harness::createNewInstrumentClip()). Required for those two types; MIDI and CV read no card and
	///        are given none.
	Clip* createNewInstrumentClip(OutputType outputType, int32_t yDisplay,
	                              const deluge::storage::AcquireResult* acquired = nullptr);

	bool createNewTrackForAudioClip(AudioClip* newClip);
	bool createNewTrackForInstrumentClip(OutputType type, InstrumentClip* clip, bool copyDrumsFromClip,
	                                     const deluge::storage::AcquireResult* acquired = nullptr);

	bool insertAndResyncNewClip(Clip* newClip, int32_t yDisplay);
	void resyncNewClip(Clip* newClip, ModelStackWithTimelineCounter* modelStackWithTimelineCounter);

	/// @brief Enters the held-clip UI mode for a clip that was just selected or just created
	///
	/// Was padAction()'s `startHoldingDown` label, which the create branch jumped into. That branch's
	/// tail now runs from completeNewTrack(), possibly a tick or more later, so the shared body is a
	/// function rather than a `goto` target -- the statements are unchanged and both paths still run
	/// exactly the same ones.
	/// @param clip     The clip now being held.
	/// @param xDisplay The pad column the press is on.
	/// @param yDisplay The pad row the press is on.
	void beginHoldingClipPress(Clip* clip, int32_t xDisplay, int32_t yDisplay);

	/// @brief Whether the pad that started a dispatched new-track gesture is still held.
	///
	/// Set when padAction() dispatches, cleared by the matching release. The completion enters the
	/// held-clip UI mode only while this is true: with the acquisition deferred, the release can be
	/// consumed BEFORE the clip exists, and a completion that then set UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
	/// would leave the view stuck holding a clip nobody is pressing -- the next unrelated pad press
	/// would clone it instead of selecting it.
	bool newTrackPressHeld_{false};

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
	InstrumentClip* gridCreateInstrumentClipWithNewTrack(OutputType type,
	                                                     const deluge::storage::AcquireResult* acquired = nullptr);
	Clip* gridCreateClip(uint32_t targetSection, Output* targetOutput = nullptr, Clip* sourceClip = nullptr,
	                     const deluge::storage::AcquireResult* acquired = nullptr);
	/// @brief Places a freshly created clip into the song and returns it, or nullptr on failure
	///
	/// Section assignment, insertion into the session list, source-clip track handling, play-pos resync
	/// and selection. Split out of gridCreateClip() because the new-track path cannot reach it until the
	/// user has chosen a clip type, which happens on a later tick.
	Clip* gridPlaceNewClip(Clip* newClip, uint32_t targetSection, Output* targetOutput, Clip* sourceClip,
	                       const deluge::storage::AcquireResult* acquired = nullptr);
	// Worker-fiber jobs for the new-track grid gesture: gridCreateClip() yields for the
	// interactive clip-type picker, which can't run synchronously on this BSP, so the
	// create + the call site's post-logic run on the fiber. Press x/y are packed into
	// the single arg (y<<16 | x). One per call site (their post-logic differs).
	/// @brief Which post-create continuation a deferred new-track gesture asked for
	enum class PendingGridClip : uint8_t {
		NONE,   ///< no gesture in flight
		ENTER,  ///< from gridHandlePads: create, play, and transition into the clip
		SELECT, ///< from the UI_MODE_NONE empty-pad press: create and select
	};

	/// @brief Opens the clip-type picker for a new-track gesture and returns immediately
	///
	/// The clip cannot be created until the user picks a type, so nothing waits here: the pad handler
	/// returns to its loop and finishGridNewTrackClip() completes the gesture once the choice arrives.
	/// @param which    the continuation to run once the clip exists.
	/// @param packedXY the press coordinates, y<<16 | x, carried across the wait.
	void beginGridNewTrackClip(PendingGridClip which, uint32_t packedXY);

	/// @brief Completes a deferred new-track gesture, if one is in flight
	///
	/// Called from exitTrackCreation(), the single funnel out of the picker. Does nothing when no
	/// gesture is pending or when the user backed out (which clears @ref createClip).
	void finishGridNewTrackClip();

	/// @brief Creates and places a grid new-track clip, or nullptr if it could not be created
	///
	/// The half finishGridNewTrackClip() ran identically for both continuations, now on the storage
	/// owner. Only this create-and-place head is shared; the two tails stay separate functions.
	/// @param type     The clip type chosen in the picker.
	/// @param packedXY The press coordinates, `y << 16 | x`.
	/// @return The placed clip, or nullptr if creation or placement failed.
	Clip* createGridNewTrackClip(OutputType type, uint32_t packedXY,
	                             const deluge::storage::AcquireResult* acquired = nullptr);

	/// @brief ENTER continuation: play the new clip, then hold it or transition into it
	/// @param clip            The clip that was just created and placed.
	/// @param padStillPressed Whether the starting pad was still down when the gesture dispatched.
	///                        Passed in rather than re-derived from gridFirstPressedX/Y: those are
	///                        reset by clipPressEnded() before a dispatched completion ever runs.
	void gridNewTrackClipEntered(Clip* clip, bool padStillPressed);

	/// @brief SELECT continuation: select the new clip, and take clip control if still held
	/// @param clip            The clip that was just created and placed.
	/// @param padStillPressed Whether the starting pad was still down when the gesture dispatched.
	void gridNewTrackClipSelected(Clip* clip, bool padStillPressed);

	/// @name Deferred new-track gesture state
	/// Carried across the picker wait, since no stack spans it.
	/// @{
	PendingGridClip pendingGridClip = PendingGridClip::NONE;
	uint32_t pendingGridClipXY = 0;
	/// @}
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
		// explicit fallthrough cases
		case GridDefaultActiveModeSelection: // handled outside
		case GridDefaultActiveModeMaxElement:;
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
