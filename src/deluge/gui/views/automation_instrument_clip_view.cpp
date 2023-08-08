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

#include "gui/views/automation_instrument_clip_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/timeline_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/numeric_driver.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/print.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_instrument_clip_multiply.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_note_row_horizontal_shift.h"
#include "model/consequence/consequence_note_row_length.h"
#include "model/consequence/consequence_note_row_mute.h"
#include "model/drum/drum.h"
#include "model/drum/kit.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/melodic_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/copied_note_row.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multi_range.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <new>
#include <string.h>

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

using namespace deluge::gui;

const uint32_t auditionPadActionUIModes[] = {UI_MODE_AUDITIONING, UI_MODE_HORIZONTAL_SCROLL, UI_MODE_RECORD_COUNT_IN,
                                             UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, 0};

const uint32_t editPadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, 0};

const uint32_t mutePadActionUIModes[] = {UI_MODE_AUDITIONING, 0};

static const uint32_t verticalScrollUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN,
                                                 0};

//this array is sorted in the order that Parameters are scrolled through on the display

const uint32_t paramsForAutomation[41] = {
    ::Param::Global::VOLUME_POST_FX, //Master Volume, Pitch, Pan
    ::Param::Local::PITCH_ADJUST,
    ::Param::Local::PAN,
    ::Param::Local::LPF_FREQ, //LPF Cutoff, Resonance
    ::Param::Local::LPF_RESONANCE,
    ::Param::Local::HPF_FREQ, //HPF Cutoff, Resonance
    ::Param::Local::HPF_RESONANCE,
    ::Param::Global::REVERB_AMOUNT, //Reverb Amount
    ::Param::Global::DELAY_RATE,    //Delay Rate, Feedback
    ::Param::Global::DELAY_FEEDBACK,
    ::Param::Global::VOLUME_POST_REVERB_SEND, //Sidechain Send
    ::Param::Local::OSC_A_VOLUME,             //OSC 1 Volume, Pitch, Phase Width, Carrier Feedback, Wave Index
    ::Param::Local::OSC_A_PITCH_ADJUST,
    ::Param::Local::OSC_A_PHASE_WIDTH,
    ::Param::Local::CARRIER_0_FEEDBACK,
    ::Param::Local::OSC_A_WAVE_INDEX, //OSC 2 Volume, Pitch, Phase Width, Carrier Feedback, Wave Index
    ::Param::Local::OSC_B_VOLUME,
    ::Param::Local::OSC_B_PITCH_ADJUST,
    ::Param::Local::OSC_B_PHASE_WIDTH,
    ::Param::Local::CARRIER_1_FEEDBACK,
    ::Param::Local::OSC_B_WAVE_INDEX,
    ::Param::Local::MODULATOR_0_VOLUME, //FM Mod 1 Volume, Pitch, Feedback
    ::Param::Local::MODULATOR_0_PITCH_ADJUST,
    ::Param::Local::MODULATOR_0_FEEDBACK,
    ::Param::Local::MODULATOR_1_VOLUME, //FM Mod 2 Volume, Pitch, Feedback
    ::Param::Local::MODULATOR_1_PITCH_ADJUST,
    ::Param::Local::MODULATOR_1_FEEDBACK,
    ::Param::Local::ENV_0_ATTACK, //Env 1 ADSR
    ::Param::Local::ENV_0_DECAY,
    ::Param::Local::ENV_0_SUSTAIN,
    ::Param::Local::ENV_0_RELEASE,
    ::Param::Local::ENV_1_ATTACK, //Env 2 ADSR
    ::Param::Local::ENV_1_DECAY,
    ::Param::Local::ENV_1_SUSTAIN,
    ::Param::Local::ENV_1_RELEASE,
    ::Param::Global::LFO_FREQ,      //LFO 1 Freq
    ::Param::Local::LFO_LOCAL_FREQ, //LFO 2 Freq
    ::Param::Global::MOD_FX_DEPTH,  //Mod FX Depth, Rate
    ::Param::Global::MOD_FX_RATE,
    ::Param::Global::ARP_RATE,   //Arp Rate
    ::Param::Local::NOISE_VOLUME //Noise
};

//grid sized array to assign patched parameter's (a.k.a automatable parameters) to the grid

const uint32_t paramShortcutsForAutomation[kDisplayWidth][kDisplayHeight] = {
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {::Param::Local::OSC_A_VOLUME, ::Param::Local::OSC_A_PITCH_ADJUST, 0xFFFFFFFF, ::Param::Local::OSC_A_PHASE_WIDTH,
     0xFFFFFFFF, ::Param::Local::CARRIER_0_FEEDBACK, ::Param::Local::OSC_A_WAVE_INDEX, ::Param::Local::NOISE_VOLUME},
    {::Param::Local::OSC_B_VOLUME, ::Param::Local::OSC_B_PITCH_ADJUST, 0xFFFFFFFF, ::Param::Local::OSC_B_PHASE_WIDTH,
     0xFFFFFFFF, ::Param::Local::CARRIER_1_FEEDBACK, ::Param::Local::OSC_B_WAVE_INDEX, 0xFFFFFFFF},
    {::Param::Local::MODULATOR_0_VOLUME, ::Param::Local::MODULATOR_0_PITCH_ADJUST, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     ::Param::Local::MODULATOR_0_FEEDBACK, 0xFFFFFFFF, 0xFFFFFFFF},
    {::Param::Local::MODULATOR_1_VOLUME, ::Param::Local::MODULATOR_1_PITCH_ADJUST, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     ::Param::Local::MODULATOR_1_FEEDBACK, 0xFFFFFFFF, 0xFFFFFFFF},
    {::Param::Global::VOLUME_POST_FX, 0xFFFFFFFF, ::Param::Local::PITCH_ADJUST, ::Param::Local::PAN, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {::Param::Local::ENV_0_RELEASE, ::Param::Local::ENV_0_SUSTAIN, ::Param::Local::ENV_0_DECAY,
     ::Param::Local::ENV_0_ATTACK, 0xFFFFFFFF, 0xFFFFFFFF, ::Param::Local::LPF_RESONANCE, ::Param::Local::LPF_FREQ},
    {::Param::Local::ENV_1_RELEASE, ::Param::Local::ENV_1_SUSTAIN, ::Param::Local::ENV_1_DECAY,
     ::Param::Local::ENV_1_ATTACK, 0xFFFFFFFF, 0xFFFFFFFF, ::Param::Local::HPF_RESONANCE, ::Param::Local::HPF_FREQ},
    {0xFFFFFFFF, 0xFFFFFFFF, ::Param::Global::VOLUME_POST_REVERB_SEND, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF},
    {::Param::Global::ARP_RATE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {::Param::Global::LFO_FREQ, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     ::Param::Global::MOD_FX_DEPTH, ::Param::Global::MOD_FX_RATE},
    {::Param::Local::LFO_LOCAL_FREQ, 0xFFFFFFFF, 0xFFFFFFFF, ::Param::Global::REVERB_AMOUNT, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF},
    {::Param::Global::DELAY_RATE, 0xFFFFFFFF, 0xFFFFFFFF, ::Param::Global::DELAY_FEEDBACK, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}};

//grid sized array to assign midi cc values to each pad on the grid

const uint32_t midiCCShortcutsForAutomation[kDisplayWidth][kDisplayHeight] = {

    {0, 16, 32, 48, 64, 80, 96, 112},          {1, 17, 33, 49, 65, 81, 97, 113},

    {2, 18, 34, 50, 66, 82, 98, 114},          {3, 19, 35, 51, 67, 83, 99, 115},

    {4, 20, 36, 52, 68, 84, 100, 116},         {5, 21, 37, 53, 69, 85, 101, 117},

    {6, 22, 38, 54, 70, 86, 102, 118},         {7, 23, 39, 55, 71, 87, 103, 119},

    {8, 24, 40, 56, 72, 88, 104, 0xFFFFFFFF},  {9, 25, 41, 57, 73, 89, 105, 0xFFFFFFFF},

    {10, 26, 42, 58, 74, 90, 106, 0xFFFFFFFF}, {11, 27, 43, 59, 75, 91, 107, 0xFFFFFFFF},

    {12, 28, 44, 60, 76, 92, 108, 0xFFFFFFFF}, {13, 29, 45, 61, 77, 93, 109, 0xFFFFFFFF},

    {14, 30, 46, 62, 78, 94, 110, 120},        {15, 31, 47, 63, 79, 95, 111, 121}};

//let's render some love <3

const uint32_t love[kDisplayWidth][kDisplayHeight] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0},
    {0, 0, 0, 0xFFFFFFFF, 0, 0, 0xFFFFFFFF, 0},
    {0, 0, 0xFFFFFFFF, 0, 0, 0, 0, 0xFFFFFFFF},
    {0, 0xFFFFFFFF, 0, 0, 0, 0, 0xFFFFFFFF, 0},
    {0xFFFFFFFF, 0, 0, 0, 0, 0xFFFFFFFF, 0, 0},
    {0, 0xFFFFFFFF, 0, 0, 0, 0, 0xFFFFFFFF, 0},
    {0, 0, 0xFFFFFFFF, 0, 0, 0, 0, 0xFFFFFFFF},
    {0, 0, 0, 0xFFFFFFFF, 0, 0, 0xFFFFFFFF, 0},
    {0, 0, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0},
    {0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 0, 0, 0, 0},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 0},
    {0, 0, 0, 0, 0, 0xFFFFFFFF, 0, 0},
    {0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 0, 0xFFFFFFFF, 0},
    {0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0, 0, 0, 0, 0, 0, 0, 0}};

//VU meter style colours for the automation editor

const uint8_t rowColour[kDisplayHeight][3] = {

    {0, 255, 0}, {36, 219, 0}, {73, 182, 0}, {109, 146, 0}, {146, 109, 0}, {182, 73, 0}, {219, 36, 0}, {255, 0, 0}};

const uint8_t rowTailColour[kDisplayHeight][3] = {

    {2, 53, 2}, {9, 46, 2}, {17, 38, 2}, {24, 31, 2}, {31, 24, 2}, {38, 17, 2}, {46, 9, 2}, {53, 2, 2}};

const uint8_t rowBlurColour[kDisplayHeight][3] = {

    {71, 111, 71}, {72, 101, 66}, {73, 90, 62}, {74, 80, 57}, {76, 70, 53}, {77, 60, 48}, {78, 49, 44}, {79, 39, 39}};

AutomationInstrumentClipView automationInstrumentClipView{};

AutomationInstrumentClipView::AutomationInstrumentClipView() {

	instrumentClipView.numEditPadPresses = 0;

	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		instrumentClipView.editPadPresses[i].isActive = false;
	}

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		instrumentClipView.numEditPadPressesPerNoteRowOnScreen[yDisplay] = 0;
		instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = 255;
		instrumentClipView.auditionPadIsPressed[yDisplay] = 0;
	}

	instrumentClipView.auditioningSilently = false;
	instrumentClipView.timeLastEditPadPress = 0;

	//initialize automation view specific variables
	interpolation = true;
	interpolationBefore = false;
	interpolationAfter = false;
	encoderAction = false; //used to prevent excessive blinking when you're scrolling with horizontal / vertical / mod encoders
	shortcutBlinking = false; //used to reset shortcut blinking
}

inline InstrumentClip* getCurrentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

//called everytime you open up the automation view
bool AutomationInstrumentClipView::opened() {

	//grab the default setting for interpolation
	interpolation = runtimeFeatureSettings.get(RuntimeFeatureSettingType::AutomationInterpolate);

	InstrumentClip* clip = getCurrentClip();

	//check if we for some reason, left the automation view, then switch clip types, then came back in
	//if you did that...reset the parameter selection and save the current parameter type selection
	//so we can check this again next time it happens
	if (clip->output->type != clip->lastSelectedInstrumentType) {
		initParameterSelection();

		clip->lastSelectedInstrumentType = clip->output->type;
	}

	resetShortcutBlinking();

	openedInBackground();

	InstrumentClipMinder::opened();

	focusRegained();

	return true;
}

// Initializes some stuff to begin a new editing session
void AutomationInstrumentClipView::focusRegained() {

	ClipView::focusRegained();

	instrumentClipView.auditioningSilently = false; // Necessary?

	InstrumentClipMinder::focusRegained();

	instrumentClipView.setLedStates();
}

void AutomationInstrumentClipView::openedInBackground() {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	clip->onAutomationInstrumentClipView = true; //used when you're in song view / arranger view / keyboard view
	clip->onKeyboardScreen = false;							//(so it knows to come back to automation view)

	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);

	instrumentClipView.recalculateColours();

	AudioEngine::routineWithClusterLoading(); // -----------------------------------
	AudioEngine::logAction("AutomationInstrumentClipView::beginSession 2");

	if (renderingToStore) {
		renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight],
		               true);
		renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight]);
	}
	else {
		uiNeedsRendering(this);
	}
}

//no clue what this function does, but it's needed and called allll the time
//same code as in instrument clip view
void AutomationInstrumentClipView::graphicsRoutine() {
	if (!currentSong) {
		return; // Briefly, if loading a song fails, during the creation of a new blank one, this could happen.
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) {
		return;
	}

	if (PadLEDs::flashCursor == FLASH_CURSOR_OFF) {
		return;
	}

	int32_t newTickSquare;

	bool reallyNoTickSquare = (!playbackHandler.isEitherClockActive() || !currentSong->isClipActive(clip)
	                           || currentUIMode == UI_MODE_EXPLODE_ANIMATION || playbackHandler.ticksLeftInCountIn);

	if (reallyNoTickSquare) {
		newTickSquare = 255;
	}
	else {
		newTickSquare = getTickSquare();
		if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
			newTickSquare = 255;
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	memset(tickSquares, newTickSquare, kDisplayHeight);

	uint8_t colours[kDisplayHeight];
	uint8_t nonMutedColour = clip->getCurrentlyRecordingLinearly() ? 2 : 0;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		int32_t noteRowIndex;
		NoteRow* noteRow = clip->getNoteRowOnScreen(yDisplay, currentSong, &noteRowIndex);
		colours[yDisplay] = (noteRow && noteRow->muted) ? 1 : nonMutedColour;

		if (!reallyNoTickSquare) {
			if (noteRow && noteRow->hasIndependentPlayPos()) {

				int32_t noteRowId = clip->getNoteRowId(noteRow, noteRowIndex);
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);

				int32_t rowTickSquare = getSquareFromPos(noteRow->getLivePos(modelStackWithNoteRow));
				if (rowTickSquare < 0 || rowTickSquare >= kDisplayWidth) {
					rowTickSquare = 255;
				}
				tickSquares[yDisplay] = rowTickSquare;
			}
		}
	}

	PadLEDs::setTickSquares(tickSquares, colours);
}

//rendering

//called whenever you call uiNeedsRendering(this) somewhere else
//used to render automation overview, automation editor
//used to setup the shortcut blinking
bool AutomationInstrumentClipView::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                  bool drawUndefinedArea) {

	if (!image) {
		return true;
	}

	if (!occupancyMask) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) {
		return true;
	}

	PadLEDs::renderingLock = true;

	instrumentClipView.recalculateColours();

	memset(image, 0,
	       sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth)
	           * 3); // erase current image as it will be refreshed

	memset(occupancyMask, 0,
	       sizeof(uint8_t) * kDisplayHeight
	           * (kDisplayWidth + kSideBarWidth)); // erase current occupancy mask as it will be refreshed

	performActualRender(whichRows, &image[0][0][0], occupancyMask, currentSong->xScroll[NAVIGATION_CLIP],
	                    currentSong->xZoom[NAVIGATION_CLIP], kDisplayWidth, kDisplayWidth + kSideBarWidth,
	                    drawUndefinedArea);

	InstrumentClip* clip = getCurrentClip();

	if (encoderAction == false) {
		if (clip->lastSelectedParamShortcutX
		    != 255) { //if a Param has been selected for editing, blink its shortcut pad
			if (shortcutBlinking == false) {
				memset(soundEditor.sourceShortcutBlinkFrequencies, 255,
				       sizeof(soundEditor.sourceShortcutBlinkFrequencies));
				soundEditor.setupShortcutBlink(clip->lastSelectedParamShortcutX, clip->lastSelectedParamShortcutY, 10);
				soundEditor.blinkShortcut();

				shortcutBlinking = true;
			}
		}
		else { //unset previously set blink timers if not editing a parameter
			resetShortcutBlinking();
		}
	}
	else {
		encoderAction = false; //doing this so the shortcut doesn't blink like crazy while turning knobs that refresh UI
	}

	PadLEDs::renderingLock = false;

	return true;
}

//determines whether you should render the automation editor, automation overview or just render some love <3
void AutomationInstrumentClipView::performActualRender(uint32_t whichRows, uint8_t* image,
                                                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                       int32_t xScroll, uint32_t xZoom, int32_t renderWidth,
                                                       int32_t imageWidth, bool drawUndefinedArea) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		uint8_t* occupancyMaskOfRow = occupancyMask[yDisplay];

		if (instrument->type != InstrumentType::CV
		    && !(instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()
		         && !((Kit*)instrument)->selectedDrum)
		    && !(instrument->type == InstrumentType::KIT && instrumentClipView.getAffectEntire())) {

			if (clip->lastSelectedParamID != 255) { //if parameter has been selected, show Automation Editor

				renderAutomationEditor(modelStack, clip, instrument, image + (yDisplay * imageWidth * 3),
				                       occupancyMaskOfRow, renderWidth, xScroll, xZoom, yDisplay, drawUndefinedArea);
			}

			else { //if not editing a parameter, show Automation Overview

				renderAutomationOverview(modelStack, clip, instrument, image + (yDisplay * imageWidth * 3),
				                         occupancyMaskOfRow, yDisplay);
			}
		}

		else {
			if (instrument->type == InstrumentType::CV) {
				renderLove(image + (yDisplay * imageWidth * 3), occupancyMaskOfRow, yDisplay);
			}
		}
	}
}

//renders automation overview
void AutomationInstrumentClipView::renderAutomationOverview(ModelStackWithTimelineCounter* modelStack,
                                                            InstrumentClip* clip, Instrument* instrument,
                                                            uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

		uint8_t* pixel = image + (xDisplay * 3);

		ModelStackWithAutoParam* modelStackWithParam = 0;

		if ((instrument->type == InstrumentType::SYNTH || instrument->type == InstrumentType::KIT)
		    && (paramShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF)) {
			modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, paramShortcutsForAutomation[xDisplay][yDisplay]);
		}

		else if (instrument->type == InstrumentType::MIDI_OUT
		         && midiCCShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {
			modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, midiCCShortcutsForAutomation[xDisplay][yDisplay]);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {
			if (modelStackWithParam->autoParam
			        ->isAutomated()) { //highlight pad white if the parameter it represents is currently automated
				pixel[0] = 130;
				pixel[1] = 120;
				pixel[2] = 130;
			}

			else {

				if (instrument->type == InstrumentType::MIDI_OUT
				    && midiCCShortcutsForAutomation[xDisplay][yDisplay] <= 119) {

					//formula I came up with to rended pad colours from green to red across 119 Midi CC pads
					pixel[0] = 2 + (midiCCShortcutsForAutomation[xDisplay][yDisplay] * ((51 << 20) / 119)) >> 20; //red
					pixel[1] =
					    53 - ((midiCCShortcutsForAutomation[xDisplay][yDisplay] * ((51 << 20) / 119)) >> 20); //green
					pixel[2] = 2;                                                                             //blue
				}

				else { //if we're not in a midi clip, highlight the automatable pads dimly white (...more like grey)

					pixel[0] = 10;
					pixel[1] = 10;
					pixel[2] = 10;
				}
			}

			occupancyMask[xDisplay] = 64;
		}
	}
}

//gets the length of the clip, renders the pads corresponding to current parameter values set up to the clip length
//renders the undefined area of the clip that the user can't interact with
void AutomationInstrumentClipView::renderAutomationEditor(ModelStackWithTimelineCounter* modelStack,
                                                          InstrumentClip* clip, Instrument* instrument, uint8_t* image,
                                                          uint8_t occupancyMask[], int32_t renderWidth, int32_t xScroll,
                                                          uint32_t xZoom, int32_t yDisplay, bool drawUndefinedArea) {

	ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		int32_t effectiveLength;

		if (instrument->type == InstrumentType::KIT) {
			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

			effectiveLength = modelStackWithNoteRow->getLoopLength();
		}
		else {
			effectiveLength = clip->loopLength; //this will differ for a kit when in note row mode
		}

		renderRow(modelStackWithParam, image, occupancyMask, true, effectiveLength, true, xScroll, xZoom, 0,
		          renderWidth, false, yDisplay);

		if (drawUndefinedArea == true) {

			clip->drawUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth, this,
			                        currentSong->tripletsOn); // Sends image pointer for just the one row
		}
	}
}

//this function started off as a copy of the renderRow function from the NoteRow class
//replaced "notes" with "nodes"
//it worked for the most part, but there's bugs
//so I'm leaving it here in case I can get it working
//instead of using the rest of the function, I inserted my other rendering code at the top which always works
void AutomationInstrumentClipView::renderRow(ModelStackWithAutoParam* modelStack, uint8_t* image,
                                             uint8_t occupancyMask[], bool overwriteExisting,
                                             uint32_t effectiveRowLength, bool allowNoteTails, int32_t xScroll,
                                             uint32_t xZoom, int32_t xStartNow, int32_t xEnd, bool drawRepeats,
                                             int32_t yDisplay) {

	//if (!modelStack->autoParam->nodes.getNumElements()) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

		uint32_t squareStart = getPosFromSquare(xDisplay);
		int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareStart, modelStack);
		int32_t knobPos = modelStack->paramCollection->paramValueToKnobPos(currentValue, modelStack);
		knobPos = knobPos + 64;

		uint8_t* pixel = image + (xDisplay * 3);

		if (knobPos != 0 && knobPos >= yDisplay * 16) {
			memcpy(pixel, &rowColour[yDisplay], 3);
			occupancyMask[xDisplay] = 64;
		}
	}

	return;
	//}


	//disabling the rest of this code for now as it's not working properly

	/*
	int32_t squareEndPos[kMaxImageStoreWidth];
	int32_t searchTerms[kMaxImageStoreWidth];

	int32_t whichRepeat = 0;

	int32_t xEndNow;

	do {
		// Start by presuming we'll do all the squares now... though we may decide in a second to do a smaller number now, and come back for more batches
		xEndNow = xEnd;

		// For each square we might do now...
		for (int32_t square = xStartNow; square < xEndNow; square++) {

			// Work out its endPos...
			int32_t thisSquareEndPos = getPosFromSquare(square + 1, xScroll, xZoom) - effectiveRowLength * whichRepeat;

			// If we're drawing repeats and the square ends beyond end of Clip...
			if (drawRepeats && thisSquareEndPos > effectiveRowLength) {

				// If this is the first square we're doing now, we can take a shortcut to skip forward some repeats
				if (square == xStartNow) {
					int32_t numExtraRepeats = (uint32_t)(thisSquareEndPos - 1) / effectiveRowLength;
					whichRepeat += numExtraRepeats;
					thisSquareEndPos -= numExtraRepeats * effectiveRowLength;
				}

				// Otherwise, we'll come back in a moment to do the rest of the repeats - just record that we want to stop rendering before this square until then
				else {
					xEndNow = square;
					break;
				}
			}

			squareEndPos[square - xStartNow] = thisSquareEndPos;
		}

		memcpy(searchTerms, squareEndPos, sizeof(squareEndPos));

		modelStack->autoParam->nodes.searchMultiple(searchTerms, xEndNow - xStartNow);

		int32_t thisSquareStartPos = getPosFromSquare(xStartNow, xScroll, xZoom) - effectiveRowLength * whichRepeat;

		//int32_t thisSquareEndPos = getPosFromSquare(xStartNow + 1, xScroll, xZoom) - effectiveRowLength * whichRepeat;

		for (int32_t xDisplay = xStartNow; xDisplay < xEndNow; xDisplay++) {
			if (xDisplay != xStartNow) {
				thisSquareStartPos = squareEndPos[xDisplay - xStartNow - 1];
				//thisSquareEndPos = squareEndPos[xDisplay - xStartNow];
			}
			int32_t i = searchTerms[xDisplay - xStartNow];

			ParamNode* node = modelStack->autoParam->nodes.getElement(i - 1); // Subtracting 1 to do "LESS"

			uint8_t* pixel = image + xDisplay * 3;

			int32_t currentValue = modelStack->autoParam->getValueAtPos(thisSquareStartPos, modelStack);
			int32_t knobPos = modelStack->paramCollection->paramValueToKnobPos(currentValue, modelStack);

			knobPos = knobPos + 64;

			if (knobPos != 0 && knobPos >= yDisplay * 16) {

				// If Node starts somewhere within square, draw the blur colour
				if (node && node->pos > thisSquareStartPos) {

					memcpy(pixel, &rowBlurColour[yDisplay], 3);
					occupancyMask[xDisplay] = 64;
				}

				// Or if Node starts exactly on square...
				else if (node && node->pos == thisSquareStartPos) {
					memcpy(pixel, &rowColour[yDisplay], 3);
					occupancyMask[xDisplay] = 64;
				}

				// Draw wrapped notes
				//	else if (node && node->pos < thisSquareEndPos) {
				//		memcpy(pixel, &rowTailColour[yDisplay], 3);
				//		occupancyMask[xDisplay] = 64;
				//	}

				//Draw wrapped notes
				else if (!drawRepeats || whichRepeat) {
					bool wrapping = (i == 0); // Subtracting 1 to do "LESS"
					                          //	if (wrapping) {
					//		int32_t lastNodeIndex = modelStack->autoParam->nodes.getNumElements() - 1;
					//		node = modelStack->autoParam->nodes.getElement(lastNodeIndex);
					//	}
					int32_t nodeEnd = effectiveRowLength - 1;
					if (wrapping) {
						nodeEnd -= effectiveRowLength;
					}
					if (nodeEnd > thisSquareStartPos && allowNoteTails) {
						memcpy(pixel, &rowTailColour[yDisplay], 3);
						occupancyMask[xDisplay] = 64;
					}
				}
			}
		}

		xStartNow = xEndNow;
		whichRepeat++;
		// TODO: if we're gonna repeat, we probably don't need to do the multi search again...
	} while (
	    xStartNow
	    != xEnd); // This will only do another repeat if we'd modified xEndNow, which can only happen if drawRepeats
	*/
}

//easter egg lol. it is rendered when you press the CV clip button as you can't use automation view there
//it draws a cute heart and musical note
void AutomationInstrumentClipView::renderLove(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

		uint8_t* pixel = image + (xDisplay * 3);

		if (love[xDisplay][yDisplay] == 0xFFFFFFFF) {

			memcpy(pixel, &rowColour[yDisplay], 3);
			occupancyMask[xDisplay] = 64;
		}
	}
}

//no change compared to the instrument clip view version
bool AutomationInstrumentClipView::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {

	if (!image) {
		return true;
	}

	for (int32_t i = 0; i < kDisplayHeight; i++) {
		if (whichRows & (1 << i)) {
			instrumentClipView.drawMuteSquare(getCurrentClip()->getNoteRowOnScreen(i, currentSong), image[i],
			                                  occupancyMask[i]);
			instrumentClipView.drawAuditionSquare(i, image[i]);
		}
	}

	return true;
}

//button action

ActionResult AutomationInstrumentClipView::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	// Scale mode button
	if (b == SCALE_MODE) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		// Kits can't do scales!
		if (instrument->type == InstrumentType::KIT) {
			if (on) {
				indicator_leds::indicateAlertOnLed(IndicatorLED::KIT);
			}
			return ActionResult::DEALT_WITH;
		}

		actionLogger.deleteAllLogs(); // Can't undo past this!

		if (on) {

			if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {

				// If user holding shift and we're already in scale mode, cycle through available scales
				if (Buttons::isShiftButtonPressed() && clip->inScaleMode) {
					cycleThroughScales();
					instrumentClipView.recalculateColours();
					uiNeedsRendering(this);
				}

				// Or, no shift button - normal behaviour
				else {
					currentUIMode = UI_MODE_SCALE_MODE_BUTTON_PRESSED;
					instrumentClipView.exitScaleModeOnButtonRelease = true;
					if (!clip->inScaleMode) {
						calculateDefaultRootNote(); // Calculate it now so we can show the user even before they've released the button
						flashDefaultRootNoteOn = false;
						instrumentClipView.flashDefaultRootNote();
					}
				}
			}

			// If user is auditioning just one NoteRow, we can go directly into Scale Mode and set that root note
			else if (instrumentClipView.oneNoteAuditioning() && !clip->inScaleMode) {
				instrumentClipView.cancelAllAuditioning();
				instrumentClipView.enterScaleMode(instrumentClipView.lastAuditionedYDisplay);
			}
		}
		else {
			if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
				currentUIMode = UI_MODE_NONE;
				if (clip->inScaleMode) {
					if (instrumentClipView.exitScaleModeOnButtonRelease) {
						instrumentClipView.exitScaleMode();
					}
				}
				else {
					instrumentClipView.enterScaleMode();
				}
			}
		}
	}

	// Song view button
	else if (b == SESSION_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentSong->lastClipInstanceEnteredStartPos != -1 || clip->isArrangementOnlyClip()) {
				bool success = arrangerView.transitionToArrangementEditor();
				if (!success) {
					goto doOther;
				}
			}
			else {
doOther:
				// Transition to session view
				//code taken from the keyboard screen instead of calling the transition to session view function
				currentUIMode = UI_MODE_INSTRUMENT_CLIP_COLLAPSING;
				int32_t transitioningToRow = sessionView.getClipPlaceOnScreen(clip);
				memcpy(&PadLEDs::imageStore, PadLEDs::image, sizeof(PadLEDs::image));
				memcpy(&PadLEDs::occupancyMaskStore, PadLEDs::occupancyMask, sizeof(PadLEDs::occupancyMask));
				PadLEDs::numAnimatedRows = kDisplayHeight;
				for (int32_t y = 0; y < kDisplayHeight; y++) {
					PadLEDs::animatedRowGoingTo[y] = transitioningToRow;
					PadLEDs::animatedRowGoingFrom[y] = y;
				}

				PadLEDs::setupInstrumentClipCollapseAnimation(true);
				PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
				PadLEDs::renderClipExpandOrCollapse();
			}
			resetShortcutBlinking();
		}
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			changeRootUI(&keyboardScreen);
			resetShortcutBlinking(); //reset blinking if you're leaving automation view for keyboard view
									//blinking will be reset when you come back
		}
	}

	// Clip button - exit mode
	//if you're holding shift or holding an audition pad while pressed clip, don't exit out of automation view
	//reset parameter selection and short blinking instead
	else if (b == CLIP_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (Buttons::isShiftButtonPressed()) {
				initParameterSelection();
			}
			else {
				changeRootUI(&instrumentClipView);
			}
			resetShortcutBlinking();
		}
		else if (on && currentUIMode == UI_MODE_AUDITIONING) {
			initParameterSelection();
			resetShortcutBlinking();
		}
	}

	// Wrap edit button
	//no clue if this button does anything in the automation view...
	else if (b == CROSS_SCREEN_EDIT) {
		if (on) {
			if (currentUIMode == UI_MODE_NONE) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				if (clip->wrapEditing) {
					clip->wrapEditing = false;
				}
				else {
					clip->wrapEditLevel = currentSong->xZoom[NAVIGATION_CLIP] * kDisplayWidth;
					// Ensure that there are actually multiple screens to edit across
					if (clip->wrapEditLevel < clip->loopLength) {
						clip->wrapEditing = true;
					}
				}

				setLedStates();
			}
		}
	}

	//when switching clip type, reset parameter selection and shortcut blinking
	else if (b == KIT && currentUIMode == UI_MODE_NONE) {
		if (on) {
			if (instrument->type != InstrumentType::KIT) { //don't reset anything if you're already in a KIT clip
				initParameterSelection();
				resetShortcutBlinking();
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (Buttons::isNewOrShiftButtonPressed()) {
				instrumentClipView.createNewInstrument(InstrumentType::KIT);
			}
			else {
				instrumentClipView.changeInstrumentType(InstrumentType::KIT);
			}
		}
	}

	//when switching clip type, reset parameter selection and shortcut blinking
	else if (b == SYNTH && currentUIMode != UI_MODE_HOLDING_SAVE_BUTTON
	         && currentUIMode != UI_MODE_HOLDING_LOAD_BUTTON) {
		if (on) {
			if (instrument->type != InstrumentType::SYNTH) { //don't reset anything if you're already in a SYNTH clip
				initParameterSelection();
				resetShortcutBlinking();
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode
			    == UI_MODE_NONE) { //this gets triggered when you change an existing clip to synth / create a new synth clip in song mode
				if (Buttons::isNewOrShiftButtonPressed()) {
					instrumentClipView.createNewInstrument(InstrumentType::SYNTH);
				}
				else { //this gets triggered when you change clip type to synth from within inside clip view
					instrumentClipView.changeInstrumentType(InstrumentType::SYNTH);
				}
			}
		}
	}

	//when switching clip type, reset parameter selection and shortcut blinking
	else if (b == MIDI) {
		if (on) {
			if (instrument->type != InstrumentType::MIDI_OUT) { //don't reset anything if you're already in a MIDI clip
				initParameterSelection();
				resetShortcutBlinking();
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) {
				instrumentClipView.changeInstrumentType(InstrumentType::MIDI_OUT);
			}
		}
	}

	//when switching clip type, reset parameter selection and shortcut blinking
	else if (b == CV) {
		initParameterSelection();
		resetShortcutBlinking();

		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) {
				instrumentClipView.changeInstrumentType(InstrumentType::CV);
			}
		}
	}

	// Horizontal encoder button
	else if (b == X_ENC) {

		// If user wants to "multiple" Clip contents
		if (on && Buttons::isShiftButtonPressed() && !isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)
		    && !isOnParameterGridMenuView()) {
			if (isNoUIModeActive()) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				// Zoom to max if we weren't already there...
				if (!zoomToMax()) {
					// Or if we didn't need to do that, double Clip length
					instrumentClipView.doubleClipLengthAction();
				}
				else {
					displayZoomLevel();
				}
			}
			enterUIMode(
			    UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON); // Whether or not we did the "multiply" action above, we need to be in this UI mode, e.g. for rotating individual NoteRow
		}

		// Otherwise...
		else {
			if (isUIModeActive(UI_MODE_AUDITIONING)) {
				if (!on) {
					instrumentClipView.timeHorizontalKnobLastReleased = AudioEngine::audioSampleTimer;
					numericDriver.cancelPopup();
				}
			}
			goto passToOthers; // For exiting the UI mode, I think
		}
	}

	//if holding horizontal encoder button down and pressing back clear automation
	//if you're on automation overview, clear all automation
	//if you're in the automation editor, clear the automation for the parameter in focus

	else if (b == BACK && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		if (on
		    && clip->lastSelectedParamID == 255) { //only allow clearing of a clip if you're in the automation overview
			goto passToOthers;
		}
		else if (on && clip->lastSelectedParamID != 255) {
			//delete automation of current parameter selected

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_DELETE, false);
				modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

				numericDriver.displayPopup(HAVE_OLED ? "Automation deleted" : "DELETED");
			}
		}
	}

	//Select encoder
	//if you're not pressing shift and press down on the select encoder, toggle interpolation on/off

	else if (!Buttons::isShiftButtonPressed() && b == SELECT_ENC) {
		if (on) {
			if (interpolation == RuntimeFeatureStateToggle::Off) {

				interpolation = RuntimeFeatureStateToggle::On;

				numericDriver.displayPopup(HAVE_OLED ? "Interpolation On" : "ON");
			}
			else {
				interpolation = RuntimeFeatureStateToggle::Off;

				numericDriver.displayPopup(HAVE_OLED ? "Interpolation Off" : "OFF");
			}
		}
	}

	else if (b == AFFECT_ENTIRE) { //when you press affect entire in a kit, the parameter selection needs to reset
		initParameterSelection(); //right now this is actioned in a Synth and Midi clip also...should I change it? hrm...
		goto passToOthers;
	}

	else {
passToOthers:

		numericDriver.cancelPopup();
		uiNeedsRendering(this);

		ActionResult result = InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		if (result != ActionResult::NOT_DEALT_WITH) {
			return result;
		}

		return ClipView::buttonAction(b, on, inCardRoutine);
	}

	//disabled re-rendering when scale mode button is pressed as it causes the grid to flicker weirdly
	if (b != SCALE_MODE) {
		uiNeedsRendering(this);
	}

	return ActionResult::DEALT_WITH;
}

//pad action
//handles shortcut pad action for automation (e.g. when you press shift + pad on the grid)
//everything else is pretty much the same as instrument clip view

ActionResult AutomationInstrumentClipView::padAction(int32_t x, int32_t y, int32_t velocity) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// Edit pad action...
	if (x < kDisplayWidth) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		//if the user wants to change the parameter they are editing using Shift + Pad shortcut
		if (velocity) {

			if (Buttons::isShiftButtonPressed()) {

				handleSinglePadPress(modelStack, clip, x, y, true);

				return ActionResult::DEALT_WITH;
			}
			else { //regular automation step editing action
				goto doRegularEditPadActionProbably;
			}
		}

		// Regular edit-pad action
		else {
doRegularEditPadActionProbably:
			if (isUIModeWithinRange(editPadActionUIModes)) {
				editPadAction(velocity, y, x, currentSong->xZoom[NAVIGATION_CLIP]);
			}
		}
	}

	// If mute pad action
	else if (x == kDisplayWidth) {
		if (currentUIMode == UI_MODE_MIDI_LEARN) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (instrument->type != InstrumentType::KIT) {
				return ActionResult::DEALT_WITH;
			}
			NoteRow* noteRow = clip->getNoteRowOnScreen(y, currentSong);
			if (!noteRow || !noteRow->drum) {
				return ActionResult::DEALT_WITH;
			}
			view.noteRowMuteMidiLearnPadPressed(velocity, noteRow);
		}
		else if (isUIModeWithinRange(mutePadActionUIModes) && velocity) {
			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(y, modelStack);

			bool renderingNeeded = false;

			//if we're in a kit, and you press a mute pad
			//check if it's a mute pad corresponding to the current selected drum
			//if not, change the drum selection, refresh parameter selection and go back to automation overview
			if (instrument->type == InstrumentType::KIT) {
				if (modelStackWithNoteRow->getNoteRowAllowNull()) {
					Drum* drum = modelStackWithNoteRow->getNoteRow()->drum;
					if (((Kit*)instrument)->selectedDrum != drum) {
						initParameterSelection();
						renderingNeeded = true;
					}
				}
			}
			else {
				if (modelStackWithNoteRow->getNoteRowAllowNull()) {
					renderingNeeded = true;
				}
			}

			instrumentClipView.mutePadPress(y);

			if (renderingNeeded) { //re-rendering mute pads
				uiNeedsRendering(this);
			}
		}
	}

	// Audition pad action
	else {
possiblyAuditionPad:
		if (x == kDisplayWidth + 1) {

			// "Learning" to this audition pad:
			if (isUIModeActiveExclusively(UI_MODE_MIDI_LEARN)) {
				if (getCurrentUI() == this) {
					if (sdRoutineLock) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					if (instrument->type == InstrumentType::KIT) {
						NoteRow* thisNoteRow = clip->getNoteRowOnScreen(y, currentSong);
						if (!thisNoteRow || !thisNoteRow->drum) {
							return ActionResult::DEALT_WITH;
						}
						view.drumMidiLearnPadPressed(velocity, thisNoteRow->drum, (Kit*)instrument);
					}
					else {
						view.melodicInstrumentMidiLearnPadPressed(velocity, (MelodicInstrument*)instrument);
					}
				}
			}

			// Changing the scale:
			else if (isUIModeActiveExclusively(UI_MODE_SCALE_MODE_BUTTON_PRESSED)) {
				if (sdRoutineLock) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				if (velocity
				    && instrument->type
				           != InstrumentType::
				               KIT) { // We probably couldn't have got this far if it was a Kit, but let's just check
					if (clip->inScaleMode) {
						currentUIMode = UI_MODE_NONE; // So that the upcoming render of the sidebar comes out correctly
						instrumentClipView.changeRootNote(y);
						instrumentClipView.exitScaleModeOnButtonRelease = false;
					}
					else {
						instrumentClipView.enterScaleMode(y);
						uiNeedsRendering(this);
					}
				}
			}

			// Actual basic audition pad press:
			else if (!velocity || isUIModeWithinRange(auditionPadActionUIModes)) {
				if (sdRoutineLock && !allowSomeUserActionsEvenWhenInCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allowable sometimes if in card routine.
				}
				auditionPadAction(velocity, y, Buttons::isShiftButtonPressed());
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

//edit pad action
//handles single and multi pad presses for automation editing and for parameter selection on the automation overview
//stores pad presses in the EditPadPresses struct of the instrument clip view

void AutomationInstrumentClipView::editPadAction(bool state, uint8_t yDisplay, uint8_t xDisplay, uint32_t xZoom) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If button down
	if (state) {
		if (!isSquareDefined(xDisplay)) {

			return;
		}
		// If this is a note-length-edit press...
		//needed for Automation
		if (clip->lastSelectedParamID != 255 && instrumentClipView.numEditPadPresses == 1
		    && ((int32_t)(instrumentClipView.timeLastEditPadPress + 80 * 44 - AudioEngine::audioSampleTimer) < 0)) {

			int32_t firstPadX = 255;
			int32_t firstPadY = 255;

			// Find that original press
			int32_t i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (instrumentClipView.editPadPresses[i].isActive) {

					firstPadX = instrumentClipView.editPadPresses[i].xDisplay;
					firstPadY = instrumentClipView.editPadPresses[i].yDisplay;

					break;
				}
			}

			if (firstPadX != 255 && firstPadY != 255) {
				handleMultiPadPress(modelStack, clip, firstPadX, firstPadY, xDisplay, yDisplay);
			}
		}

		// Or, if this is a regular create-or-select press...
		else {

			instrumentClipView.timeLastEditPadPress = AudioEngine::audioSampleTimer;
			// Find an empty space in the press buffer, if there is one
			int32_t i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (!instrumentClipView.editPadPresses[i].isActive) {
					break;
				}
			}
			if (i < kEditPadPressBufferSize) {
				handleSinglePadPress(modelStack, clip, xDisplay, yDisplay);

				instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;

				// If this is the first press, record the time
				if (instrumentClipView.numEditPadPresses == 0) {
					instrumentClipView.timeFirstEditPadPress = AudioEngine::audioSampleTimer;
					instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				}

				instrumentClipView.editPadPresses[i].isActive = true;
				instrumentClipView.editPadPresses[i].yDisplay = yDisplay;
				instrumentClipView.editPadPresses[i].xDisplay = xDisplay;
				instrumentClipView.numEditPadPresses++;
				instrumentClipView.numEditPadPressesPerNoteRowOnScreen[yDisplay]++;
				enterUIMode(UI_MODE_NOTES_PRESSED);
			}
		}
	}

	// Or if pad press ended...
	else {

		// Find the corresponding press, if there is one
		int32_t i;
		for (i = 0; i < kEditPadPressBufferSize; i++) {
			if (instrumentClipView.editPadPresses[i].isActive
			    && instrumentClipView.editPadPresses[i].yDisplay == yDisplay
			    && instrumentClipView.editPadPresses[i].xDisplay == xDisplay) {
				break;
			}
		}

		// If we found it...
		if (i < kEditPadPressBufferSize) {

			numericDriver.cancelPopup(); // Crude way of getting rid of the probability-editing permanent popup

			instrumentClipView.endEditPadPress(i);

			instrumentClipView.checkIfAllEditPadPressesEnded();
		}
	}
}

//audition pad action
//don't think I changed anything in here compared to the instrument clip view version

void AutomationInstrumentClipView::auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	bool clipIsActiveOnInstrument = makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	bool isKit = (instrument->type == InstrumentType::KIT);

	bool renderingNeeded = false;

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
	    clip->getNoteRowOnScreen(yDisplay, modelStackWithTimelineCounter);

	Drum* drum = NULL;

	// If Kit...
	if (isKit) {

		if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
			drum = modelStackWithNoteRowOnCurrentClip->getNoteRow()->drum;
			if (((Kit*)instrument)->selectedDrum != drum) {
				initParameterSelection();
				renderingNeeded = true;
			}
		}

		// If NoteRow doesn't exist here, we'll see about creating one
		else {

			// But not if we're actually not on this screen
			if (getCurrentUI() != this) {
				return;
			}

			// Press-down
			if (velocity) {

				instrumentClipView.setSelectedDrum(NULL);
			}

			goto getOut;
		}
	}

	// Or if synth
	else if (instrument->type == InstrumentType::SYNTH) {
		if (velocity) {
			if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
				menu_item::multiRangeMenu.noteOnToChangeRange(clip->getYNoteFromYDisplay(yDisplay, currentSong)
				                                              + ((SoundInstrument*)instrument)->transpose);
			}
		}
	}

	// Recording - only allowed if currentClip is activeClip
	if (clipIsActiveOnInstrument && playbackHandler.shouldRecordNotesNow() && currentSong->isClipActive(clip)) {

		// Note-on
		if (velocity) {

			// If count-in is on, we only got here if it's very nearly finished, so pre-empt that note.
			// This is basic. For MIDI input, we do this in a couple more cases - see noteMessageReceived()
			// in MelodicInstrument and Kit
			if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) {
				if (isKit) {
					if (drum) {
						drum->recordNoteOnEarly((velocity == USE_DEFAULT_VELOCITY) ? instrument->defaultVelocity
						                                                           : velocity,
						                        clip->allowNoteTails(modelStackWithNoteRowOnCurrentClip));
					}
				}
				else {
					int32_t yNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
					((MelodicInstrument*)instrument)
					    ->earlyNotes.insertElementIfNonePresent(
					        yNote, instrument->defaultVelocity,
					        clip->allowNoteTails(
					            modelStackWithNoteRowOnCurrentClip)); // NoteRow is allowed to be NULL in this case.
				}
			}

			else {

				// May need to create NoteRow if there wasn't one previously
				if (!modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {

					modelStackWithNoteRowOnCurrentClip =
					    instrumentClipView.createNoteRowForYDisplay(modelStackWithTimelineCounter, yDisplay);
				}

				if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
					clip->recordNoteOn(modelStackWithNoteRowOnCurrentClip,
					                   (velocity == USE_DEFAULT_VELOCITY) ? instrument->defaultVelocity : velocity);
					goto maybeRenderRow;
				}
			}
		}

		// Note-off
		else {

			if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
				clip->recordNoteOff(modelStackWithNoteRowOnCurrentClip);
maybeRenderRow:
				if (!(currentUIMode & UI_MODE_HORIZONTAL_SCROLL)) { // What about zoom too?
					uiNeedsRendering(this, 1 << yDisplay, 0);
				}
			}
		}
	}

	{
		NoteRow* noteRowOnActiveClip;

		if (clipIsActiveOnInstrument) {
			noteRowOnActiveClip = modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull();
		}

		else {
			// Kit
			if (instrument->type == InstrumentType::KIT) {
				noteRowOnActiveClip = ((InstrumentClip*)instrument->activeClip)->getNoteRowForDrum(drum);
			}

			// Non-kit
			else {
				int32_t yNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
				noteRowOnActiveClip = ((InstrumentClip*)instrument->activeClip)->getNoteRowForYNote(yNote);
			}
		}

		// If note on...
		if (velocity) {
			if (instrumentClipView.lastAuditionedYDisplay
			    != yDisplay) { //if you pressed a different audition pad than last time, refresh grid
				renderingNeeded = true;
			}

			int32_t velocityToSound = velocity;
			if (velocityToSound == USE_DEFAULT_VELOCITY) {
				velocityToSound = instrument->defaultVelocity;
			}

			instrumentClipView.auditionPadIsPressed[yDisplay] =
			    velocityToSound; // Yup, need to do this even if we're going to do a "silent" audition, so pad lights up etc.

			if (noteRowOnActiveClip) {
				// Ensure our auditioning doesn't override a note playing in the sequence
				if (playbackHandler.isEitherClockActive()
				    && noteRowOnActiveClip->soundingStatus == STATUS_SEQUENCED_NOTE) {
					goto doSilentAudition;
				}
			}

			// If won't be actually sounding Instrument...
			if (shiftButtonDown || Buttons::isButtonPressed(hid::button::Y_ENC)) {

				instrumentClipView.fileBrowserShouldNotPreview = true;
doSilentAudition:
				instrumentClipView.auditioningSilently = true;
				instrumentClipView.reassessAllAuditionStatus();
			}
			else {
				if (!instrumentClipView.auditioningSilently) {

					instrumentClipView.fileBrowserShouldNotPreview = false;

					instrumentClipView.sendAuditionNote(true, yDisplay, velocityToSound, 0);

					{ instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = velocityToSound; }
				}
			}

			// If wasn't already auditioning...
			if (!isUIModeActive(UI_MODE_AUDITIONING)) {
				instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				//instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = false;
				enterUIMode(UI_MODE_AUDITIONING);
			}

			instrumentClipView.drawNoteCode(yDisplay);
			instrumentClipView.lastAuditionedYDisplay = yDisplay;

			// Begin resampling / output-recording
			if (Buttons::isButtonPressed(hid::button::RECORD)
			    && audioRecorder.recordingSource == AudioInputChannel::NONE) {
				audioRecorder.beginOutputRecording();
				Buttons::recordButtonPressUsedUp = true;
			}

			if (isKit) {
				instrumentClipView.setSelectedDrum(drum);
				//uiNeedsRendering(this); // need to redraw automation grid squares cause selected drum may have changed
				goto getOut;
			}
		}

		// Or if auditioning this NoteRow just finished...
		else {
			if (instrumentClipView.auditionPadIsPressed[yDisplay]) {
				instrumentClipView.auditionPadIsPressed[yDisplay] = 0;
				instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = 255;

				// Stop the note sounding - but only if a sequenced note isn't in fact being played here.
				if (!noteRowOnActiveClip || noteRowOnActiveClip->soundingStatus == STATUS_OFF) {
					instrumentClipView.sendAuditionNote(false, yDisplay, 64, 0);
				}
			}
			instrumentClipView.someAuditioningHasEnded(true);
			actionLogger.closeAction(ACTION_NOTEROW_ROTATE);
		}

		renderingNeededRegardlessOfUI(0, 1 << yDisplay);
	}

getOut:

	// This has to happen after instrumentClipView.setSelectedDrum is called, cos that resets LEDs
	if (!clipIsActiveOnInstrument && velocity) {
		indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
	}

	if (renderingNeeded) {
		uiNeedsRendering(this);
	}
}

//horizontal encoder action
//using this to shift automations left / right
ActionResult AutomationInstrumentClipView::horizontalEncoderAction(int32_t offset) {

	InstrumentClip* clip = getCurrentClip();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	encoderAction = true;

	if (clip->lastSelectedParamID != 255
	    && ((isNoUIModeActive() && Buttons::isButtonPressed(hid::button::Y_ENC))
	        || (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
	            && Buttons::isButtonPressed(hid::button::CLIP_VIEW))
	        || (isUIModeActiveExclusively(UI_MODE_AUDITIONING | UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)))) {

		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
		}

		shiftAutomationHorizontally(offset);

		if (offset < 0) {
			numericDriver.displayPopup(HAVE_OLED ? "Shift Left" : "LEFT");
		}
		else if (offset > 0) {
			numericDriver.displayPopup(HAVE_OLED ? "Shift Right" : "RIGHT");
		}

		return ActionResult::DEALT_WITH;
	}

	//else if showing the Parameter selection grid menu, disable this action
	else if (isOnParameterGridMenuView()) {
		return ActionResult::DEALT_WITH;
	}

	// Auditioning but not holding down <> encoder - edit length of just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		if (!instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
wantToEditNoteRowLength:
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
			}

			ModelStackWithNoteRow* modelStackWithNoteRow =
			    instrumentClipView.getOrCreateNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);

			instrumentClipView.editNoteRowLength(modelStackWithNoteRow, offset,
			                                     instrumentClipView.lastAuditionedYDisplay);
			//instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			uiNeedsRendering(this);
		}

		// Unlike for all other cases where we protect against the user accidentally turning the encoder more after releasing their press on it,
		// for this edit-NoteRow-length action, because it's a related action, it's quite likely that the user actually will want to do it after the yes-pressed-encoder-down
		// action, which is "rotate/shift notes in row". So, we have a 250ms timeout for this one.
		else if ((uint32_t)(AudioEngine::audioSampleTimer - instrumentClipView.timeHorizontalKnobLastReleased)
		         >= 250 * 44) {
			instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
			goto wantToEditNoteRowLength;
		}
		return ActionResult::DEALT_WITH;
	}

	// Or, let parent deal with it
	else {
		ActionResult result = ClipView::horizontalEncoderAction(offset);
		uiNeedsRendering(this);
		return result;
	}
}

//new function created for automation instrument clip view to shift automations of the selected parameter
//previously users only had the option to shift ALL automations together
//as part of community feature i disabled automation shifting in the regular instrument clip view
void AutomationInstrumentClipView::shiftAutomationHorizontally(int32_t offset) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

			uint32_t squareStart = getPosFromSquare(xDisplay);

			int32_t effectiveLength;

			if (instrument->type == InstrumentType::KIT) {
				ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

				effectiveLength = modelStackWithNoteRow->getLoopLength();
			}
			else {
				effectiveLength = clip->loopLength; //this will differ for a kit when in note row mode
			}

			if (squareStart < effectiveLength) {
				modelStackWithParam->autoParam->shiftHorizontally(offset, effectiveLength);
			}
		}
	}

	uiNeedsRendering(this);
}

//vertical encoder action
//no change compared to instrument clip view version

ActionResult AutomationInstrumentClipView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;
	encoderAction = true;

	if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
	}

	// If encoder button pressed
	if (Buttons::isButtonPressed(hid::button::Y_ENC)) {
		// If user not wanting to move a noteCode, they want to transpose the key
		if (!currentUIMode && instrument->type != InstrumentType::KIT) {

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			actionLogger.deleteAllLogs();

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			// If shift button not pressed, transpose whole octave
			if (!Buttons::isShiftButtonPressed()) {
				offset = std::min((int32_t)1, std::max((int32_t)-1, offset));
				clip->transpose(offset * 12, modelStack);
				if (clip->isScaleModeClip()) {
					clip->yScroll += offset * (currentSong->numModeNotes - 12);
				}
				//numericDriver.displayPopup("OCTAVE");
			}

			// Otherwise, transpose single semitone
			else {
				// If current Clip not in scale-mode, just do it
				if (!clip->isScaleModeClip()) {
					clip->transpose(offset, modelStack);

					// If there are no scale-mode Clips at all, move the root note along as well - just in case the user wants to go back to scale mode (in which case the "previous" root note would be used to help guess what root note to go with)
					if (!currentSong->anyScaleModeClips()) {
						currentSong->rootNote += offset;
					}
				}

				// Otherwise, got to do all key-mode Clips
				else {
					currentSong->transposeAllScaleModeClips(offset);
				}
				//numericDriver.displayPopup("SEMITONE");
			}
		}
	}

	// Or, if shift key is pressed
	else if (Buttons::isShiftButtonPressed()) {
		uint32_t whichRowsToRender = 0;

		// If NoteRow(s) auditioned, shift its colour (Kits only)
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			//instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			if (!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
				if (instrument->type != InstrumentType::KIT) {
					goto shiftAllColour;
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
					if (instrumentClipView.auditionPadIsPressed[yDisplay]) {
						ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);
						NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
						if (noteRow) { // This is fine. If we were in Kit mode, we could only be auditioning if there was a NoteRow already
							noteRow->colourOffset += offset;
							if (noteRow->colourOffset >= 72) {
								noteRow->colourOffset -= 72;
							}
							if (noteRow->colourOffset < 0) {
								noteRow->colourOffset += 72;
							}
							instrumentClipView.recalculateColour(yDisplay);
							whichRowsToRender |= (1 << yDisplay);
						}
					}
				}
			}
		}

		// Otherwise, adjust whole colour spectrum
		else if (currentUIMode == UI_MODE_NONE) {
shiftAllColour:
			clip->colourOffset += offset;
			instrumentClipView.recalculateColours();
			whichRowsToRender = 0xFFFFFFFF;
		}

		if (whichRowsToRender) {
			uiNeedsRendering(this, whichRowsToRender, whichRowsToRender);
		}
	}

	// If neither button is pressed, we'll do vertical scrolling
	else {
		if (isUIModeWithinRange(verticalScrollUIModes)) {
			if (!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress
			    || (!isUIModeActive(UI_MODE_NOTES_PRESSED) && !isUIModeActive(UI_MODE_AUDITIONING))) {

				return scrollVertical(offset, inCardRoutine, false);
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

//don't think I touch anything here compared to the instrument clip view version
//so I might be able to remove this function and simply call the instrumentClipView.scrollVertical
//I'm not doing anything with it
//But this function could come in handy in the future if I implement vertical zooming

ActionResult AutomationInstrumentClipView::scrollVertical(int32_t scrollAmount, bool inCardRoutine,
                                                          bool draggingNoteRow) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	int32_t noteRowToShiftI;
	int32_t noteRowToSwapWithI;

	bool isKit = instrument->type == InstrumentType::KIT;

	// If a Kit...
	if (isKit) {
		// Limit scrolling
		if (scrollAmount >= 0) {
			if ((int16_t)(clip->yScroll + scrollAmount) > (int16_t)(clip->getNumNoteRows() - 1)) {
				return ActionResult::DEALT_WITH;
			}
		}
		else {
			if (clip->yScroll + scrollAmount < 1 - kDisplayHeight) {
				return ActionResult::DEALT_WITH;
			}
		}
	}

	// Or if not a Kit...
	else {
		int32_t newYNote;
		if (scrollAmount > 0) {
			newYNote = clip->getYNoteFromYDisplay(kDisplayHeight - 1 + scrollAmount, currentSong);
		}
		else {
			newYNote = clip->getYNoteFromYDisplay(scrollAmount, currentSong);
		}

		if (!clip->isScrollWithinRange(scrollAmount, newYNote)) {
			return ActionResult::DEALT_WITH;
		}
	}

	if (inCardRoutine && (instrumentClipView.numEditPadPresses || draggingNoteRow)) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	bool currentClipIsActive = currentSong->isClipActive(clip);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// Switch off any auditioned notes. But leave on the one whose NoteRow we're moving, if we are
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] != 255
		    && (!draggingNoteRow || instrumentClipView.lastAuditionedYDisplay != yDisplay)) {
			instrumentClipView.sendAuditionNote(false, yDisplay, 127, 0);

			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			if (noteRow) {
				// If recording, record a note-off for this NoteRow, if one exists
				if (playbackHandler.shouldRecordNotesNow() && currentClipIsActive) {
					clip->recordNoteOff(modelStackWithNoteRow);
				}
			}
		}
	}

	// Do actual scroll
	clip->yScroll += scrollAmount;

	instrumentClipView
	    .recalculateColours(); // Don't render - we'll do that after we've dealt with presses (potentially creating Notes)

	// Switch on any auditioned notes - remembering that the one we're shifting (if we are) was left on before
	bool drawnNoteCodeYet = false;
	bool forceStoppedAnyAuditioning = false;
	bool changedActiveModControllable = false;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] != 255) {
			// If shifting a NoteRow..
			if (draggingNoteRow && instrumentClipView.lastAuditionedYDisplay == yDisplay) {}

			// Otherwise, switch its audition back on
			else {
				// Check NoteRow exists, incase we've got a Kit
				ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

				if (!isKit || modelStackWithNoteRow->getNoteRowAllowNull()) {

					if (modelStackWithNoteRow->getNoteRowAllowNull()
					    && modelStackWithNoteRow->getNoteRow()->soundingStatus == STATUS_SEQUENCED_NOTE) {}
					else {

						// Record note-on if we're recording
						if (playbackHandler.shouldRecordNotesNow() && currentClipIsActive) {

							// If no NoteRow existed before, try creating one
							if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
								modelStackWithNoteRow =
								    instrumentClipView.createNoteRowForYDisplay(modelStack, yDisplay);
							}

							if (modelStackWithNoteRow->getNoteRowAllowNull()) {
								clip->recordNoteOn(modelStackWithNoteRow, instrument->defaultVelocity);
							}
						}

						instrumentClipView.sendAuditionNote(
						    true, yDisplay, instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay],
						    0); // Should this technically grab the note-length of the note if there is one?
					}
				}
				else {
					instrumentClipView.auditionPadIsPressed[yDisplay] = false;
					instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = 255;
					forceStoppedAnyAuditioning = true;
				}
			}
			if (!draggingNoteRow && !drawnNoteCodeYet
			    && instrumentClipView.auditionPadIsPressed
			           [yDisplay]) { // If we're shiftingNoteRow, no need to re-draw the noteCode, because it'll be the same
				instrumentClipView.drawNoteCode(yDisplay);
				if (isKit) {
					Drum* newSelectedDrum = NULL;
					NoteRow* noteRow = clip->getNoteRowOnScreen(yDisplay, currentSong);
					if (noteRow) {
						newSelectedDrum = noteRow->drum;
					}
					instrumentClipView.setSelectedDrum(newSelectedDrum, true);
					changedActiveModControllable = !instrumentClipView.getAffectEntire();
				}

				if (instrument->type == InstrumentType::SYNTH) {
					if (getCurrentUI() == &soundEditor
					    && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
						menu_item::multiRangeMenu.noteOnToChangeRange(clip->getYNoteFromYDisplay(yDisplay, currentSong)
						                                              + ((SoundInstrument*)instrument)->transpose);
					}
				}

				drawnNoteCodeYet = true;
			}
		}
	}
	if (forceStoppedAnyAuditioning) {
		instrumentClipView.someAuditioningHasEnded(true);
	}

	uiNeedsRendering(this); // Might be in waveform view
	return ActionResult::DEALT_WITH;
}

//mod encoder action

//used to change the value of a step when you press and hold a pad on the timeline
//used to record live automations in
void AutomationInstrumentClipView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {

	encoderAction = true;

	instrumentClipView.dontDeleteNotesOnDepress();

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	//if user holding a node down, we'll adjust the value of the selected parameter being automated
	if (currentUIMode == UI_MODE_NOTES_PRESSED) {

		if (clip->lastSelectedParamID != 255 && instrumentClipView.numEditPadPresses > 0
		    && ((int32_t)(instrumentClipView.timeLastEditPadPress + 80 * 44 - AudioEngine::audioSampleTimer) < 0)) {

			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

			if (modelStackWithParam && modelStackWithParam->autoParam) {

				// find pads that are currently pressed
				int32_t i;
				for (i = 0; i < kEditPadPressBufferSize; i++) {
					if (instrumentClipView.editPadPresses[i].isActive) {

						uint32_t squareStart = getPosFromSquare(instrumentClipView.editPadPresses[i].xDisplay);

						int32_t effectiveLength;

						if (instrument->type == InstrumentType::KIT) {
							ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

							effectiveLength = modelStackWithNoteRow->getLoopLength();
						}
						else {
							effectiveLength = clip->loopLength; //this will differ for a kit when in note row mode
						}

						if (squareStart < effectiveLength) {

							int32_t previousValue =
							    modelStackWithParam->autoParam->getValuePossiblyAtPos(squareStart, modelStackWithParam);
							int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(
							    previousValue, modelStackWithParam);

							int32_t newKnobPos = calculateKnobPosForModEncoderTurn(knobPos, offset);

							automationInstrumentClipView.interpolationBefore = false;
							automationInstrumentClipView.interpolationAfter = false;

							setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart,
							                            instrumentClipView.editPadPresses[i].xDisplay, effectiveLength);
						}
					}
				}
			}
		}

		else {
			goto followOnAction;
		}
	}

	else { //if playback is enabled and you are recording, you will be able to record in live automations for the selected parameter

		if (clip->lastSelectedParamID != 255) {

			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

			if (modelStackWithParam && modelStackWithParam->autoParam) {

				if (modelStackWithParam->getTimelineCounter()
				    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

					int32_t previousValue =
					    modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);
					int32_t knobPos =
					    modelStackWithParam->paramCollection->paramValueToKnobPos(previousValue, modelStackWithParam);

					int32_t newKnobPos = calculateKnobPosForModEncoderTurn(knobPos, offset);

					int32_t newValue =
					    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

					automationInstrumentClipView.interpolationBefore = false;
					automationInstrumentClipView.interpolationAfter = false;

					modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam,
					                                                          view.modPos, view.modLength);

					modelStack->getTimelineCounter()->instrumentBeenEdited();

					displayParameterValue(newKnobPos + 64);
				}
			}
		}
		else {
			goto followOnAction;
		}
	}

	uiNeedsRendering(this);
	return;

followOnAction:
	ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
}

//used to copy paste automation or to delete automation of the current selected parameter
void AutomationInstrumentClipView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If they want to copy or paste automation...
	if (Buttons::isButtonPressed(hid::button::LEARN)) {
		if (on && instrument->type != InstrumentType::CV) {
			if (Buttons::isShiftButtonPressed()) {
				if (clip->lastSelectedParamID != 255) //paste within Automation Editor
					pasteAutomation();
				else { //paste on Automation Overview
					instrumentClipView.pasteAutomation(whichModEncoder);
				}
			}
			else {
				if (clip->lastSelectedParamID != 255) //copy within Automation Editor
					copyAutomation();
				else { //copy on Automation Overview
					instrumentClipView.copyAutomation(whichModEncoder);
				}
			}
		}
	}

	//delete automation of current parameter selected
	else if (Buttons::isShiftButtonPressed() && clip->lastSelectedParamID != 255) {

		ModelStackWithAutoParam* modelStackWithParam =
		    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

		if (modelStackWithParam && modelStackWithParam->autoParam) {
			Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_DELETE, false);
			modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

			numericDriver.displayPopup(HAVE_OLED ? "Automation deleted" : "DELETED");
		}
	}

	else {
		goto followOnAction;
	}

	uiNeedsRendering(this);
	return;

followOnAction: //it will come here when you are on the automation overview iscreen

	view.modEncoderButtonAction(whichModEncoder, on);
	uiNeedsRendering(this);
}

void AutomationInstrumentClipView::copyAutomation() {
	if (copiedParamAutomation.nodes) {
		GeneralMemoryAllocator::get().dealloc(copiedParamAutomation.nodes);
		copiedParamAutomation.nodes = NULL;
		copiedParamAutomation.numNodes = 0;
	}

	int32_t startPos = getPosFromSquare(0);
	int32_t endPos = getPosFromSquare(kDisplayWidth);
	if (startPos == endPos) {
		return;
	}

	InstrumentClip* clip = getCurrentClip();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];

	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		bool isPatchCable =
		    (modelStackWithParam->paramCollection
		     == modelStackWithParam->paramManager
		            ->getPatchCableSetAllowJibberish()); // Ok this is cursed, but will work fine so long as
		// the possibly invalid memory here doesn't accidentally
		// equal modelStack->paramCollection.
		modelStackWithParam->autoParam->copy(startPos, endPos, &copiedParamAutomation, isPatchCable,
		                                     modelStackWithParam);

		if (copiedParamAutomation.nodes) {
			numericDriver.displayPopup(HAVE_OLED ? "Automation copied" : "COPY");
			return;
		}
	}

	numericDriver.displayPopup(HAVE_OLED ? "No automation to copy" : "NONE");
}

void AutomationInstrumentClipView::pasteAutomation() {
	if (!copiedParamAutomation.nodes) {
		numericDriver.displayPopup(HAVE_OLED ? "No automation to paste" : "NONE");
		return;
	}

	int32_t startPos = getPosFromSquare(0);
	int32_t endPos = getPosFromSquare(kDisplayWidth);

	int32_t pastedAutomationWidth = endPos - startPos;
	if (pastedAutomationWidth == 0) {
		return;
	}

	float scaleFactor = (float)pastedAutomationWidth / copiedParamAutomation.width;

	InstrumentClip* clip = getCurrentClip();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];

	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_PASTE, false);

		if (action) {
			action->recordParamChangeIfNotAlreadySnapshotted(modelStackWithParam, false);
		}

		bool isPatchCable = (modelStackWithParam->paramCollection
		                     == modelStackWithParam->paramManager->getPatchCableSetAllowJibberish());
		// Ok this is cursed, but will work fine so long as
		// the possibly invalid memory here doesn't accidentally
		// equal modelStack->paramCollection.

		modelStackWithParam->autoParam->paste(startPos, endPos, scaleFactor, modelStackWithParam,
		                                      &copiedParamAutomation, isPatchCable);

		numericDriver.displayPopup(HAVE_OLED ? "Automation pasted" : "PASTE");

		if (playbackHandler.isEitherClockActive()) {
			currentPlaybackMode->reversionDone(); // Re-gets automation and stuff
		}

		return;
	}

	numericDriver.displayPopup(HAVE_OLED ? "Can't paste automation" : "CANT");
}

//select encoder action

//used to change the parameter selection and reset shortcut pad settings so that new pad can be blinked
//once parameter is selected
void AutomationInstrumentClipView::selectEncoderAction(int8_t offset) {

	//change midi CC or param ID
	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	clip->lastSelectedParamShortcutX = 255;

	if (instrument->type == InstrumentType::SYNTH || instrument->type == InstrumentType::KIT) {

		if (clip->lastSelectedParamID == 255) {
			clip->lastSelectedParamID = paramsForAutomation[0];
			clip->lastSelectedParamArrayPosition = 0;
		}
		else if ((clip->lastSelectedParamArrayPosition + offset) < 0) {
			clip->lastSelectedParamID = paramsForAutomation[40];
			clip->lastSelectedParamArrayPosition = 40;
		}
		else if ((clip->lastSelectedParamArrayPosition + offset) > 40) {
			clip->lastSelectedParamID = paramsForAutomation[0];
			clip->lastSelectedParamArrayPosition = 0;
		}
		else {
			clip->lastSelectedParamID = paramsForAutomation[clip->lastSelectedParamArrayPosition + offset];
			clip->lastSelectedParamArrayPosition += offset;
		}

		displayParameterName(clip->lastSelectedParamID);

		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {

				if (paramShortcutsForAutomation[x][y] == clip->lastSelectedParamID) {
					clip->lastSelectedParamShortcutX = x;
					clip->lastSelectedParamShortcutY = y;

					goto flashShortcut;
				}
			}
		}
	}

	else if (instrument->type == InstrumentType::MIDI_OUT) {

		if (clip->lastSelectedParamID == 255) {
			clip->lastSelectedParamID = 0;
		}
		else if ((clip->lastSelectedParamID + offset) < 0) {
			clip->lastSelectedParamID = 121;
		}
		else if ((clip->lastSelectedParamID + offset) > 121) {
			clip->lastSelectedParamID = 0;
		}
		else {
			clip->lastSelectedParamID += offset;
		}

		displayParameterName(clip->lastSelectedParamID);

		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				if (midiCCShortcutsForAutomation[x][y] == clip->lastSelectedParamID) {

					clip->lastSelectedParamShortcutX = x;
					clip->lastSelectedParamShortcutY = y;

					goto flashShortcut;
				}
			}
		}

		goto flashShortcut;
	}

	return;

flashShortcut:

	resetShortcutBlinking();
	uiNeedsRendering(this);
}

//tempo encoder action
void AutomationInstrumentClipView::tempoEncoderAction(int8_t offset, bool encoderButtonPressed,
                                                      bool shiftButtonPressed) {

	playbackHandler.tempoEncoderAction(offset, encoderButtonPressed, shiftButtonPressed);
}

//called by melodic_instrument.cpp or kit.cpp
void AutomationInstrumentClipView::noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) {

	instrumentClipView.noteRowChanged(clip, noteRow);
}

//called by playback_handler.cpp
void AutomationInstrumentClipView::notifyPlaybackBegun() {
	instrumentClipView.reassessAllAuditionStatus();
}

//resets the Parameter Selection which sends you back to the Automation Overview screen
//these values are saved on a clip basis
void AutomationInstrumentClipView::initParameterSelection() {
	InstrumentClip* clip = getCurrentClip();

	clip->lastSelectedParamID = 255;
	clip->lastSelectedParamShortcutX = 255;
}

//get's the modelstack for the parameters that are being edited
//the model stack differs for SYNTH's, KIT's, MIDI clip's
ModelStackWithAutoParam* AutomationInstrumentClipView::getModelStackWithParam(ModelStackWithTimelineCounter* modelStack,
                                                                              InstrumentClip* clip, int32_t paramID) {

	ModelStackWithAutoParam* modelStackWithParam = 0;
	Instrument* instrument = (Instrument*)clip->output;

	if (instrument->type == InstrumentType::SYNTH) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(instrument->toModControllable(), &clip->paramManager);

		if (modelStackWithThreeMainThings) {
			ParamCollectionSummary* summary = modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();

			if (summary) {
				ParamSet* paramSet = (ParamSet*)summary->paramCollection;
				modelStackWithParam =
				    modelStackWithThreeMainThings->addParam(paramSet, summary, paramID, &paramSet->params[paramID]);
			}
		}
	}

	else if (instrument->type == InstrumentType::KIT) {
		//for a kit we have two types of automation: with Affect Entire and without Affect Entire
		//for a kit with affect entire off, we are automating information at the noterow level
		if (!instrumentClipView.getAffectEntire()) {

			Drum* drum = ((Kit*)instrument)->selectedDrum;

			if (drum) {
				if (drum->type == DrumType::SOUND) { //no automation for MIDI or CV kit drum types

					ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

					if (modelStackWithNoteRow) {

						ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
						    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow();

						if (modelStackWithThreeMainThings) {

							ParamCollectionSummary* summary =
							    modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();

							if (summary) {
								ParamSet* paramSet = (ParamSet*)summary->paramCollection;
								modelStackWithParam = modelStackWithThreeMainThings->addParam(
								    paramSet, summary, paramID, &paramSet->params[paramID]);
							}
						}
					}
				}
			}
		}

		//code below was my attempts at getting kit affect entire automations working
		//not successful yet, so commented it out.

		/*else {
			//	int32_t noteRowIndex = 0;

			Drum* drum = ((Kit*)instrument)->selectedDrum;

			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

			if (modelStackWithNoteRow) {

				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow();

				if (modelStackWithThreeMainThings) {

					ParamCollectionSummary* summary =
					    modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();

					if (summary) {
						ParamSet* paramSet = (ParamSet*)summary->paramCollection;
						modelStackWithParam = modelStackWithThreeMainThings->addParam(paramSet, summary, paramID,
						                                                              &paramSet->params[paramID]);
					}
				}
			}*/
		//	}
		//	}

		// You must first be sure that noteRow is set, and has a ParamManager
		/*	ModelStackWithThreeMainThings* ModelStackWithNoteRow::addOtherTwoThingsAutomaticallyGivenNoteRow() const {
			ModelStackWithThreeMainThings* toReturn = (ModelStackWithThreeMainThings*)this;
			NoteRow* noteRowHere = getNoteRow();
			InstrumentClip* clip = (InstrumentClip*)getTimelineCounter();
			toReturn->modControllable =
			    (clip->output->type == InstrumentType::KIT && noteRowHere->drum) // What if there's no Drum?
			        ? noteRowHere->drum->toModControllable()
			        : clip->output->toModControllable();
			toReturn->paramManager = &noteRowHere->paramManager;
			return toReturn;
		}		*/

		//if affect entire is enabled, set automation at kit clip level, not kit row level
		/*else if (instrumentClipView.getAffectEntire()) {

			// Get existing NoteRow if there was one
			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay, modelStack);

			// If no NoteRow yet...
			if (modelStackWithNoteRow->getNoteRowAllowNull()) {

				NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

				if (noteRow) {

					ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
						modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow();

					if (modelStackWithThreeMainThings) {

						ParamCollectionSummary* summary =
							modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();

					//	if (summary) {
					//		ParamSet* paramSet = (ParamSet*)summary->paramCollection;
					//		modelStackWithParam = modelStackWithThreeMainThings->addParam(paramSet, summary, paramID,
					//																	  &paramSet->params[paramID]);
					//	}
					}

					//ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
					//	modelStack->addOtherTwoThingsButNoNoteRow(cli->toModControllable(), &clip->paramManager);

					//if (modelStackWithThreeMainThings) {
					//		ParamCollectionSummary* summary = modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();

					//	if (summary) {
					//		ParamSet* paramSet = (ParamSet*)summary->paramCollection;
					//		modelStackWithParam =
					//			modelStackWithThreeMainThings->addParam(paramSet, summary, paramID, &paramSet->params[paramID]);
					//	}
					//}
				}
			}
		}*/
	}

	else if (instrument->type == InstrumentType::MIDI_OUT) {

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(clip->output->toModControllable(), &clip->paramManager);

		if (modelStackWithThreeMainThings) {

			MIDIInstrument* instrument = (MIDIInstrument*)instrument;

			modelStackWithParam =
			    instrument->getParamToControlFromInputMIDIChannel(paramID, modelStackWithThreeMainThings);
		}
	}

	return modelStackWithParam;
}

//this function writes the new values calculated by the handleSinglePadPress and handleMultiPadPress functions
void AutomationInstrumentClipView::setParameterAutomationValue(ModelStackWithAutoParam* modelStack, int32_t knobPos,
                                                               int32_t squareStart, int32_t xDisplay,
                                                               int32_t effectiveLength) {

	int32_t newValue = modelStack->paramCollection->knobPosToParamValue(knobPos, modelStack);

	uint32_t squareWidth = instrumentClipView.getSquareWidth(xDisplay, effectiveLength);

	//called twice because there was a weird bug where for some reason the first call wasn't take effect on
	//one pad (and whatever pad it was changed every time)...super weird...calling twice fixed it...
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);

	modelStack->getTimelineCounter()->instrumentBeenEdited();

	displayParameterValue(knobPos + 64);
}

//takes care of setting the automation value for the single pad that was pressed
void AutomationInstrumentClipView::handleSinglePadPress(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip,
                                                        int32_t xDisplay, int32_t yDisplay, bool shortcutPress) {

	Instrument* instrument = (Instrument*)clip->output;

	if ((shortcutPress || clip->lastSelectedParamID == 255)
	    && !(instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()
	         && !((Kit*)instrument)->selectedDrum)) { //this means you are selecting a parameter

		if ((instrument->type == InstrumentType::SYNTH || instrument->type == InstrumentType::KIT)
		    && paramShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {

			//if you are in a synth or a kit clip and the shortcut is valid, set current selected ParamID
			clip->lastSelectedParamID = paramShortcutsForAutomation[xDisplay][yDisplay];
		}

		else if (instrument->type == InstrumentType::MIDI_OUT
		         && midiCCShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {

			//if you are in a midi clip and the shortcut is valid, set the current selected ParamID
			clip->lastSelectedParamID = midiCCShortcutsForAutomation[xDisplay][yDisplay];
		}

		else {
			return;
		}

		//save the selected parameter ID's shortcut pad x,y coords so that you can setup the shortcut blink
		clip->lastSelectedParamShortcutX = xDisplay;
		clip->lastSelectedParamShortcutY = yDisplay;

		displayParameterName(clip->lastSelectedParamID);
		resetShortcutBlinking();
	}

	else if (clip->lastSelectedParamID != 255) { //this means you are editing a parameter's value

		ModelStackWithAutoParam* modelStackWithParam =
		    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

		if (modelStackWithParam && modelStackWithParam->autoParam) {

			uint32_t squareStart = getPosFromSquare(xDisplay);

			int32_t effectiveLength;

			if (instrument->type == InstrumentType::KIT) {
				ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

				effectiveLength = modelStackWithNoteRow->getLoopLength();
			}
			else {
				effectiveLength = clip->loopLength; //this will differ for a kit when in note row mode
			}

			if (squareStart < effectiveLength) {

				automationInstrumentClipView.interpolationBefore = false;
				automationInstrumentClipView.interpolationAfter = false;

				int32_t newKnobPos = calculateKnobPosForSinglePadPress(yDisplay);
				setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength);
			}
		}
	}

	uiNeedsRendering(this);
}

//calculates what the new parameter value is when you press a single pad
int32_t AutomationInstrumentClipView::calculateKnobPosForSinglePadPress(int32_t yDisplay) {

	int32_t newKnobPos = 0;

	if (yDisplay >= 0
	    && yDisplay
	           < 7) { //if you press bottom pad, value is 0, for all other pads except for the top pad, value = row Y * 18
		newKnobPos = yDisplay * 18;
	}
	else { //if you are pressing the top pad, set the value to max (127)
		newKnobPos = 127;
	}

	newKnobPos =
	    newKnobPos
	    - 64; //in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.

	return newKnobPos;
}

//takes care of setting the automation values for the two pads pressed and the pads in between
void AutomationInstrumentClipView::handleMultiPadPress(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip,
                                                       int32_t firstPadX, int32_t firstPadY, int32_t secondPadX,
                                                       int32_t secondPadY) {

	Instrument* instrument = (Instrument*)clip->output;

	if (modelStack) {

		//calculate value corresponding to the two pads that were pressed in a multi pad press action
		int32_t firstPadValue = calculateKnobPosForSinglePadPress(firstPadY) + 64;
		int32_t secondPadValue = calculateKnobPosForSinglePadPress(secondPadY) + 64;

		ModelStackWithAutoParam* modelStackWithParam =
		    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

		if (modelStackWithParam && modelStackWithParam->autoParam) {

			//loop from the firstPad to the secondPad, setting the values in between
			for (int32_t x = firstPadX; x <= secondPadX; x++) {

				uint32_t squareStart = getPosFromSquare(x);

				int32_t effectiveLength;

				if (instrument->type == InstrumentType::KIT) {
					ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

					effectiveLength = modelStackWithNoteRow->getLoopLength();
				}
				else {
					effectiveLength = clip->loopLength; //this will differ for a kit when in note row mode
				}

				if (squareStart < effectiveLength) {

					if (automationInstrumentClipView.interpolation) {

						//these bool's are used in auto param when the homogenizeregion function is called
						//it enables interpolation which causes the values to be smooth'd at the node level
						automationInstrumentClipView.interpolationBefore = true;
						automationInstrumentClipView.interpolationAfter = true;

						if (x == firstPadX) { //for the first pad, disable interpolation before
							automationInstrumentClipView.interpolationBefore = false;
						}
						else if (x == secondPadX) { //for the second pad, disable interpolation after
							automationInstrumentClipView.interpolationAfter = false;
						}
					}
					else { //if interpolation flag is off, disable these flags as well so homogenize region is reset to default
						automationInstrumentClipView.interpolationBefore = false;
						automationInstrumentClipView.interpolationAfter = false;
					}

					int32_t newKnobPos =
					    calculateKnobPosForMultiPadPress(x, firstPadX, firstPadValue, secondPadX, secondPadValue);
					setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, x, effectiveLength);

					automationInstrumentClipView.interpolationBefore = false;
					automationInstrumentClipView.interpolationAfter = false;
				}
			}
		}
	}

	uiNeedsRendering(this);
}

//linear equation to calculate what the values should be for pads between the two pads pressed
int32_t AutomationInstrumentClipView::calculateKnobPosForMultiPadPress(int32_t xDisplay, int32_t firstPadX,
                                                                       int32_t firstPadValue, int32_t secondPadX,
                                                                       int32_t secondPadValue) {

	int32_t newKnobPos = 0;

	if (xDisplay == firstPadX) {
		//set beg positon to value corresponding to Beginning Position Pad
		newKnobPos = firstPadValue;
	}

	else if (xDisplay == secondPadX) {
		//set end position to value corresponding to End Position Pad
		newKnobPos = secondPadValue;
	}

	else { //linear interpolation formula to calculate the value of the pads in between.
		//f(x) = A + (x - Ax) * ((B - A) / (Bx - Ax))
		newKnobPos =
		    firstPadValue
		    + (xDisplay - firstPadX) * ((((secondPadValue - firstPadValue) << 20) / (secondPadX - firstPadX)) >> 20);
	}

	newKnobPos =
	    newKnobPos
	    - 64; //in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.

	return newKnobPos;
}

//used to calculate new knobPos when you turn the mod encoders (gold knobs)
int32_t AutomationInstrumentClipView::calculateKnobPosForModEncoderTurn(int32_t knobPos, int32_t offset) {

	knobPos = knobPos + 64; //adjust the current knob so that it is within the range of 0-127 for calculation purposes

	int32_t newKnobPos = 0;

	if ((knobPos + offset) < 0) {
		newKnobPos = knobPos;
	}
	else if ((knobPos + offset) <= 127) {
		newKnobPos = knobPos + offset;
	}
	else {
		newKnobPos = knobPos;
	}

	newKnobPos =
	    newKnobPos
	    - 64; //in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.

	return newKnobPos;
}

//used to disable certain actions on the automation overview screen
//e.g. doubling clip length, editing clip length
bool AutomationInstrumentClipView::isOnParameterGridMenuView() {

	InstrumentClip* clip = getCurrentClip();

	if (clip->lastSelectedParamID == 255) {
		return true;
	}
	return false;
}

//displays patched param names or midi cc names
void AutomationInstrumentClipView::displayParameterName(int32_t paramID) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStack, clip, paramID);
	bool isAutomated = false;

	//check if Parameter is currently automated so that the automation status can be drawn on the screen with the Parameter Name
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		if (modelStackWithParam->autoParam->isAutomated()) {
			isAutomated = true;
		}
	}

	if (instrument->type == InstrumentType::SYNTH || instrument->type == InstrumentType::KIT) {

		char buffer[30];

#if HAVE_OLED //drawing Parameter Names on 7SEG isn't legible and not done currently, so won't do it here either

		strcpy(buffer, getPatchedParamDisplayNameForOLED(paramID));

		if (isAutomated) {
			strcat(buffer, "\n(automated)");
		}

		OLED::popupText(buffer, true);

#endif
	}

	else if (instrument->type == InstrumentType::MIDI_OUT) {

		InstrumentClipMinder::drawMIDIControlNumber(paramID, isAutomated);
	}
}

//disable parameter value when it is changed
void AutomationInstrumentClipView::displayParameterValue(int32_t knobPos) {

	char buffer[5];

	intToString(knobPos, buffer);
	numericDriver.displayPopup(buffer);
}

//created this function to undo any existing blinking so that it doesn't get rendered in my view
//also created it so that i can reset blinking when a parameter is deselected or when you enter/exit automation view
void AutomationInstrumentClipView::resetShortcutBlinking() {
	memset(soundEditor.sourceShortcutBlinkFrequencies, 255, sizeof(soundEditor.sourceShortcutBlinkFrequencies));
	uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
	shortcutBlinking = false;
}
