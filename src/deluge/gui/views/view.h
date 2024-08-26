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
#include "model/model_stack.h"
#include "modulation/params/param.h"
#include "processing/audio_output.h"
#include <cstdint>

class InstrumentClip;
class NoteRow;
class UI;
class ModControllable;
class ParamManagerForTimeline;
class RootUI;
class ParamManagerForTimeline;
class MelodicInstrument;
class Drum;
class Instrument;
class TimelineCounter;
class Clip;
class Output;
class AudioClip;
class ModelStackWithThreeMainThings;
class ParamManagerForTimeline;
class MIDIDevice;
class LearnedMIDI;
class Kit;

// A view is where the user can interact with the pads - song view, Clip view, automation view and keyboard view.
// (Is that still a good description? This class is a bit of a mishmash of poorly organised code, sorry.)

/// Assorted utilities for manging the user interface.
class View {
public:
	View();
	void focusRegained();
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	void setTimeBaseScaleLedState();
	void setLedStates();

	void clipStatusMidiLearnPadPressed(bool on, Clip* whichLoopable);
	void noteRowMuteMidiLearnPadPressed(bool on, NoteRow* whichNoteRow);
	void endMidiLearnPressSession(MidiLearn newThingPressed = MidiLearn::NONE);
	void noteOnReceivedForMidiLearn(MIDIDevice* fromDevice, int32_t channel, int32_t note, int32_t velocity);
	void pcReceivedForMIDILearn(MIDIDevice* fromDevice, int32_t channel, int32_t pc) {}
	void ccReceivedForMIDILearn(MIDIDevice* fromDevice, int32_t channel, int32_t cc, int32_t value);
	void drumMidiLearnPadPressed(bool on, Drum* drum, Kit* kit);
	void instrumentMidiLearnPadPressed(bool on, Instrument* instrument);
	void sectionMidiLearnPadPressed(bool on, uint8_t section);
	void midiLearnFlash();
	void setModLedStates();
	int32_t getModKnobMode();
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
	void modButtonAction(uint8_t whichButton, bool on);
	void setKnobIndicatorLevels();
	void setKnobIndicatorLevel(uint8_t whichModEncoder);
	int32_t convertPatchCableKnobPosToIndicatorLevel(int32_t knobPos);
	void setActiveModControllableTimelineCounter(TimelineCounter* playPositionCounter,
	                                             bool shouldSendMidiFeedback = true);
	void setActiveModControllableWithoutTimelineCounter(ModControllable* modControllable, ParamManager* paramManager);
	void cycleThroughReverbPresets();
	char const* getReverbPresetDisplayName(int32_t preset);
	int32_t getCurrentReverbPreset();
	void setModRegion(uint32_t pos = 0xFFFFFFFF, uint32_t length = 0, int32_t noteRowId = 0);
	void notifyParamAutomationOccurred(ParamManager* paramManager, bool updateModLevels = true);
	void displayAutomation();
	void displayOutputName(Output* output, bool doBlink = true, Clip* clip = NULL);
	void instrumentChanged(ModelStackWithTimelineCounter* modelStack, Instrument* newInstrument);
	void navigateThroughPresetsForInstrumentClip(int32_t offset, ModelStackWithTimelineCounter* modelStack,
	                                             bool doBlink = false);
	void navigateThroughAudioOutputsForAudioClip(int32_t offset, AudioClip* clip, bool doBlink = false);
	bool changeOutputType(OutputType newOutputType, ModelStackWithTimelineCounter* modelStack, bool doBlink = false);
	void drawOutputNameFromDetails(OutputType outputType, int32_t slot, int32_t subSlot, char const* name,
	                               bool isNameEmpty, bool editedByUser, bool doBlink, Clip* clip = NULL);
	void startMIDILearn();
	void endMIDILearn();
	[[nodiscard]] RGB getClipMuteSquareColour(Clip* clip, RGB thisColour, bool allowMIDIFlash = true);
	ActionResult clipStatusPadAction(Clip* clip, bool on, int32_t yDisplayIfInSessionView = -1);
	void flashPlayEnable();
	void flashPlayDisable();
	void flashPlayRoutine();

	void activateMacro(uint32_t y);
	Clip* findNextClipForOutput(Output* output);
	bool renderMacros(int32_t column, uint32_t y, int32_t selectedMacro, RGB image[][kDisplayWidth + kSideBarWidth],
	                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

	// MIDI learn stuff
	MidiLearn thingPressedForMidiLearn = MidiLearn::NONE;
	bool deleteMidiCommandOnRelease;
	bool midiLearnFlashOn;
	bool shouldSaveSettingsAfterMidiLearn;

	LearnedMIDI* learnedThing;
	Instrument* instrumentPressedForMIDILearn;
	Drum* drumPressedForMIDILearn;

	// deceivingly this is used only while learning drums
	Kit* kitPressedForMIDILearn;

	ModelStackWithThreeMainThings activeModControllableModelStack;
	uint8_t dummy[MODEL_STACK_MAX_SIZE - sizeof(ModelStackWithThreeMainThings)];

	bool pendingParamAutomationUpdatesModLevels;

	bool clipArmFlashOn;
	bool blinkOn;

	uint32_t timeSaveButtonPressed;

	int32_t modNoteRowId;
	uint32_t modPos;
	// 0 if not currently editing a region / step / holding a note. If you're gonna refer to this, you absolutely have
	// to first check that the TimelineCounter you're thinking of setting some automation on
	// == activeModControllableTimelineCounter
	uint32_t modLength;

	int32_t calculateKnobPosForDisplay(deluge::modulation::params::Kind kind, int32_t paramID, int32_t knobPos);
	void displayModEncoderValuePopup(deluge::modulation::params::Kind kind, int32_t paramID, int32_t newKnobPos,
	                                 PatchSource source1 = PatchSource::NONE, PatchSource source2 = PatchSource::NONE);
	void potentiallyMakeItHarderToTurnKnob(int32_t whichModEncoder, ModelStackWithAutoParam* modelStackWithParam,
	                                       int32_t newKnobPos);
	void sendMidiFollowFeedback(ModelStackWithAutoParam* modelStackWithParam = nullptr, int32_t knobPos = kNoSelection,
	                            bool isAutomation = false);

	// vu meter rendering
	bool displayVUMeter;
	bool potentiallyRenderVUMeter(RGB image[][kDisplayWidth + kSideBarWidth]);

	void getParameterNameFromModEncoder(int32_t whichModEncoder, char* parameterName);

	bool isClipContext();

private:
	void pretendModKnobsUntouchedForAWhile();
	void instrumentBeenEdited();
	void clearMelodicInstrumentMonoExpressionIfPossible();

	// vu meter rendering
	int32_t getMaxYDisplayForVUMeter(float level);
	int32_t cachedMaxYDisplayForVUMeterL;
	int32_t cachedMaxYDisplayForVUMeterR;
	void renderVUMeter(int32_t maxYDisplay, int32_t xDisplay, RGB thisImage[][kDisplayWidth + kSideBarWidth]);
	bool renderedVUMeter;

	// mod encoder action
	void modEncoderAction_nonExistentParam(int32_t whichModEncoder, int32_t offset,
	                                       ModelStackWithAutoParam* modelStackWithParam);
	void modEncoderAction_existentParam(int32_t whichModEncoder, int32_t offset,
	                                    ModelStackWithAutoParam* modelStackWithParam, bool noteTailsAllowedBefore);
	ModelStackWithAutoParam* getModelStackWithParam(int32_t whichModEncoder, bool& noteTailsAllowedBefore);

	// mod encoder button action
	void modEncoderButtonAction_deleteAutomation(uint8_t whichModEncoder);
	void modEncoderButtonAction_changeModControllable(uint8_t whichModEncoder, bool on);

	// Timing constants for display arbitration in displayModEncoderValuePopup (in AudioEngine sample units)
	static constexpr uint32_t MIN_DISPLAY_OWNERSHIP_TIME = kSampleRate; // 1 second (minimum juggling time)
	static constexpr uint32_t DISPLAY_TIMEOUT = kSampleRate / 4;        // 0.25 seconds (ball drop timeout)
	static constexpr uint32_t MIN_UPDATE_INTERVAL = kSampleRate / 22;   // ~45ms (minimum perceptible update frequency)
};

extern View view;
