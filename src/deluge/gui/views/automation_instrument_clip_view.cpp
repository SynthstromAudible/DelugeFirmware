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
                                                 UI_MODE_DRAGGING_KIT_NOTEROW, 0};

const uint32_t paramsForAutomation[41] = {
    //this array is sorted in the order that Parameters are scrolled through on the display
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

const uint32_t paramShortcutsForAutomation[kDisplayWidth][kDisplayHeight] = {
    {0xFFFFFFFF, 0xFFFFFFFFL, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
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

const uint32_t midiCCShortcutsForAutomation[kDisplayWidth][kDisplayHeight] = {
    {112, 96, 80, 64, 48, 32, 16, 0},          {113, 97, 81, 65, 49, 33, 17, 1},
    {114, 98, 82, 66, 50, 34, 18, 2},          {115, 99, 83, 67, 51, 35, 19, 3},
    {116, 100, 84, 68, 52, 36, 20, 4},         {117, 101, 85, 69, 53, 37, 21, 5},
    {118, 102, 86, 70, 54, 38, 22, 6},         {119, 103, 87, 71, 55, 39, 23, 7},
    {0xFFFFFFFF, 104, 88, 72, 56, 40, 24, 8},  {0xFFFFFFFF, 105, 89, 73, 57, 41, 25, 9},
    {0xFFFFFFFF, 106, 90, 74, 58, 42, 26, 10}, {0xFFFFFFFF, 107, 91, 75, 59, 43, 27, 11},
    {0xFFFFFFFF, 108, 92, 76, 60, 44, 28, 12}, {0xFFFFFFFF, 109, 93, 77, 61, 45, 29, 13},
    {120, 110, 94, 78, 62, 46, 30, 14},        {121, 111, 95, 79, 63, 47, 31, 15}};

const uint32_t easterEgg[kDisplayWidth][kDisplayHeight] = {
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

//const uint32_t padShortcutsForInterpolation[16][8] = {0};

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
	lastSelectedParamX = 255;
	lastSelectedParamY = 255;
	lastSelectedParamArrayPosition = 0;
	lastEditPadPressXDisplay = 255;
	clipClear = 0;
	interpolation = 0;
	encoderAction = false;
}

inline InstrumentClip* getCurrentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

bool AutomationInstrumentClipView::opened() {

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

	getCurrentClip()->onAutomationInstrumentClipView = true;
	getCurrentClip()->onKeyboardScreen = false;

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
		if (clip->lastSelectedParamID != 255) { //if a Param has been selected for editing, blink its shortcut pad
			soundEditor.setupShortcutBlink(lastSelectedParamX, lastSelectedParamY, 3);
			soundEditor.blinkShortcut();
		}
		else { //unset previously set blink timers if not editing a parameter
			uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
		}
	}
	else {
		encoderAction = false; //doing this so the shortcut doesn't blink like crazy while turning knobs that refresh UI
	}

	PadLEDs::renderingLock = false;

	return true;
}

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

		if (clip->lastSelectedParamID != 255) { //if parameter has been selected, show Automation Editor
			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

			if (modelStackWithParam && modelStackWithParam->autoParam) {

				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					uint32_t squareStart = getPosFromSquare(xDisplay);
					int32_t currentValue =
					    modelStackWithParam->autoParam->getValuePossiblyAtPos(squareStart, modelStackWithParam);
					int32_t knobPos =
					    modelStackWithParam->paramCollection->paramValueToKnobPos(currentValue, modelStackWithParam);
					knobPos = knobPos + 64;

					uint8_t* pixel = image + (yDisplay * imageWidth * 3) + (xDisplay * 3);

					if (knobPos != 0 && knobPos >= yDisplay * 18) {
						memcpy(pixel, &instrumentClipView.rowTailColour[yDisplay], 3);
						occupancyMaskOfRow[xDisplay] = 64;
					}
				}

				int32_t effectiveLength = 0;
				effectiveLength = clip->loopLength;

				if (drawUndefinedArea == true) {

					clip->drawUndefinedArea(xScroll, xZoom, effectiveLength, image + (yDisplay * imageWidth * 3),
					                        occupancyMaskOfRow, renderWidth, this,
					                        currentSong->tripletsOn); // Sends image pointer for just the one row
				}
			}

			else { //render easterEgg
				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					uint8_t* pixel = image + (yDisplay * imageWidth * 3) + (xDisplay * 3);

					if (easterEgg[xDisplay][yDisplay] == 0xFFFFFFFF) {

						memcpy(pixel, &instrumentClipView.rowTailColour[yDisplay], 3);
						occupancyMaskOfRow[xDisplay] = 64;
					}
				}
			}
		}

		else { //if not editing a parameter, show Automation Overview
			if (clip->output->type == InstrumentType::SYNTH || clip->output->type == InstrumentType::KIT) {
				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					uint8_t* pixel = image + (yDisplay * imageWidth * 3) + (xDisplay * 3);

					//	else {
					if (paramShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {
						ModelStackWithAutoParam* modelStackWithParam =
						    getModelStackWithParam(modelStack, clip, paramShortcutsForAutomation[xDisplay][yDisplay]);

						if (modelStackWithParam && modelStackWithParam->autoParam) {
							if (modelStackWithParam->autoParam->isAutomated()) { //highlight pad white if the parameter it represents is currently automated
								pixel[0] = 130;
								pixel[1] = 120;
								pixel[2] = 130;
							}

							else {
								memcpy(pixel, &instrumentClipView.rowTailColour[yDisplay], 3);
							}

							occupancyMaskOfRow[xDisplay] = 64;
						}
						else { //render easterEgg

							if (easterEgg[xDisplay][yDisplay] == 0xFFFFFFFF) {

								memcpy(pixel, &instrumentClipView.rowTailColour[yDisplay], 3);
								occupancyMaskOfRow[xDisplay] = 64;
							}
						}
					}
				}
			}

			else if (clip->output->type == InstrumentType::MIDI_OUT) {
				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					uint8_t* pixel = image + (yDisplay * imageWidth * 3) + (xDisplay * 3);

					if (midiCCShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {

						ModelStackWithAutoParam* modelStackWithParam =
						    getModelStackWithParam(modelStack, clip, midiCCShortcutsForAutomation[xDisplay][yDisplay]);

						if (modelStackWithParam && modelStackWithParam->autoParam) {

							if (modelStackWithParam->autoParam->isAutomated()) { //highlight pad white if the parameter it represents is currently automated
								pixel[0] = 130;
								pixel[1] = 120;
								pixel[2] = 130;
							}

							else {
								memcpy(pixel, &instrumentClipView.rowTailColour[yDisplay], 3);
							}

							occupancyMaskOfRow[xDisplay] = 64;
						}
						else { //render easterEgg

							if (easterEgg[xDisplay][yDisplay] == 0xFFFFFFFF) {

								memcpy(pixel, &instrumentClipView.rowTailColour[yDisplay], 3);
								occupancyMaskOfRow[xDisplay] = 64;
							}
						}
					}
				}
			}

			else { //render easterEgg
				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					uint8_t* pixel = image + (yDisplay * imageWidth * 3) + (xDisplay * 3);

					if (easterEgg[xDisplay][yDisplay] == 0xFFFFFFFF) {

						memcpy(pixel, &instrumentClipView.rowTailColour[yDisplay], 3);
						occupancyMaskOfRow[xDisplay] = 64;
					}
				}
			}
		}
	}
}

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

	// Scale mode button
	if (b == SCALE_MODE) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		// Kits can't do scales!
		if (currentSong->currentClip->output->type == InstrumentType::KIT) {
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

			if (currentSong->lastClipInstanceEnteredStartPos != -1
			    || currentSong->currentClip->isArrangementOnlyClip()) {
				bool success = arrangerView.transitionToArrangementEditor();
				if (!success) {
					goto doOther;
				}
			}
			else {
doOther:
				// Transition to session view
				currentUIMode = UI_MODE_INSTRUMENT_CLIP_COLLAPSING;
				int32_t transitioningToRow = sessionView.getClipPlaceOnScreen(currentSong->currentClip);
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
		}
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			changeRootUI(&keyboardScreen);
		}
	}

	// Clip button - exit mode
	else if (b == CLIP_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			changeRootUI(&instrumentClipView);
		}
	}

	// Wrap edit button
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
					if (clip->wrapEditLevel < currentSong->currentClip->loopLength) {
						clip->wrapEditing = true;
					}
				}

				setLedStates();
			}
		}
	}

	// Kit button. Unlike the other instrument-type buttons, whose code is in InstrumentClipMinder, this one is only allowed in the KeyboardScreen
	else if (b == KIT && currentUIMode == UI_MODE_NONE) {
		if (on) {
			clip->lastSelectedParamID = 255;
			lastEditPadPressXDisplay = 255;

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

	else if (b == SYNTH && currentUIMode != UI_MODE_HOLDING_SAVE_BUTTON
	         && currentUIMode != UI_MODE_HOLDING_LOAD_BUTTON) {
		if (on) {
			clip->lastSelectedParamID = 255;
			lastEditPadPressXDisplay = 255;

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

	else if (b == MIDI) {
		if (on) {
			clip->lastSelectedParamID = 255;
			lastEditPadPressXDisplay = 255;

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) {
				instrumentClipView.changeInstrumentType(InstrumentType::MIDI_OUT);
			}
		}
	}

	else if (b == CV) {
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

	// Vertical encoder button
	else if (b == Y_ENC) {
		//needed for Automation
		if (on) {
			if (interpolation == 0) {
				interpolation = 1;

				numericDriver.displayPopup(HAVE_OLED ? "Interpolation On" : "ON");
			}
			else {
				interpolation = 0;

				numericDriver.displayPopup(HAVE_OLED ? "Interpolation Off" : "OFF");
			}
		}
		//needed for Automation
	}

	//Select encoder
	else if (b == SELECT_ENC) {

		char const* displayText;
		displayText = "Select Encoder";
		numericDriver.displayPopup(displayText);
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

	if (b != SESSION_VIEW && b != SCALE_MODE) {
		uiNeedsRendering(this);
	}

	return ActionResult::DEALT_WITH;
}

//pad action

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

			if (currentSong->currentClip->output->type != InstrumentType::KIT) {
				return ActionResult::DEALT_WITH;
			}
			NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(y, currentSong);
			if (!noteRow || !noteRow->drum) {
				return ActionResult::DEALT_WITH;
			}
			view.noteRowMuteMidiLearnPadPressed(velocity, noteRow);
		}
		else if (isUIModeWithinRange(mutePadActionUIModes) && velocity) {
			instrumentClipView.mutePadPress(y);
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

					if (currentSong->currentClip->output->type == InstrumentType::KIT) {
						NoteRow* thisNoteRow = getCurrentClip()->getNoteRowOnScreen(y, currentSong);
						if (!thisNoteRow || !thisNoteRow->drum) {
							return ActionResult::DEALT_WITH;
						}
						view.drumMidiLearnPadPressed(velocity, thisNoteRow->drum,
						                             (Kit*)currentSong->currentClip->output);
					}
					else {
						view.melodicInstrumentMidiLearnPadPressed(velocity,
						                                          (MelodicInstrument*)currentSong->currentClip->output);
					}
				}
			}

			// Changing the scale:
			else if (isUIModeActiveExclusively(UI_MODE_SCALE_MODE_BUTTON_PRESSED)) {
				if (sdRoutineLock) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				if (velocity
				    && currentSong->currentClip->output->type
				           != InstrumentType::
				               KIT) { // We probably couldn't have got this far if it was a Kit, but let's just check
					if (getCurrentClip()->inScaleMode) {
						currentUIMode = UI_MODE_NONE; // So that the upcoming render of the sidebar comes out correctly
						instrumentClipView.changeRootNote(y);
						instrumentClipView.exitScaleModeOnButtonRelease = false;
					}
					else {
						instrumentClipView.enterScaleMode(y);
					}
				}
			}

			// Actual basic audition pad press:
			else if (!velocity || isUIModeWithinRange(auditionPadActionUIModes)) {
				exitUIMode(UI_MODE_DRAGGING_KIT_NOTEROW);
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

void AutomationInstrumentClipView::editPadAction(bool state, uint8_t yDisplay, uint8_t xDisplay, uint32_t xZoom) {

	uint32_t squareStart = getPosFromSquare(xDisplay);

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If button down
	if (state) {

		// Don't allow further new presses if already done nudging
		//	if (numEditPadPresses && doneAnyNudgingSinceFirstEditPadPress) {

		//		return;
		//	}

		if (!isSquareDefined(xDisplay)) {

			return;
		}

		// Get existing NoteRow if there was one
		ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

		// If no NoteRow yet...
		if (!modelStackWithNoteRow->getNoteRowAllowNull()) {

			// Just check we're not beyond Clip length
			if (squareStart >= clip->loopLength) {
				return;
			}

			// And create the new NoteRow
			if (clip->output->type != InstrumentType::KIT) {
				modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(modelStack, yDisplay);
			}
			else {
				NoteRow* noteRow = NULL;
				int32_t noteRowId;
				modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);
			}

			if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
				//	if (instrument->type == InstrumentType::KIT) {
				//		instrumentClipView.setSelectedDrum(NULL);
				//	}
				return;
			}

			// If that just created a new NoteRow for a Kit, then we can't undo any further back than this
			if (instrument->type == InstrumentType::KIT) {
				actionLogger.deleteAllLogs();
			}
		}

		int32_t effectiveLength = modelStackWithNoteRow->getLoopLength();

		// Now that we've definitely got a NoteRow, check against NoteRow "effective" length here (though it'll very possibly be the same as the Clip length we may have tested against above).
		//don't need this check for automation clip view because we are not working with a note row concept, except for kit's, but we can check that later. for now we just need to get pads pressed.
		//	if (squareStart >= effectiveLength) {
		//		return;
		//	}

		uint32_t squareWidth = instrumentClipView.getSquareWidth(xDisplay, effectiveLength);

		NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

		ParamManagerForTimeline* paramManager = NULL;
		if (instrument->type == InstrumentType::SYNTH) {
			paramManager = &clip->paramManager; //for a synth the params are saved at clip level
		}
		else if (instrument->type == InstrumentType::KIT) {
			paramManager = &noteRow->paramManager; //for a kit, params are saved at note level
		}

		// If this is a note-length-edit press...
		//needed for Automation
		if (instrumentClipView.numEditPadPresses == 1
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
		//needed for Automation

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

				ParamManagerForTimeline* paramManagerDummy;
				Sound* sound = instrumentClipView.getSoundForNoteRow(noteRow, &paramManagerDummy);

				uint32_t whichRowsToReRender = (1 << yDisplay);

				Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

				uint32_t desiredNoteLength = squareWidth;
				if (sound) {

					int32_t yNote;

					if (instrument->type == InstrumentType::KIT) {
						yNote = 60;
					}
					else {
						yNote = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					}

					// If a time-synced sample...
					uint32_t sampleLength = sound->hasAnyTimeStretchSyncing(paramManager, true, yNote);
					if (sampleLength) {
						uint32_t sampleLengthInTicks =
						    ((uint64_t)sampleLength << 32) / currentSong->timePerTimerTickBig;

						// Previously I was having it always jump to a "square" number, but as James Meharry pointed out, what if the Clip is deliberately a
						// non-square length?
						desiredNoteLength = effectiveLength;
						while (!(desiredNoteLength & 1)) {
							desiredNoteLength >>= 1;
						}

						while (desiredNoteLength * 1.41 < sampleLengthInTicks) {
							desiredNoteLength <<= 1;
						}

						// If desired note length too long and no existing notes, extend the Clip (or if the NoteRow has independent length, do that instead).
						if (noteRow->hasNoNotes() && !clip->wrapEditing && desiredNoteLength > effectiveLength) {
							squareStart = 0;
							if (noteRow->loopLengthIfIndependent) {
								noteRow->loopLengthIfIndependent = desiredNoteLength;
							}
							else {
								currentSong->setClipLength(clip, desiredNoteLength, action);

								// Clip length changing may visually change other rows too, so must re-render them all
								whichRowsToReRender = 0xFFFFFFFF;
							}
						}
					}

					// Or if general cut-mode samples - but only for kit Clips, not synth
					else if (instrument->type == InstrumentType::KIT) {
						bool anyLooping;
						sampleLength = sound->hasCutOrLoopModeSamples(paramManager, yNote, &anyLooping);
						if (sampleLength) {

							// If sample loops, we want to cut out before we get to the loop-point
							if (anyLooping) {
								desiredNoteLength = ((uint64_t)sampleLength << 32) / currentSong->timePerTimerTickBig;
							}

							// Or if sample doesn't loop, we want to extend just past the end point
							else {
								desiredNoteLength =
								    (int32_t)((sampleLength - 2) / currentSong->getTimePerTimerTickFloat()) + 1;
							}
						}
					}

					desiredNoteLength = std::max(desiredNoteLength, squareWidth);
				}

				uint32_t maxNoteLengthHere = clip->getWrapEditLevel();
				desiredNoteLength = std::min(desiredNoteLength, maxNoteLengthHere);

				//	Note* firstNote;
				//	Note* lastNote;
				//	uint8_t squareType =
				//	    noteRow->getSquareType(squareStart, squareWidth, &firstNote, &lastNote, modelStackWithNoteRow,
				//	                           clip->allowNoteTails(modelStackWithNoteRow), desiredNoteLength, action,
				//	                           playbackHandler.isEitherClockActive() && currentSong->isClipActive(clip),
				//	                           isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON));

				// If error (no ram left), get out
				//	if (!squareType) {
				//		numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
				//		return;
				//	}

				// Otherwise, we've selected a note
				//	else {

				//needed for Automation
				handleSinglePadPress(modelStack, clip, xDisplay, yDisplay);
				//needed for Automation

				instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;

				// If this is the first press, record the time
				if (instrumentClipView.numEditPadPresses == 0) {
					instrumentClipView.timeFirstEditPadPress = AudioEngine::audioSampleTimer;
					//	doneAnyNudgingSinceFirstEditPadPress = false;
					//	offsettingNudgeNumberDisplay = false;
					instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				}

				//	if (squareType == SQUARE_BLURRED) {
				//		instrumentClipView.editPadPresses[i].intendedPos = squareStart;
				//		instrumentClipView.editPadPresses[i].intendedLength = squareWidth;
				//		instrumentClipView.editPadPresses[i].deleteOnDepress = true;
				//	}
				//	else {
				//		instrumentClipView.editPadPresses[i].intendedPos = lastNote->pos;
				//		instrumentClipView.editPadPresses[i].intendedLength = lastNote->getLength();
				//		instrumentClipView.editPadPresses[i].deleteOnDepress =
				//		    (squareType == SQUARE_NOTE_HEAD || squareType == SQUARE_NOTE_TAIL_UNMODIFIED);
				//	}

				//	instrumentClipView.editPadPresses[i].isBlurredSquare = false; //(squareType == SQUARE_BLURRED);
				//	instrumentClipView.editPadPresses[i].intendedVelocity = firstNote->getVelocity();
				//	instrumentClipView.editPadPresses[i].intendedProbability = firstNote->getProbability();
				instrumentClipView.editPadPresses[i].isActive = true;
				instrumentClipView.editPadPresses[i].yDisplay = yDisplay;
				instrumentClipView.editPadPresses[i].xDisplay = xDisplay;
				//	instrumentClipView.editPadPresses[i].deleteOnScroll = true;
				//	instrumentClipView.editPadPresses[i].mpeCachedYet = false;
				//	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
				//		instrumentClipView.editPadPresses[i].stolenMPE[m].num = 0;
				//	}
				instrumentClipView.numEditPadPresses++;
				instrumentClipView.numEditPadPressesPerNoteRowOnScreen[yDisplay]++;
				enterUIMode(UI_MODE_NOTES_PRESSED);

				// If new note...
				/*	if (squareType == SQUARE_NEW_NOTE) {

						// If we're cross-screen-editing, create other corresponding notes too
						if (clip->wrapEditing) {
							int32_t error = noteRow->addCorrespondingNotes(
							    squareStart, desiredNoteLength, instrumentClipView.editPadPresses[i].intendedVelocity,
							    modelStackWithNoteRow, clip->allowNoteTails(modelStackWithNoteRow), action);

							if (error) {
								numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
							}
						}
					}*/

				// Edit mod knob values for this Note's region
				//	int32_t distanceToNextNote = clip->getDistanceToNextNote(lastNote, modelStackWithNoteRow);

				//	if (instrument->type == InstrumentType::KIT) {
				//		instrumentClipView.setSelectedDrum(noteRow->drum);
				//	}

				// Can only set the mod region after setting the selected drum! Otherwise the params' currentValues don't end up right
				//	view.setModRegion(
				//	    firstNote->pos,
				//	    std::max((uint32_t)distanceToNextNote + lastNote->pos - firstNote->pos, squareWidth),
				//	    modelStackWithNoteRow->noteRowId);

				// Now that we're holding a note down, get set up for if the user wants to edit its MPE values.
				//	for (int32_t t = 0; t < MPE_RECORD_LENGTH_FOR_NOTE_EDITING; t++) {
				//		mpeValuesAtHighestPressure[t][0] = 0;
				//		mpeValuesAtHighestPressure[t][1] = 0;
				//		mpeValuesAtHighestPressure[t][2] = -1; // -1 means not valid yet
				//	}
				//	mpeMostRecentPressure = 0;
				//	mpeRecordLastUpdateTime = AudioEngine::audioSampleTimer;

				instrumentClipView.reassessAuditionStatus(yDisplay);
				//	}

				// Might need to re-render row, if it was changed
				//	if (squareType == SQUARE_NEW_NOTE || squareType == SQUARE_NOTE_TAIL_MODIFIED) {
				//		uiNeedsRendering(this, whichRowsToReRender, 0);
				//	}
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

			//	uint8_t velocity = instrumentClipView.editPadPresses[i].intendedVelocity;

			// Must mark it as inactive first, otherwise, the note-deletion code may do so and then we'd do it again here
			instrumentClipView.endEditPadPress(i);

			// If we're meant to be deleting it on depress...
			/*	if (instrumentClipView.editPadPresses[i].deleteOnDepress
			    && AudioEngine::audioSampleTimer - instrumentClipView.timeLastEditPadPress < (44100 >> 1)) {

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStack);

				Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

				NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

				int32_t wrapEditLevel = clip->getWrapEditLevel();

				noteRow->clearArea(squareStart, instrumentClipView.getSquareWidth(xDisplay, modelStackWithNoteRow->getLoopLength()),
				                   modelStackWithNoteRow, action, wrapEditLevel);

				noteRow->clearMPEUpUntilNextNote(modelStackWithNoteRow, squareStart, wrapEditLevel, true);

				uiNeedsRendering(this, 1 << yDisplay, 0);
			}*/

			// Or if not deleting...
			//	else {
			//		instrument->defaultVelocity = velocity;
			//	}

			// Close last note nudge action, if there was one - so each such action is for one consistent set of notes
			actionLogger.closeAction(ACTION_NOTE_NUDGE);

			// If *all* presses are now ended
			instrumentClipView.checkIfAllEditPadPressesEnded();

			instrumentClipView.reassessAuditionStatus(yDisplay);
		}
	}
}

//audition pad action

void AutomationInstrumentClipView::auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	bool clipIsActiveOnInstrument = makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

	Instrument* instrument = (Instrument*)currentSong->currentClip->output;

	bool isKit = (instrument->type == InstrumentType::KIT);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
	    modelStack->addTimelineCounter(currentSong->currentClip);
	ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
	    getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStackWithTimelineCounter);

	Drum* drum = NULL;

	// If Kit...
	if (isKit) {

		if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
			drum = modelStackWithNoteRowOnCurrentClip->getNoteRow()->drum;
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
				menu_item::multiRangeMenu.noteOnToChangeRange(
				    getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong)
				    + ((SoundInstrument*)instrument)->transpose);
			}
		}
	}

	// Recording - only allowed if currentClip is activeClip
	if (clipIsActiveOnInstrument && playbackHandler.shouldRecordNotesNow()
	    && currentSong->isClipActive(currentSong->currentClip)) {

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
						                        getCurrentClip()->allowNoteTails(modelStackWithNoteRowOnCurrentClip));
					}
				}
				else {
					int32_t yNote = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					((MelodicInstrument*)instrument)
					    ->earlyNotes.insertElementIfNonePresent(
					        yNote, instrument->defaultVelocity,
					        getCurrentClip()->allowNoteTails(
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
					getCurrentClip()->recordNoteOn(modelStackWithNoteRowOnCurrentClip,
					                               (velocity == USE_DEFAULT_VELOCITY) ? instrument->defaultVelocity
					                                                                  : velocity);
					goto maybeRenderRow;
				}
			}
		}

		// Note-off
		else {

			if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
				getCurrentClip()->recordNoteOff(modelStackWithNoteRowOnCurrentClip);
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
				int32_t yNote = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
				noteRowOnActiveClip = ((InstrumentClip*)instrument->activeClip)->getNoteRowForYNote(yNote);
			}
		}

		// If note on...
		if (velocity) {
			if (instrumentClipView.lastAuditionedYDisplay
			    != yDisplay) { //if you pressed a different audition pad than last time, refresh grid
				uiNeedsRendering(this);
			}

			int32_t velocityToSound = velocity;
			if (velocityToSound == USE_DEFAULT_VELOCITY) {
				velocityToSound = ((Instrument*)currentSong->currentClip->output)->defaultVelocity;
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
				//editedAnyPerNoteRowStuffSinceAuditioningBegan = false;
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
				goto getOut; // No need to redraw any squares, because instrumentClipView.setSelectedDrum() has done it
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
			numericDriver.cancelPopup();                      // In case euclidean stuff was being edited etc
			instrumentClipView.someAuditioningHasEnded(true); //instrumentClipView.lastAuditionedYDisplay == yDisplay);
			actionLogger.closeAction(ACTION_NOTEROW_ROTATE);
		}

		renderingNeededRegardlessOfUI(0, 1 << yDisplay);
	}

getOut:

	// This has to happen after instrumentClipView.setSelectedDrum is called, cos that resets LEDs
	if (!clipIsActiveOnInstrument && velocity) {
		indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
	}
}

//horizontal encoder action

ActionResult AutomationInstrumentClipView::horizontalEncoderAction(int32_t offset) {

	encoderAction = true;

	//if showing the Parameter selection grid menu, disable this action
	if (isOnParameterGridMenuView()) {
		return ActionResult::DEALT_WITH;
	}

	// Holding down vertical encoder button and turning horizontal encoder
	// Holding down clip button, pressing and turning horizontal encoder
	// Shift clip automation horizontally
	else if ((isNoUIModeActive() && Buttons::isButtonPressed(hid::button::Y_ENC))
	         || (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
	             && Buttons::isButtonPressed(hid::button::CLIP_VIEW))) {
		rotateAutomationHorizontally(offset);
		return ActionResult::DEALT_WITH;
	}

	// Auditioning but not holding down <> encoder - edit length of just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		if (!instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
wantToEditNoteRowLength:
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
			}

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    instrumentClipView.getOrCreateNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);

			instrumentClipView.editNoteRowLength(modelStackWithNoteRow, offset,
			                                     instrumentClipView.lastAuditionedYDisplay);
			//editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
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
		return ClipView::horizontalEncoderAction(offset);
	}
}

// Supply offset as 0 to just popup number, not change anything
void AutomationInstrumentClipView::rotateAutomationHorizontally(int32_t offset) {

	//	instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = true;

	// If just popping up number, but multiple presses, we're quite limited with what intelligible stuff we can display
	if (!offset) { // && numEditPadPresses > 1) {
		return;
	}

	//	int32_t resultingTotalOffset = 0;

	//	lastAutomationNudgeOffset += offset;
	//	resultingTotalOffset = lastAutomationNudgeOffset + offset;

	// Nudge automation at NoteRow level, while our ModelStack still has a pointer to the NoteRow
	//			{
	//				ModelStackWithThreeMainThings* modelStackWithThreeMainThingsForNoteRow =
	//				    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow();
	//				noteRow->paramManager.nudgeAutomationHorizontallyAtPos(
	//				    instrumentClipView.editPadPresses[i].intendedPos, offset,
	//				    modelStackWithThreeMainThingsForNoteRow->getLoopLength(), action,
	//				    modelStackWithThreeMainThingsForNoteRow, distanceTilNext);
	//			}

	// WARNING! A bit dodgy, but at this stage, we can no longer refer to modelStackWithNoteRow, cos we're going to reuse its
	// parent ModelStackWithTimelineCounter, below.

	// Nudge automation at Clip level
	//	{

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	InstrumentClip* clip = getCurrentClip();

	if (clip->output->type == InstrumentType::SYNTH) {

		if (clip->lastSelectedParamID != 255) {

			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

			if (modelStackWithParam && modelStackWithParam->autoParam) {

				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					int32_t effectiveLength = clip->loopLength;
					uint32_t squareStart = getPosFromSquare(xDisplay);
					modelStackWithParam->autoParam->moveRegionHorizontally(
					    modelStackWithParam, squareStart, effectiveLength, offset, effectiveLength, NULL);
				}

			}
		}
	}

	else if (clip->output->type == InstrumentType::MIDI_OUT) {

		if (clip->lastSelectedParamID != 255) {

			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

			if (modelStackWithParam && modelStackWithParam->autoParam) {

				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					int32_t effectiveLength = clip->loopLength;
					uint32_t squareStart = getPosFromSquare(xDisplay);
					modelStackWithParam->autoParam->moveRegionHorizontally(
					    modelStackWithParam, squareStart, effectiveLength, offset, effectiveLength, NULL);
				}

			}
		}
	}

	uiNeedsRendering(this);
}

//vertical encoder action

ActionResult AutomationInstrumentClipView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {

	encoderAction = true;

	if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
	}

	// If encoder button pressed
	if (Buttons::isButtonPressed(hid::button::Y_ENC)) {
		// If user not wanting to move a noteCode, they want to transpose the key
		if (!currentUIMode && currentSong->currentClip->output->type != InstrumentType::KIT) {

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			actionLogger.deleteAllLogs();

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			// If shift button not pressed, transpose whole octave
			if (!Buttons::isShiftButtonPressed()) {
				offset = std::min((int32_t)1, std::max((int32_t)-1, offset));
				getCurrentClip()->transpose(offset * 12, modelStack);
				if (getCurrentClip()->isScaleModeClip()) {
					getCurrentClip()->yScroll += offset * (currentSong->numModeNotes - 12);
				}
				//numericDriver.displayPopup("OCTAVE");
			}

			// Otherwise, transpose single semitone
			else {
				// If current Clip not in scale-mode, just do it
				if (!getCurrentClip()->isScaleModeClip()) {
					getCurrentClip()->transpose(offset, modelStack);

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
			//editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			if (!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
				if (getCurrentClip()->output->type != InstrumentType::KIT) {
					goto shiftAllColour;
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
					if (instrumentClipView.auditionPadIsPressed[yDisplay]) {
						ModelStackWithNoteRow* modelStackWithNoteRow =
						    getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStack);
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
			getCurrentClip()->colourOffset += offset;
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
				bool draggingNoteRow = (isUIModeActive(UI_MODE_DRAGGING_KIT_NOTEROW));
				return scrollVertical(offset, inCardRoutine, draggingNoteRow);
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

ActionResult AutomationInstrumentClipView::scrollVertical(int32_t scrollAmount, bool inCardRoutine,
                                                          bool draggingNoteRow) {

	int32_t noteRowToShiftI;
	int32_t noteRowToSwapWithI;

	bool isKit = currentSong->currentClip->output->type == InstrumentType::KIT;

	// If a Kit...
	if (isKit) {
		// Limit scrolling
		if (scrollAmount >= 0) {
			if ((int16_t)(getCurrentClip()->yScroll + scrollAmount)
			    > (int16_t)(getCurrentClip()->getNumNoteRows() - 1)) {
				return ActionResult::DEALT_WITH;
			}
		}
		else {
			if (getCurrentClip()->yScroll + scrollAmount < 1 - kDisplayHeight) {
				return ActionResult::DEALT_WITH;
			}
		}

		// Limit how far we can shift a NoteRow
		if (draggingNoteRow) {
			noteRowToShiftI = instrumentClipView.lastAuditionedYDisplay + getCurrentClip()->yScroll;
			if (noteRowToShiftI < 0 || noteRowToShiftI >= getCurrentClip()->noteRows.getNumElements()) {
				return ActionResult::DEALT_WITH;
			}

			if (scrollAmount >= 0) {
				if (noteRowToShiftI >= getCurrentClip()->noteRows.getNumElements() - 1) {
					return ActionResult::DEALT_WITH;
				}
				noteRowToSwapWithI = noteRowToShiftI + 1;
			}
			else {
				if (noteRowToShiftI == 0) {
					return ActionResult::DEALT_WITH;
				}
				noteRowToSwapWithI = noteRowToShiftI - 1;
			}
		}
	}

	// Or if not a Kit...
	else {
		int32_t newYNote;
		if (scrollAmount > 0) {
			newYNote = getCurrentClip()->getYNoteFromYDisplay(kDisplayHeight - 1 + scrollAmount, currentSong);
		}
		else {
			newYNote = getCurrentClip()->getYNoteFromYDisplay(scrollAmount, currentSong);
		}

		if (!getCurrentClip()->isScrollWithinRange(scrollAmount, newYNote)) {
			return ActionResult::DEALT_WITH;
		}
	}

	if (inCardRoutine && (instrumentClipView.numEditPadPresses || draggingNoteRow)) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	bool currentClipIsActive = currentSong->isClipActive(currentSong->currentClip);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// Switch off any auditioned notes. But leave on the one whose NoteRow we're moving, if we are
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] != 255
		    && (!draggingNoteRow || instrumentClipView.lastAuditionedYDisplay != yDisplay)) {
			instrumentClipView.sendAuditionNote(false, yDisplay, 127, 0);

			ModelStackWithNoteRow* modelStackWithNoteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStack);
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			if (noteRow) {
				// If recording, record a note-off for this NoteRow, if one exists
				if (playbackHandler.shouldRecordNotesNow() && currentClipIsActive) {
					getCurrentClip()->recordNoteOff(modelStackWithNoteRow);
				}
			}
		}
	}

	// Do actual scroll
	getCurrentClip()->yScroll += scrollAmount;

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
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStack);

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
								getCurrentClip()->recordNoteOn(
								    modelStackWithNoteRow,
								    ((Instrument*)currentSong->currentClip->output)->defaultVelocity);
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
					NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
					if (noteRow) {
						newSelectedDrum = noteRow->drum;
					}
					instrumentClipView.setSelectedDrum(newSelectedDrum, true);
					changedActiveModControllable = !instrumentClipView.getAffectEntire();
				}

				if (currentSong->currentClip->output->type == InstrumentType::SYNTH) {
					if (getCurrentUI() == &soundEditor
					    && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
						menu_item::multiRangeMenu.noteOnToChangeRange(
						    getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong)
						    + ((SoundInstrument*)currentSong->currentClip->output)->transpose);
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

void AutomationInstrumentClipView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {

	encoderAction = true;

	instrumentClipView.dontDeleteNotesOnDepress();

	InstrumentClip* clip = getCurrentClip();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	Output* output = clip->output;

	//if user holding a node down, we'll adjust the value of the selected parameter being automated
	if (currentUIMode == UI_MODE_NOTES_PRESSED) {

		if (clip->output->type == InstrumentType::SYNTH || clip->output->type == InstrumentType::KIT) {

			if (clip->lastSelectedParamID != 255 && lastEditPadPressXDisplay != 255) {

				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				ModelStackWithAutoParam* modelStackWithParam =
				    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

				if (modelStackWithParam && modelStackWithParam->autoParam) {

					uint32_t squareStart = getPosFromSquare(lastEditPadPressXDisplay);
					int32_t effectiveLength = clip->loopLength;
					if (squareStart < effectiveLength) {

						int32_t previousValue =
						    modelStackWithParam->autoParam->getValuePossiblyAtPos(squareStart, modelStackWithParam);
						int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(
						    previousValue, modelStackWithParam);

						int32_t newKnobPos = calculateKnobPosForModEncoderTurn(knobPos, offset);

						setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart,
						                            lastEditPadPressXDisplay, effectiveLength);
					}
				}
			}

			else {
				goto followOnAction;
			}
		}

		else if (clip->output->type == InstrumentType::MIDI_OUT) {

			if (clip->lastSelectedParamID != 255 && lastEditPadPressXDisplay != 255) {

				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				ModelStackWithAutoParam* modelStackWithParam =
				    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

				if (modelStackWithParam && modelStackWithParam->autoParam) {

					uint32_t squareStart = getPosFromSquare(lastEditPadPressXDisplay);
					int32_t effectiveLength = clip->loopLength;
					if (squareStart < effectiveLength) {

						int32_t previousValue =
						    modelStackWithParam->autoParam->getValuePossiblyAtPos(squareStart, modelStackWithParam);
						int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(
						    previousValue, modelStackWithParam);

						int32_t newKnobPos = calculateKnobPosForModEncoderTurn(knobPos, offset);

						setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart,
						                            lastEditPadPressXDisplay, effectiveLength);
					}
				}
			}
			else {
				goto followOnAction;
			}
		}
	}

	else { //if playback is enabled and you are recording, you will be able to record in live automations for the selected parameter

		if (clip->output->type == InstrumentType::SYNTH || clip->output->type == InstrumentType::KIT) {

			if (clip->lastSelectedParamID != 255) {

				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				ModelStackWithAutoParam* modelStackWithParam =
				    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

				if (modelStackWithParam && modelStackWithParam->autoParam) {

					if (modelStackWithParam->getTimelineCounter()
					    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

						int32_t previousValue =
						    modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);
						int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(
						    previousValue, modelStackWithParam);

						int32_t newKnobPos = calculateKnobPosForModEncoderTurn(knobPos, offset);

						int32_t newValue =
						    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

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

		else if (clip->output->type == InstrumentType::MIDI_OUT) {

			if (clip->lastSelectedParamID != 255) {

				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				ModelStackWithAutoParam* modelStackWithParam =
				    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

				if (modelStackWithParam && modelStackWithParam->autoParam) {

					if (modelStackWithParam->getTimelineCounter()
					    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

						int32_t previousValue =
						    modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);
						int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(
						    previousValue, modelStackWithParam);

						int32_t newKnobPos = calculateKnobPosForModEncoderTurn(knobPos, offset);

						int32_t newValue =
						    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

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
	}

	uiNeedsRendering(this);
	return;

followOnAction:
	ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
}

void AutomationInstrumentClipView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	InstrumentClip* clip = getCurrentClip();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If they want to copy or paste automation...
	if (Buttons::isButtonPressed(hid::button::LEARN)) {
		if (on && currentSong->currentClip->output->type != InstrumentType::CV) {
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

	//press top mod encoder to display current parameter selected and its automation status
	else if (whichModEncoder == 1 && on && clip->lastSelectedParamID != 255) {
	         //&& (clip->output->type == InstrumentType::SYNTH || clip->output->type == InstrumentType::KIT)) {
		displayParameterName(clip->lastSelectedParamID);
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

	ModelStackWithAutoParam* modelStackWithParam =
	    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

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

	ModelStackWithAutoParam* modelStackWithParam =
	    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

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

void AutomationInstrumentClipView::selectEncoderAction(int8_t offset) {

	encoderAction = true;

	//If the user is holding down mod encoder while turning select, change midi CC or param ID
	if (Buttons::isButtonPressed(hid::button::MOD_ENCODER_0) || Buttons::isButtonPressed(hid::button::MOD_ENCODER_1)) {
		InstrumentClip* clip = getCurrentClip();

		lastSelectedParamX = 255;

		if (clip->output->type == InstrumentType::SYNTH || clip->output->type == InstrumentType::KIT) {

			if (clip->lastSelectedParamID == 255) {
				clip->lastSelectedParamID = paramsForAutomation[0];
				lastSelectedParamArrayPosition = 0;
			}
			else if ((lastSelectedParamArrayPosition + offset) < 0) {
				clip->lastSelectedParamID = paramsForAutomation[40];
				lastSelectedParamArrayPosition = 40;
			}
			else if ((lastSelectedParamArrayPosition + offset) > 40) {
				clip->lastSelectedParamID = paramsForAutomation[0];
				lastSelectedParamArrayPosition = 0;
			}
			else {
				clip->lastSelectedParamID = paramsForAutomation[lastSelectedParamArrayPosition + offset];
				lastSelectedParamArrayPosition += offset;
			}

			displayParameterName(clip->lastSelectedParamID);

			for (int32_t x = 0; x < kDisplayWidth; x++) {
				for (int32_t y = 0; y < kDisplayHeight; y++) {

					if (paramShortcutsForAutomation[x][y] == clip->lastSelectedParamID) {

						lastSelectedParamX = x;
						lastSelectedParamY = y;

						goto flashShortcut;
					}
				}
			}
		}

		else if (clip->output->type == InstrumentType::MIDI_OUT) {

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

						lastSelectedParamX = x;
						lastSelectedParamY = y;

						goto flashShortcut;
					}
				}
			}

			goto flashShortcut;
		}

		return;

flashShortcut:

		if (lastSelectedParamX != 255) {
			soundEditor.setupShortcutBlink(lastSelectedParamX, lastSelectedParamY, 3);
			soundEditor.blinkShortcut();
		}

		uiNeedsRendering(this);
	}

	// Or, normal option - trying to change Instrument presets
	else {

		InstrumentClipMinder::selectEncoderAction(offset);
	}
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

//Automation Lanes
ModelStackWithAutoParam* AutomationInstrumentClipView::getModelStackWithParam(ModelStackWithTimelineCounter* modelStack,
                                                                              InstrumentClip* clip, int32_t paramID) {

	ModelStackWithAutoParam* modelStackWithParam = 0;

	if (clip->output->type == InstrumentType::SYNTH) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(clip->output->toModControllable(), &clip->paramManager);

		if (modelStackWithThreeMainThings) {
			ParamCollectionSummary* summary = modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();

			if (summary) {
				ParamSet* paramSet = (ParamSet*)summary->paramCollection;
				modelStackWithParam =
				    modelStackWithThreeMainThings->addParam(paramSet, summary, paramID, &paramSet->params[paramID]);
			}
		}
	}

	else if (clip->output->type == InstrumentType::KIT) {
		Drum* drum = getCurrentClip()->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay, currentSong)->drum;

		if (!instrumentClipView.getAffectEntire() && drum->type == DrumType::SOUND) {
			int32_t noteRowIndex = 0;

			NoteRow* thisNoteRow =
			    clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay, currentSong, &noteRowIndex);

			if (thisNoteRow) {

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    modelStack->addNoteRow(clip->getNoteRowId(thisNoteRow, noteRowIndex), thisNoteRow);

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
				}
			}
		}
	}

	else if (clip->output->type == InstrumentType::MIDI_OUT) {

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(clip->output->toModControllable(), &clip->paramManager);

		if (modelStackWithThreeMainThings) {

			MIDIInstrument* instrument = (MIDIInstrument*)getCurrentClip()->output;

			modelStackWithParam =
			    instrument->getParamToControlFromInputMIDIChannel(paramID, modelStackWithThreeMainThings);
		}
	}

	return modelStackWithParam;
}

void AutomationInstrumentClipView::setParameterAutomationValue(ModelStackWithAutoParam* modelStack, int32_t knobPos,
                                                               int32_t squareStart, int32_t xDisplay,
                                                               int32_t effectiveLength) {

	int32_t newValue = modelStack->paramCollection->knobPosToParamValue(knobPos, modelStack);

	uint32_t squareWidth = instrumentClipView.getSquareWidth(xDisplay, effectiveLength);

	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);

	modelStack->getTimelineCounter()->instrumentBeenEdited();

	displayParameterValue(knobPos + 64);
}

void AutomationInstrumentClipView::handleSinglePadPress(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip,
                                                        int32_t xDisplay, int32_t yDisplay, bool shortcutPress) {

	if (clip->output->type == InstrumentType::SYNTH || clip->output->type == InstrumentType::KIT) {

		if ((clip->lastSelectedParamID == 255 || (shortcutPress && Buttons::isShiftButtonPressed()))
		    && paramShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {
			clip->lastSelectedParamID = paramShortcutsForAutomation[xDisplay][yDisplay];

			displayParameterName(clip->lastSelectedParamID);

			lastSelectedParamX = xDisplay;
			lastSelectedParamY = yDisplay;

			soundEditor.setupShortcutBlink(xDisplay, yDisplay, 3);
			soundEditor.blinkShortcut();
		}

		else if (clip->lastSelectedParamID != 255) {

			lastEditPadPressXDisplay = xDisplay;

			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

			if (modelStackWithParam && modelStackWithParam->autoParam) {

				uint32_t squareStart = getPosFromSquare(xDisplay);
				int32_t effectiveLength = clip->loopLength;
				if (squareStart < effectiveLength) {

					int32_t newKnobPos = calculateKnobPosForSinglePadPress(yDisplay);
					setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, xDisplay,
					                            effectiveLength);
				}
			}
		}
	}

	else if (clip->output->type == InstrumentType::MIDI_OUT) {

		if ((clip->lastSelectedParamID == 255 || (shortcutPress && Buttons::isShiftButtonPressed()))
		    && midiCCShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {
			clip->lastSelectedParamID = midiCCShortcutsForAutomation[xDisplay][yDisplay];

			displayParameterName(clip->lastSelectedParamID);

			lastSelectedParamX = xDisplay;
			lastSelectedParamY = yDisplay;

			soundEditor.setupShortcutBlink(xDisplay, yDisplay, 3);
			soundEditor.blinkShortcut();
		}

		else if (clip->lastSelectedParamID != 255) {

			lastEditPadPressXDisplay = xDisplay;

			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

			if (modelStackWithParam && modelStackWithParam->autoParam) {

				uint32_t squareStart = getPosFromSquare(xDisplay);
				int32_t effectiveLength = clip->loopLength;
				if (squareStart < effectiveLength) {

					int32_t newKnobPos = calculateKnobPosForSinglePadPress(yDisplay);
					setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, xDisplay,
					                            effectiveLength);
				}
			}
		}
	}

	uiNeedsRendering(this);
}

int32_t AutomationInstrumentClipView::calculateKnobPosForSinglePadPress(int32_t yDisplay) {

	int32_t newKnobPos = 0;

	if (yDisplay >= 0 && yDisplay < 7) {
		newKnobPos = yDisplay * 18;
	}
	else {
		newKnobPos = 127;
	}

	newKnobPos = newKnobPos - 64;

	return newKnobPos;
}

void AutomationInstrumentClipView::handleMultiPadPress(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip,
                                                       int32_t firstPadX, int32_t firstPadY, int32_t secondPadX,
                                                       int32_t secondPadY) {

	if (modelStack) {

		int32_t firstPadValue = calculateKnobPosForSinglePadPress(firstPadY) + 64;
		int32_t secondPadValue = calculateKnobPosForSinglePadPress(secondPadY) + 64;

		int32_t DistanceBetweenPads = secondPadX - firstPadX;

		ModelStackWithAutoParam* modelStackWithParam =
		    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID);

		if (modelStackWithParam && modelStackWithParam->autoParam) {

			for (int32_t x = firstPadX; x <= secondPadX; x++) {

				uint32_t squareStart = getPosFromSquare(x);
				int32_t effectiveLength = clip->loopLength; //this will differ for a kit when in note row mode

				if (squareStart < effectiveLength) {

					int32_t newKnobPos =
					    calculateKnobPosForMultiPadPress(x, firstPadX, firstPadValue, secondPadX, secondPadValue);
					setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, x, effectiveLength);
				}
			}
		}
	}

	uiNeedsRendering(this);
}

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

	else {
		//f(x) = A + (x - Ax) * ((B - A) / (Bx - Ax))
		newKnobPos = firstPadValue
		             + (xDisplay - firstPadX)
		                   * ((((secondPadValue - firstPadValue) * 1000000) / (secondPadX - firstPadX)) / 1000000);
	}

	newKnobPos = newKnobPos - 64;

	return newKnobPos;
}

int32_t AutomationInstrumentClipView::calculateKnobPosForModEncoderTurn(int32_t knobPos, int32_t offset) {

	knobPos = knobPos + 64;

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

	newKnobPos = newKnobPos - 64;

	return newKnobPos;
}

bool AutomationInstrumentClipView::isOnParameterGridMenuView() {

	InstrumentClip* clip = getCurrentClip();

	if (clip->lastSelectedParamID == 255) {
		return true;
	}
	return false;
}

void AutomationInstrumentClipView::displayParameterName(int32_t paramID) {

	InstrumentClip* clip = getCurrentClip();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStack, clip, paramID);
	bool isAutomated = false;

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		if (modelStackWithParam->autoParam->isAutomated()) {
			isAutomated = true;
		}
	}

	if (clip->output->type == InstrumentType::SYNTH || clip->output->type == InstrumentType::KIT) {

		char buffer[30];

#if HAVE_OLED //drawing Parameter Names on 7SEG isn't legible and not done currently, so won't do it here either

		strcpy(buffer, getPatchedParamDisplayNameForOLED(paramID));

		if (isAutomated) {
			strcat(buffer, "\n(automated)");
		}

		OLED::popupText(buffer, true);

#endif
	}

	else if (clip->output->type == InstrumentType::MIDI_OUT) {

		InstrumentClipMinder::drawMIDIControlNumber(paramID, isAutomated);
	}
}

void AutomationInstrumentClipView::displayParameterValue(int32_t knobPos) {

	char buffer[5];

	intToString(knobPos, buffer);
	numericDriver.displayPopup(buffer);
}

//straight line formula:
//A + (B-A)*T/(Distance between A and B)
//f(x) = A + (B-A)*T/(Distance between A and B)

int32_t AutomationInstrumentClipView::LERP(int32_t A, int32_t B, int32_t T, int32_t Distance) {

	int32_t NewValue = 0;

	NewValue = (B - A) * T * 1000000;
	NewValue = NewValue / Distance;
	NewValue = NewValue / 1000000;
	NewValue = A + NewValue;

	return NewValue;
}

int32_t AutomationInstrumentClipView::LERPSweep(int32_t A, int32_t B, int32_t T, int32_t Distance) {

	int32_t NewValue = 0;

	NewValue = (T * T) * 1000000;
	NewValue = NewValue / (Distance * Distance);
	NewValue = (B - A) * NewValue;
	NewValue = NewValue / 1000000;
	NewValue = A + NewValue;

	//A + (((((B-A)*(T*T)) << 2) / (Distance * Distance)) >> 2);

	//=(((1-(((((15-D30)^2)*1000000)/((15)^2))/1000000)^2)))

	return NewValue;
}

int32_t AutomationInstrumentClipView::LERPRoot(int32_t A, int32_t B, int32_t T, int32_t Distance) {

	int32_t NewValue = 0;

	NewValue = (T * T) * 10000;
	NewValue = NewValue / (Distance * Distance);
	NewValue = NewValue * NewValue;
	NewValue = (10000 * 10000) - NewValue;
	NewValue = NewValue / 10000;
	NewValue = (B - A) * NewValue;
	NewValue = NewValue / 10000;
	NewValue = A + NewValue;

	return NewValue;
}

int32_t AutomationInstrumentClipView::LERPSweepDown(int32_t A, int32_t B, int32_t T, int32_t Distance) {

	int32_t NewValue = A + (B - A) * (1 - ((T * T) / (Distance * Distance)));

	return NewValue;
}
