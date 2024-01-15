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
#include "gui/views/clip_view.h"
#include "hid/button.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "modulation/automation/copied_param_automation.h"
#include "modulation/params/param_node.h"

class Action;
class CopiedNoteRow;
class Drum;
class Editor;
class AudioClip;
class Instrument;
class InstrumentClip;
class MidiInstrument;
class ModControllable;
class ModelStackWithAutoParam;
class ModelStackWithNoteRow;
class ModelStackWithThreeMainThings;
class ModelStackWithTimelineCounter;
class Note;
class NoteRow;
class ParamCollection;
class ParamManagerForTimeline;
class ParamNode;
class Sound;
class SoundDrum;

//grid sized arrays to assign automatable parameters to the grid
//used in automation view and in midi follow

const uint32_t patchedParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {Param::Local::OSC_A_VOLUME, Param::Local::OSC_A_PITCH_ADJUST, kNoParamID, Param::Local::OSC_A_PHASE_WIDTH,
     kNoParamID, Param::Local::CARRIER_0_FEEDBACK, Param::Local::OSC_A_WAVE_INDEX, Param::Local::NOISE_VOLUME},
    {Param::Local::OSC_B_VOLUME, Param::Local::OSC_B_PITCH_ADJUST, kNoParamID, Param::Local::OSC_B_PHASE_WIDTH,
     kNoParamID, Param::Local::CARRIER_1_FEEDBACK, Param::Local::OSC_B_WAVE_INDEX, kNoParamID},
    {Param::Local::MODULATOR_0_VOLUME, Param::Local::MODULATOR_0_PITCH_ADJUST, kNoParamID, kNoParamID, kNoParamID,
     Param::Local::MODULATOR_0_FEEDBACK, kNoParamID, kNoParamID},
    {Param::Local::MODULATOR_1_VOLUME, Param::Local::MODULATOR_1_PITCH_ADJUST, kNoParamID, kNoParamID, kNoParamID,
     Param::Local::MODULATOR_1_FEEDBACK, kNoParamID, kNoParamID},
    {Param::Global::VOLUME_POST_FX, Param::Local::PITCH_ADJUST, kNoParamID, Param::Local::PAN, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Local::FOLD},
    {Param::Local::ENV_0_RELEASE, Param::Local::ENV_0_SUSTAIN, Param::Local::ENV_0_DECAY, Param::Local::ENV_0_ATTACK,
     Param::Local::LPF_MORPH, kNoParamID, Param::Local::LPF_RESONANCE, Param::Local::LPF_FREQ},
    {Param::Local::ENV_1_RELEASE, Param::Local::ENV_1_SUSTAIN, Param::Local::ENV_1_DECAY, Param::Local::ENV_1_ATTACK,
     Param::Local::HPF_MORPH, kNoParamID, Param::Local::HPF_RESONANCE, Param::Local::HPF_FREQ},
    {kNoParamID, kNoParamID, Param::Global::VOLUME_POST_REVERB_SEND, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     kNoParamID},
    {Param::Global::ARP_RATE, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {Param::Global::LFO_FREQ, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Global::MOD_FX_DEPTH,
     Param::Global::MOD_FX_RATE},
    {Param::Local::LFO_LOCAL_FREQ, kNoParamID, kNoParamID, Param::Global::REVERB_AMOUNT, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {Param::Global::DELAY_RATE, kNoParamID, kNoParamID, Param::Global::DELAY_FEEDBACK, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID}};

const uint32_t unpatchedNonGlobalParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::STUTTER_RATE},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::SAMPLE_RATE_REDUCTION,
     Param::Unpatched::BITCRUSHING, kNoParamID},
    {Param::Unpatched::Sound::PORTAMENTO, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::COMPRESSOR_SHAPE, kNoParamID,
     Param::Unpatched::BASS, Param::Unpatched::BASS_FREQ},
    {kNoParamID, kNoParamID, Param::Unpatched::Sound::ARP_GATE, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::TREBLE, Param::Unpatched::TREBLE_FREQ},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::MOD_FX_OFFSET, Param::Unpatched::MOD_FX_FEEDBACK,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID}};

const uint32_t unpatchedGlobalParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {Param::Unpatched::GlobalEffectable::VOLUME, Param::Unpatched::GlobalEffectable::PITCH_ADJUST, kNoParamID,
     Param::Unpatched::GlobalEffectable::PAN, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::LPF_RES, Param::Unpatched::GlobalEffectable::LPF_FREQ},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::HPF_RES, Param::Unpatched::GlobalEffectable::HPF_FREQ},
    {kNoParamID, kNoParamID, Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME, kNoParamID, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH, Param::Unpatched::GlobalEffectable::MOD_FX_RATE},
    {kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {Param::Unpatched::GlobalEffectable::DELAY_RATE, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::DELAY_AMOUNT, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID}};

class AutomationClipView final : public ClipView, public InstrumentClipMinder, public ModControllableAudio {
public:
	AutomationClipView();
	bool opened();
	void openedInBackground();
	void focusRegained();

	//called by ui_timer_manager - might need to revise this routine for automation clip view since it references notes
	void graphicsRoutine();

	//rendering
	bool renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void renderDisplay(int32_t knobPosLeft = kNoSelection, int32_t knobPosRight = kNoSelection,
	                   bool modEncoderAction = false);
	void displayAutomation(bool padSelected = false, bool updateDisplay = true);

	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) { InstrumentClipMinder::renderOLED(image); }

	//button action
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);

	//pad action
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);

	//audition pad action
	void auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown);

	//horizontal encoder action
	ActionResult horizontalEncoderAction(int32_t offset);

	//vertical encoder action
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	ActionResult scrollVertical(int32_t scrollAmount);

	//mod encoder action
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	bool modEncoderActionForSelectedPad(int32_t whichModEncoder, int32_t offset);
	void modEncoderActionForUnselectedPad(int32_t whichModEncoder, int32_t offset);
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
	CopiedParamAutomation copiedParamAutomation;

	//tempo encoder action
	void tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed);

	//Select encoder action
	void selectEncoderAction(int8_t offset);

	//called by melodic_instrument.cpp or kit.cpp
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow);

	//called by playback_handler.cpp
	void notifyPlaybackBegun();

	//not sure how this is used
	ClipMinder* toClipMinder() { return this; }

	bool isOnAutomationOverview();

	bool interpolation;
	bool interpolationBefore;
	bool interpolationAfter;

	//public to midi follow can access it
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                                int32_t paramID = 0xFFFFFFFF,
	                                                Param::Kind paramKind = Param::Kind::NONE);
	ModelStackWithAutoParam* getModelStackWithParamForSynthClip(ModelStackWithTimelineCounter* modelStack,
	                                                            InstrumentClip* clip, int32_t paramID = kNoParamID,
	                                                            Param::Kind paramKind = Param::Kind::NONE);
	ModelStackWithAutoParam* getModelStackWithParamForKitClip(ModelStackWithTimelineCounter* modelStack,
	                                                          InstrumentClip* clip, int32_t paramID = kNoParamID,
	                                                          Param::Kind paramKind = Param::Kind::NONE);
	ModelStackWithAutoParam* getModelStackWithParamForMIDIClip(ModelStackWithTimelineCounter* modelStack,
	                                                           InstrumentClip* clip, int32_t paramID = kNoParamID);
	ModelStackWithAutoParam* getModelStackWithParamForAudioClip(ModelStackWithTimelineCounter* modelStack,
	                                                            AudioClip* clip, int32_t paramID = kNoParamID);

	//public so instrument clip view can access it
	void initParameterSelection();

private:
	//edit pad action
	void editPadAction(Clip* clip, bool state, uint8_t yDisplay, uint8_t xDisplay, uint32_t xZoom);

	//Automation View Render Functions
	void performActualRender(uint32_t whichRows, uint8_t* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                         int32_t xScroll, uint32_t xZoom, int32_t renderWidth, int32_t imageWidth,
	                         bool drawUndefinedArea = true);
	void renderAutomationOverview(ModelStackWithTimelineCounter* modelStack, Clip* clip, OutputType outputType,
	                              uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay = 0);
	void renderAutomationEditor(ModelStackWithTimelineCounter* modelStack, Clip* clip, uint8_t* image,
	                            uint8_t occupancyMask[], int32_t renderWidth, int32_t xScroll, uint32_t xZoom,
	                            int32_t yDisplay = 0, bool drawUndefinedArea = true);
	void renderRow(ModelStackWithTimelineCounter* modelStack, ModelStackWithAutoParam* modelStackWithParam,
	               uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay = 0, bool isAutomated = false);
	void renderLove(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay = 0);
	void renderDisplayOLED(Clip* clip, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       int32_t knobPosRight = kNoSelection);
	void renderDisplay7SEG(Clip* clip, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       bool modEncoderAction = false);

	//Enter/Exit Scale Mode
	void enterScaleMode(uint8_t yDisplay = 255);
	void exitScaleMode();

	//Horizontal Encoder Action
	void shiftAutomationHorizontally(int32_t offset);

	//Mod Encoder Action
	void copyAutomation(Clip* clip);
	void pasteAutomation(Clip* clip);

	//Select Encoder Action
	void selectGlobalParam(int32_t offset, Clip* clip);
	void selectNonGlobalParam(int32_t offset, Clip* clip);
	void selectMIDICC(int32_t offset, Clip* clip);
	int32_t getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamArrayPosition,
	                                          int32_t numParams);
	void getLastSelectedParamShortcut(Clip* clip, OutputType outputType);

	//Automation Lanes Functions
	void initPadSelection();
	void initInterpolation();
	int32_t getEffectiveLength(ModelStackWithTimelineCounter* modelStack);
	uint32_t getMiddlePosFromSquare(ModelStackWithTimelineCounter* modelStack, int32_t xDisplay);

	void getParameterName(Clip* clip, OutputType outputType, char* parameterName);
	int32_t getParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t pos);

	bool getNodeInterpolation(ModelStackWithAutoParam* modelStack, int32_t pos, bool reversed);
	void setParameterAutomationValue(ModelStackWithAutoParam* modelStack, int32_t knobPos, int32_t squareStart,
	                                 int32_t xDisplay, int32_t effectiveLength, bool modEncoderAction = false);
	void setKnobIndicatorLevels(int32_t knobPos);
	void updateModPosition(ModelStackWithAutoParam* modelStack, uint32_t squareStart, bool updateDisplay = true,
	                       bool updateIndicatorLevels = true);

	bool recordSinglePadPress(int32_t xDisplay, int32_t yDisplay);
	void handleSinglePadPress(ModelStackWithTimelineCounter* modelStack, Clip* clip, int32_t xDisplay, int32_t yDisplay,
	                          bool shortcutPress = false);
	int32_t calculateKnobPosForSinglePadPress(OutputType outputType, int32_t yDisplay);

	void handleMultiPadPress(ModelStackWithTimelineCounter* modelStack, Clip* clip, int32_t firstPadX,
	                         int32_t firstPadY, int32_t secondPadX, int32_t secondPadY, bool modEncoderAction = false);
	void renderDisplayForMultiPadPress(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                   int32_t xDisplay = kNoSelection, bool modEncoderAction = false);

	int32_t calculateKnobPosForModEncoderTurn(int32_t knobPos, int32_t offset);
	void displayCVErrorMessage();
	void resetShortcutBlinking();

	bool encoderAction;
	bool shortcutBlinking;

	bool padSelectionOn;
	bool multiPadPressSelected;
	bool multiPadPressActive;
	bool middlePadPressSelected;
	int32_t leftPadSelectedX;
	int32_t leftPadSelectedY;
	int32_t rightPadSelectedX;
	int32_t rightPadSelectedY;
	int32_t lastPadSelectedKnobPos;

	bool playbackStopped;
};

extern AutomationClipView automationClipView;
