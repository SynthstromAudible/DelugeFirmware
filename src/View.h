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

#ifndef VIEW_H_
#define VIEW_H_

#include "r_typedefs.h"
#include "ModelStack.h"

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

// A view is where the user can interact with the pads - song view, Clip view, and keyboard view.
// (Is that still a good description? This class is a bit of a mishmash of poorly organised code, sorry.)

class View {
public:
	View();
	void focusRegained();
	void setTripletsLedState();
	int buttonAction(int x, int y, bool on, bool inCardRoutine);
	void setTimeBaseScaleLedState();
	void setLedStates();

	void clipStatusMidiLearnPadPressed(bool on, Clip* whichLoopable);
	void noteRowMuteMidiLearnPadPressed(bool on, NoteRow* whichNoteRow);
	void endMidiLearnPressSession(uint8_t newThingPressed);
	void noteOnReceivedForMidiLearn(MIDIDevice* fromDevice, int channel, int note, int velocity);
	void ccReceivedForMIDILearn(MIDIDevice* fromDevice, int channel, int cc, int value);
	void drumMidiLearnPadPressed(bool on, Drum* drum, Kit* kit);
	void melodicInstrumentMidiLearnPadPressed(bool on, MelodicInstrument* instrument);
	void sectionMidiLearnPadPressed(bool on, uint8_t section);
	void midiLearnFlash();
	void setModLedStates();
	void modEncoderAction(int whichModEncoder, int offset);
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
	void modButtonAction(uint8_t whichButton, bool on);
	void setKnobIndicatorLevels();
	void setKnobIndicatorLevel(uint8_t whichModEncoder);
	void setActiveModControllableTimelineCounter(TimelineCounter* playPositionCounter);
	void setActiveModControllableWithoutTimelineCounter(ModControllable* modControllable, ParamManager* paramManager);
	void cycleThroughReverbPresets();
	void setModRegion(uint32_t pos = 0xFFFFFFFF, uint32_t length = 0, int noteRowId = 0);
	void notifyParamAutomationOccurred(ParamManager* paramManager, bool updateModLevels = true);
	void displayAutomation();
	void displayOutputName(Output* output, bool doBlink = true, Clip* clip = NULL);
	void instrumentChanged(ModelStackWithTimelineCounter* modelStack, Instrument* newInstrument);
	void navigateThroughPresetsForInstrumentClip(int offset, ModelStackWithTimelineCounter* modelStack,
	                                             bool doBlink = false);
	void navigateThroughAudioOutputsForAudioClip(int offset, AudioClip* clip, bool doBlink = false);
	bool changeInstrumentType(int newInstrumentType, ModelStackWithTimelineCounter* modelStack, bool doBlink = false);
	void drawOutputNameFromDetails(int instrumentType, int slot, int subSlot, char const* name, bool editedByUser,
	                               bool doBlink, Clip* clip = NULL);
	void endMIDILearn();
	void getClipMuteSquareColour(Clip* clip, uint8_t thisColour[]);
	int clipStatusPadAction(Clip* clip, bool on, int yDisplayIfInSessionView = -1);
	void flashPlayEnable();
	void flashPlayDisable();

	// MIDI learn stuff
	uint8_t thingPressedForMidiLearn;
	bool deleteMidiCommandOnRelease;
	bool midiLearnFlashOn;
	bool shouldSaveSettingsAfterMidiLearn;

	int8_t highestMIDIChannelSeenWhileLearning;
	int8_t lowestMIDIChannelSeenWhileLearning;

	LearnedMIDI* learnedThing;
	MelodicInstrument* melodicInstrumentPressedForMIDILearn;
	Drum* drumPressedForMIDILearn;
	Kit* kitPressedForMIDILearn;

	ModelStackWithThreeMainThings activeModControllableModelStack;
	uint8_t dummy[MODEL_STACK_MAX_SIZE - sizeof(ModelStackWithThreeMainThings)];

	bool pendingParamAutomationUpdatesModLevels;

	bool clipArmFlashOn;
	bool blinkOn;

	uint32_t timeSaveButtonPressed;

	int modNoteRowId;
	uint32_t modPos;
	// 0 if not currently editing a region / step / holding a note. If you're gonna refer to this, you absolutely have
	// to first check that the TimelineCounter you're thinking of setting some automation on
	// == activeModControllableTimelineCounter
	uint32_t modLength;

private:
	void pretendModKnobsUntouchedForAWhile();
	void instrumentBeenEdited();
	void clearMelodicInstrumentMonoExpressionIfPossible();
};

extern View view;

#endif /* VIEW_H_ */
