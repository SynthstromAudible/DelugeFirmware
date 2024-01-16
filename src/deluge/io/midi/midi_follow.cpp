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

#include "io/midi/midi_follow.h"
#include "definitions_cxx.hpp"
#include "gui/ui/menus.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_instrument_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "io/debug/print.h"
#include "io/midi/midi_engine.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/song/song.h"
#include "util/cfunctions.h"
#include "util/d_string.h"
#include "util/functions.h"
#include <new>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

using namespace deluge;
using namespace gui;

#define MIDI_DEFAULTS_XML "MIDIFollow.XML"
#define MIDI_DEFAULTS_TAG "defaults"
#define MIDI_DEFAULTS_CC_TAG "defaultCCMappings"

/// default/standard MIDI CC to Param mappings for MIDI Follow
/// if you do not customize MIDIFollow.XML, CC's will be mapped to Parameters as per the following
const int32_t defaultParamToCCMapping[kDisplayWidth][kDisplayHeight] = {
    {MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE},
    {MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE},
    {21, 12, MIDI_CC_NONE, 23, MIDI_CC_NONE, 24, 25, 41},
    {26, 13, MIDI_CC_NONE, 28, MIDI_CC_NONE, 29, 30, MIDI_CC_NONE},
    {54, 14, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, 55, MIDI_CC_NONE, MIDI_CC_NONE},
    {56, 15, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, 57, MIDI_CC_NONE, MIDI_CC_NONE},
    {7, 3, MIDI_CC_NONE, 10, MIDI_CC_NONE, 63, 62, MIDI_CC_NONE},
    {5, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, 19},
    {72, 76, 75, 73, 70, MIDI_CC_NONE, 71, 74},
    {80, 79, 78, 77, 83, MIDI_CC_NONE, 82, 81},
    {MIDI_CC_NONE, MIDI_CC_NONE, 61, MIDI_CC_NONE, 60, MIDI_CC_NONE, 86, 84},
    {51, MIDI_CC_NONE, 50, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, 87, 85},
    {58, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, 18, 17, 93, 16},
    {59, MIDI_CC_NONE, MIDI_CC_NONE, 91, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE},
    {53, MIDI_CC_NONE, MIDI_CC_NONE, 52, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE},
    {MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE, MIDI_CC_NONE}};

MidiFollow midiFollow{};

//initialize variables
MidiFollow::MidiFollow() {
	init();
}

void MidiFollow::init() {
	successfullyReadDefaultsFromFile = false;

	initMapping(paramToCC);

	for (int32_t i = 0; i < (kMaxMIDIValue + 1); i++) {
		timeLastCCSent[i] = 0;
		previousKnobPos[i] = kNoSelection;
	}

	timeAutomationFeedbackLastSent = 0;
}

void MidiFollow::initMapping(int32_t mapping[kDisplayWidth][kDisplayHeight]) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			mapping[xDisplay][yDisplay] = defaultParamToCCMapping[xDisplay][yDisplay];
		}
	}
}

/// checks to see if there is an active clip for the current context
/// cases where there is an active clip:
/// 1) pressing and holding a clip pad in arranger view, song view, grid view
/// 2) pressing and holding the audition pad of a row in arranger view
/// 3) entering a clip
Clip* getSelectedClip(bool useActiveClip) {
	Clip* clip = nullptr;
	RootUI* rootUI = getRootUI();

	//if you're in session view, check if you're pressing a clip to control that clip
	if (rootUI == &sessionView) {
		clip = sessionView.getClipForLayout();
	}
	//if you're in arranger view, check if you're pressing a clip or holding audition pad to control that clip
	else if (rootUI == &arrangerView) {
		if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW) && arrangerView.lastInteractedClipInstance) {
			clip = arrangerView.lastInteractedClipInstance->clip;
		}
		else if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
			Output* output = arrangerView.outputsOnScreen[arrangerView.yPressedEffective];
			clip = currentSong->getClipWithOutput(output);
		}
	}
	//if you're in performance view, no clip will be selected for param control
	//if you're not in sessionView, arrangerView, or performanceView, then you're in a clip
	else if (rootUI != &performanceSessionView) {
		clip = getCurrentClip();
	}
	//special case for instruments where you want to let notes and MPE through to the active clip
	if (!clip && useActiveClip) {
		clip = getCurrentClip();
	}
	return clip;
}

/// based on the current context, as determined by clip returned from the getSelectedClip function
/// obtain the modelStackWithParam for that context and return it so it can be used by midi follow
ModelStackWithAutoParam*
MidiFollow::getModelStackWithParam(ModelStackWithThreeMainThings* modelStackWithThreeMainThings,
                                   ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                   int32_t xDisplay, int32_t yDisplay, int32_t ccNumber, bool displayError) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	bool isUISessionView =
	    (getRootUI() == &performanceSessionView) || (getRootUI() == &sessionView) || (getRootUI() == &arrangerView);

	if (!clip && isUISessionView) {
		if (modelStackWithThreeMainThings) {
			modelStackWithParam = getModelStackWithParamWithoutClip(modelStackWithThreeMainThings, xDisplay, yDisplay);
		}
	}
	else {
		if (modelStackWithTimelineCounter) {
			modelStackWithParam =
			    getModelStackWithParamWithClip(modelStackWithTimelineCounter, clip, xDisplay, yDisplay);
		}
	}

	if (displayError && (paramToCC[xDisplay][yDisplay] == ccNumber)
	    && (!modelStackWithParam || !modelStackWithParam->autoParam)) {
		displayParamControlError(xDisplay, yDisplay);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamWithoutClip(ModelStackWithThreeMainThings* modelStackWithThreeMainThings,
                                              int32_t xDisplay, int32_t yDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	int32_t paramID = kNoParamID;

	if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
	}
	else if (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];
	}
	if (paramID != kNoParamID) {
		modelStackWithParam = performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings, paramID);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamWithClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                           int32_t xDisplay, int32_t yDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	Instrument* instrument = (Instrument*)clip->output;

	if (instrument->type == OutputType::SYNTH) {
		modelStackWithParam = getModelStackWithParamForSynthClip(modelStackWithTimelineCounter, (InstrumentClip*)clip,
		                                                         xDisplay, yDisplay);
	}
	else if (instrument->type == OutputType::KIT) {
		modelStackWithParam =
		    getModelStackWithParamForKitClip(modelStackWithTimelineCounter, (InstrumentClip*)clip, xDisplay, yDisplay);
	}
	else if (instrument->type == OutputType::AUDIO) {
		modelStackWithParam =
		    getModelStackWithParamForAudioClip(modelStackWithTimelineCounter, (AudioClip*)clip, xDisplay, yDisplay);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForSynthClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                               InstrumentClip* clip, int32_t xDisplay, int32_t yDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	Param::Kind paramKind = Param::Kind::NONE;
	int32_t paramID = kNoParamID;

	if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = Param::Kind::PATCHED;
		paramID = patchedParamShortcuts[xDisplay][yDisplay];
	}
	else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = Param::Kind::UNPATCHED_SOUND;
		paramID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
	}
	if ((paramKind != Param::Kind::NONE) && (paramID != kNoParamID)) {
		modelStackWithParam = automationInstrumentClipView.getModelStackWithParamForSynthClip(
		    modelStackWithTimelineCounter, clip, paramID, paramKind);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForKitClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                             InstrumentClip* clip, int32_t xDisplay, int32_t yDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	Param::Kind paramKind = Param::Kind::NONE;
	int32_t paramID = kNoParamID;

	if (!instrumentClipView.getAffectEntire()) {
		if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			paramKind = Param::Kind::PATCHED;
			paramID = patchedParamShortcuts[xDisplay][yDisplay];
		}
		else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			//don't allow control of Portamento in Kit's
			if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != Param::Unpatched::Sound::PORTAMENTO) {
				paramKind = Param::Kind::UNPATCHED_SOUND;
				paramID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
			}
		}
	}
	else {
		if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			//don't allow control of Portamento or Arp Gate in Kit Affect Entire
			if ((unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != Param::Unpatched::Sound::PORTAMENTO)
			    && (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != Param::Unpatched::Sound::ARP_GATE)) {
				paramKind = Param::Kind::UNPATCHED_SOUND;
				paramID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
			}
		}
		else if (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			paramKind = Param::Kind::UNPATCHED_GLOBAL;
			paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];
		}
	}
	if ((paramKind != Param::Kind::NONE) && (paramID != kNoParamID)) {
		modelStackWithParam = automationInstrumentClipView.getModelStackWithParamForKitClip(
		    modelStackWithTimelineCounter, clip, paramID, paramKind);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForAudioClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                               AudioClip* clip, int32_t xDisplay, int32_t yDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	Param::Kind paramKind = Param::Kind::NONE;
	int32_t paramID = kNoParamID;

	if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = Param::Kind::UNPATCHED_SOUND;
		paramID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
	}
	else if (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = Param::Kind::UNPATCHED_GLOBAL;
		paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];
	}
	if ((paramKind != Param::Kind::NONE) && (paramID != kNoParamID)) {
		modelStackWithParam = automationInstrumentClipView.getModelStackWithParamForAudioClip(
		    modelStackWithTimelineCounter, clip, paramID, paramKind);
	}
	if ((paramKind != Param::Kind::NONE) && (paramID != kNoParamID)) {
		modelStackWithParam = automationInstrumentClipView.getModelStackWithParam(modelStackWithTimelineCounter,
		                                                                          instrumentClip, paramID, paramKind);
	}

	return modelStackWithParam;
}

void MidiFollow::displayParamControlError(int32_t xDisplay, int32_t yDisplay) {
	Param::Kind paramKind = Param::Kind::NONE;
	int32_t paramID = kNoParamID;

	if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = Param::Kind::PATCHED;
		paramID = patchedParamShortcuts[xDisplay][yDisplay];
	}
	else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = Param::Kind::UNPATCHED_SOUND;
		paramID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
	}
	else if (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = Param::Kind::UNPATCHED_GLOBAL;
		paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];
	}

	if (display->haveOLED()) {
		DEF_STACK_STRING_BUF(popupMsg, 40);

		const char* name = getParamDisplayName(paramKind, paramID);
		if (name != l10n::get(l10n::String::STRING_FOR_NONE)) {
			popupMsg.append("Can't control: \n");
			popupMsg.append(name);
		}
		display->displayPopup(popupMsg.c_str());
	}
	else {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
	}
}

/// a parameter can be learned t one cc at a time
/// for a given parameter, find and return the cc that has been learned (if any)
/// it does this by finding the grid shortcut that corresponds to that param
/// and then returns what cc or no cc (255) has been mapped to that param shortcut
int32_t MidiFollow::getCCFromParam(Param::Kind paramKind, int32_t paramID) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			bool foundParamShortcut =
			    (((paramKind == Param::Kind::PATCHED) && (patchedParamShortcuts[xDisplay][yDisplay] == paramID))
			     || ((paramKind == Param::Kind::UNPATCHED_SOUND)
			         && (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == paramID))
			     || ((paramKind == Param::Kind::UNPATCHED_GLOBAL)
			         && (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] == paramID)));

			if (foundParamShortcut) {
				return paramToCC[xDisplay][yDisplay];
			}
		}
	}
	return MIDI_CC_NONE;
}

/// used to store the clip's for each note received so that note off's can be sent to the right clip
PLACE_SDRAM_DATA Clip* clipForLastNoteReceived[kMaxMIDIValue + 1] = {0};

/// called from playback handler
/// determines whether a note message received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiFollow::noteMessageReceived(MIDIDevice* fromDevice, bool on, int32_t channel, int32_t note, int32_t velocity,
                                     bool* doingMidiThru, bool shouldRecordNotesNowNow, ModelStack* modelStack) {
	MIDIMatchType match = checkMidiFollowMatch(fromDevice, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		if (note >= 0 && note <= 127) {
			Clip* clip;
			if (on) {
				clip = getSelectedClip(true);
			}
			else {
				// for note off's, see if a note on message was previously sent so you can
				// direct the note off to the right place
				clip = clipForLastNoteReceived[note];
			}

			sendNoteToClip(fromDevice, clip, match, on, channel, note, velocity, doingMidiThru, shouldRecordNotesNowNow,
			               modelStack);
		}
		//all notes off
		else if (note == ALL_NOTES_OFF) {
			for (int32_t i = 0; i <= 127; i++) {
				if (clipForLastNoteReceived[i]) {
					sendNoteToClip(fromDevice, clipForLastNoteReceived[i], match, on, channel, i, velocity,
					               doingMidiThru, shouldRecordNotesNowNow, modelStack);
				}
			}
		}
	}
}

void MidiFollow::sendNoteToClip(MIDIDevice* fromDevice, Clip* clip, MIDIMatchType match, bool on, int32_t channel,
                                int32_t note, int32_t velocity, bool* doingMidiThru, bool shouldRecordNotesNowNow,
                                ModelStack* modelStack) {

	// Only send if not muted - but let note-offs through always, for safety
	if (clip && (clip->output->type != OutputType::AUDIO)
	    && (!on || currentSong->isOutputActiveInArrangement(clip->output))) {
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
		//Output is a kit or melodic instrument
		if (modelStackWithTimelineCounter) {
			// Definitely don't record if muted in arrangement
			bool shouldRecordNotes = shouldRecordNotesNowNow && currentSong->isOutputActiveInArrangement(clip->output);
			if (clip->output->type == OutputType::KIT) {
				offerReceivedNoteToKit(modelStackWithTimelineCounter, fromDevice, on, channel, note, velocity,
				                       shouldRecordNotes, doingMidiThru, clip);
			}
			else {
				offerReceivedNoteToMelodicInstrument(modelStackWithTimelineCounter, fromDevice, match, on, channel,
				                                     note, velocity, shouldRecordNotes, doingMidiThru, clip);
			}
			if (on) {
				clipForLastNoteReceived[note] = clip;
			}
			else {
				if (note >= 0 && note <= 127) {
					clipForLastNoteReceived[note] = nullptr;
				}
			}
		}
	}
}

void MidiFollow::offerReceivedNoteToKit(ModelStackWithTimelineCounter* modelStack, MIDIDevice* fromDevice, bool on,
                                        int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
                                        bool* doingMidiThru, Clip* clip) {
	Kit* kit = (Kit*)clip->output;
	Drum* thisDrum = getDrumFromNoteCode(kit, note);

	kit->receivedNoteForDrum(modelStack, fromDevice, on, channel, note, velocity, shouldRecordNotes, doingMidiThru,
	                         thisDrum);
}

void MidiFollow::offerReceivedNoteToMelodicInstrument(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                                      MIDIDevice* fromDevice, MIDIMatchType match, bool on,
                                                      int32_t channel, int32_t note, int32_t velocity,
                                                      bool shouldRecordNotes, bool* doingMidiThru, Clip* clip) {
	MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
	melodicInstrument->receivedNote(modelStackWithTimelineCounter, fromDevice, on, channel, match, note, velocity,
	                                shouldRecordNotes, doingMidiThru);
}

/// called from playback handler
/// determines whether a midi cc received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiFollow::midiCCReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
                                bool* doingMidiThru, ModelStack* modelStack) {
	MIDIMatchType match = checkMidiFollowMatch(fromDevice, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		//obtain clip for active context (for params that's only for the active mod controllable stack)
		Clip* clip = getSelectedClip();
		//clip is allowed to be null here because there may not be an active clip
		//e.g. you want to control the song level parameters
		if (view.activeModControllableModelStack.modControllable
		    && (match == MIDIMatchType::MPE_MASTER || match == MIDIMatchType::CHANNEL)) {
			//if midi follow feedback and feedback filter is enabled,
			//check time elapsed since last midi cc was sent with midi feedback for this same ccNumber
			//if it was greater or equal than 1 second ago, allow received midi cc to go through
			//this helps avoid additional processing of midi cc's receiver
			if (!midiEngine.midiFollowFeedback
			    || (midiEngine.midiFollowFeedback
			        && (!midiEngine.midiFollowFeedbackFilter
			            || (midiEngine.midiFollowFeedbackFilter
			                && ((AudioEngine::audioSampleTimer - timeLastCCSent[ccNumber]) >= kSampleRate))))) {
				// See if it's learned to a parameter
				((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
				    ->receivedCCFromMidiFollow(modelStack, clip, ccNumber, value);
			}
		}
		//for these cc's, check if there's an active clip if the clip returned above is NULL
		if (!clip) {
			clip = currentSong->currentClip;
		}
		if (clip && (clip->output->type != OutputType::AUDIO)) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
			if (modelStackWithTimelineCounter) {
				if (clip->output->type == OutputType::KIT) {
					offerReceivedCCToKit(modelStackWithTimelineCounter, fromDevice, match, channel, ccNumber, value,
					                     doingMidiThru, clip);
				}
				else {
					offerReceivedCCToMelodicInstrument(modelStackWithTimelineCounter, fromDevice, match, channel,
					                                   ccNumber, value, doingMidiThru, clip);
				}
			}
		}
	}
}

void MidiFollow::offerReceivedCCToKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                      MIDIDevice* fromDevice, MIDIMatchType match, uint8_t channel, uint8_t ccNumber,
                                      uint8_t value, bool* doingMidiThru, Clip* clip) {
	if (match != MIDIMatchType::MPE_MASTER && match != MIDIMatchType::MPE_MEMBER) {
		return;
	}
	if (ccNumber != 74) {
		return;
	}
	if (!fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isChannelPartOfAnMPEZone(channel)) {
		return;
	}

	Kit* kit = (Kit*)clip->output;
	Drum* firstDrum = kit->getDrumFromIndex(0);

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		kit->receivedMPEYForDrum(modelStackWithTimelineCounter, thisDrum, match, channel, value);
	}
}

void MidiFollow::offerReceivedCCToMelodicInstrument(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                                    MIDIDevice* fromDevice, MIDIMatchType match, uint8_t channel,
                                                    uint8_t ccNumber, uint8_t value, bool* doingMidiThru, Clip* clip) {
	MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
	melodicInstrument->receivedCC(modelStackWithTimelineCounter, fromDevice, match, channel, ccNumber, value,
	                              doingMidiThru);
}

/// called from playback handler
/// determines whether a pitch bend received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiFollow::pitchBendReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
                                   bool* doingMidiThru, ModelStack* modelStack) {
	MIDIMatchType match = checkMidiFollowMatch(fromDevice, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		//obtain clip for active context
		Clip* clip = getSelectedClip(true);
		if (clip && (clip->output->type != OutputType::AUDIO)) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			if (modelStackWithTimelineCounter) {
				if (clip->output->type == OutputType::KIT) {
					offerReceivedPitchBendToKit(modelStackWithTimelineCounter, fromDevice, match, channel, data1, data2,
					                            doingMidiThru, clip);
				}
				else {
					offerReceivedPitchBendToMelodicInstrument(modelStackWithTimelineCounter, fromDevice, match, channel,
					                                          data1, data2, doingMidiThru, clip);
				}
			}
		}
	}
}

void MidiFollow::offerReceivedPitchBendToKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                             MIDIDevice* fromDevice, MIDIMatchType match, uint8_t channel,
                                             uint8_t data1, uint8_t data2, bool* doingMidiThru, Clip* clip) {
	Kit* kit = (Kit*)clip->output;
	Drum* firstDrum = kit->getDrumFromIndex(0);

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		kit->receivedPitchBendForDrum(modelStackWithTimelineCounter, thisDrum, data1, data2, match, channel,
		                              doingMidiThru);
	}
}

void MidiFollow::offerReceivedPitchBendToMelodicInstrument(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                                           MIDIDevice* fromDevice, MIDIMatchType match, uint8_t channel,
                                                           uint8_t data1, uint8_t data2, bool* doingMidiThru,
                                                           Clip* clip) {
	MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
	melodicInstrument->receivedPitchBend(modelStackWithTimelineCounter, fromDevice, match, channel, data1, data2,
	                                     doingMidiThru);
}

/// called from playback handler
/// determines whether aftertouch received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiFollow::aftertouchReceived(MIDIDevice* fromDevice, int32_t channel, int32_t value, int32_t noteCode,
                                    bool* doingMidiThru, ModelStack* modelStack) {
	MIDIMatchType match = checkMidiFollowMatch(fromDevice, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		//obtain clip for active context
		Clip* clip = getSelectedClip(true);
		if (clip && (clip->output->type != OutputType::AUDIO)) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			if (modelStackWithTimelineCounter) {
				if (clip->output->type == OutputType::KIT) {
					offerReceivedAftertouchToKit(modelStackWithTimelineCounter, fromDevice, match, channel, value,
					                             noteCode, doingMidiThru, clip);
				}
				else {
					offerReceivedAftertouchToMelodicInstrument(modelStackWithTimelineCounter, fromDevice, match,
					                                           channel, value, noteCode, doingMidiThru, clip);
				}
			}
		}
	}
}

void MidiFollow::offerReceivedAftertouchToKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                              MIDIDevice* fromDevice, MIDIMatchType match, int32_t channel,
                                              int32_t value, int32_t noteCode, bool* doingMidiThru, Clip* clip) {
	Kit* kit = (Kit*)clip->output;
	// Channel pressure message...
	if (noteCode == -1) {
		Drum* firstDrum = kit->getDrumFromIndex(0);
		for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
			int32_t level = BEND_RANGE_FINGER_LEVEL;
			kit->receivedAftertouchForDrum(modelStackWithTimelineCounter, thisDrum, match, channel, value);
		}
	}
	// Or a polyphonic aftertouch message - these aren't allowed for MPE except on the "master" channel.
	else {
		Drum* thisDrum = getDrumFromNoteCode(kit, noteCode);
		if ((thisDrum != nullptr) && (channel == thisDrum->lastMIDIChannelAuditioned)) {
			kit->receivedAftertouchForDrum(modelStackWithTimelineCounter, thisDrum, MIDIMatchType::CHANNEL, channel,
			                               value);
		}
	}
}

void MidiFollow::offerReceivedAftertouchToMelodicInstrument(
    ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice, MIDIMatchType match,
    int32_t channel, int32_t value, int32_t noteCode, bool* doingMidiThru, Clip* clip) {

	MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
	melodicInstrument->receivedAftertouch(modelStackWithTimelineCounter, fromDevice, match, channel, value, noteCode,
	                                      doingMidiThru);
}

/// obtain match to check if device is compatible with the midi follow channel
/// a valid match is passed through to the instruments for further evaluation
MIDIMatchType MidiFollow::checkMidiFollowMatch(MIDIDevice* fromDevice, uint8_t channel) {
	MIDIMatchType m = MIDIMatchType::NO_MATCH;
	for (auto& midiChannelType : midiEngine.midiFollowChannelType) {
		m = midiChannelType.checkMatch(fromDevice, channel);
		if (m != MIDIMatchType::NO_MATCH) {
			return m;
		}
	}
	return m;
}

/// based on the midi follow root kit note and note received
/// it calculates what the drum note row index should be
/// and then attempts to get a valid drum from the index
/// nullptr is returned if no drum is found
Drum* MidiFollow::getDrumFromNoteCode(Kit* kit, int32_t noteCode) {
	Drum* thisDrum = nullptr;
	//bottom kit noteRowId = 0
	//default middle C1 note number = 36
	//noteRowId + 36 = C1 up for kit sounds
	//this is configurable through the default menu
	if (noteCode >= midiEngine.midiFollowKitRootNote) {
		int32_t index = noteCode - midiEngine.midiFollowKitRootNote;
		thisDrum = kit->getDrumFromIndexAllowNull(index);
	}
	return thisDrum;
}

/// create default XML file and write defaults
/// I should check if file exists before creating one
void MidiFollow::writeDefaultsToFile() {
	//MidiFollow.xml
	int32_t error = storageManager.createXMLFile(MIDI_DEFAULTS_XML, true);
	if (error) {
		return;
	}

	//<defaults>
	storageManager.writeOpeningTagBeginning(MIDI_DEFAULTS_TAG);
	storageManager.writeOpeningTagEnd();

	//<defaultCCMappings>
	storageManager.writeOpeningTagBeginning(MIDI_DEFAULTS_CC_TAG);
	storageManager.writeOpeningTagEnd();

	writeDefaultMappingsToFile();

	storageManager.writeClosingTag(MIDI_DEFAULTS_CC_TAG);

	storageManager.writeClosingTag(MIDI_DEFAULTS_TAG);

	storageManager.closeFileAfterWriting();
}

/// convert paramID to a paramName to write to XML
void MidiFollow::writeDefaultMappingsToFile() {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			bool writeTag = false;
			char const* paramName;

			if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramName = ((Sound*)NULL)->Sound::paramToString(patchedParamShortcuts[xDisplay][yDisplay]);
				writeTag = true;
			}
			else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				if ((unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == Param::Unpatched::Sound::ARP_GATE)
				    || (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == Param::Unpatched::Sound::PORTAMENTO)) {
					paramName = ((Sound*)NULL)
					                ->Sound::paramToString(Param::Unpatched::START
					                                       + unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay]);
				}
				else {
					paramName = ModControllableAudio::paramToString(
					    Param::Unpatched::START + unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay]);
				}
				writeTag = true;
			}
			else if (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramName = GlobalEffectable::paramToString(Param::Unpatched::START
				                                            + unpatchedGlobalParamShortcuts[xDisplay][yDisplay]);
				writeTag = true;
			}

			if (writeTag) {
				char buffer[10];
				intToString(paramToCC[xDisplay][yDisplay], buffer);
				storageManager.writeTag(paramName, buffer);
			}
		}
	}
}

/// read defaults from XML
void MidiFollow::readDefaultsFromFile() {
	//no need to keep reading from SD card after first load
	if (successfullyReadDefaultsFromFile) {
		return;
	}
	else {
		init();
	}

	FilePointer fp;
	//MIDIFollow.XML
	bool success = storageManager.fileExists(MIDI_DEFAULTS_XML, &fp);
	if (!success) {
		writeDefaultsToFile();
		successfullyReadDefaultsFromFile = true;
		return;
	}

	//<defaults>
	int32_t error = storageManager.openXMLFile(&fp, MIDI_DEFAULTS_TAG);
	if (error) {
		writeDefaultsToFile();
		successfullyReadDefaultsFromFile = true;
		return;
	}

	char const* tagName;
	//step into the <defaultCCMappings> tag
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, MIDI_DEFAULTS_CC_TAG)) {
			readDefaultMappingsFromFile();
		}
		storageManager.exitTag();
	}

	storageManager.closeFile();

	successfullyReadDefaultsFromFile = true;
}

/// compares param name tag to the list of params available are midi controllable
/// if param is found, it loads the CC mapping info for that param into the view
void MidiFollow::readDefaultMappingsFromFile() {
	char const* paramName;
	char const* tagName;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				if (!strcmp(tagName, ((Sound*)NULL)->Sound::paramToString(patchedParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName,
				                 ((Sound*)NULL)
				                     ->Sound::paramToString(Param::Unpatched::START
				                                            + unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName,
				                 ModControllableAudio::paramToString(
				                     Param::Unpatched::START + unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName,
				                 GlobalEffectable::paramToString(
				                     Param::Unpatched::START + unpatchedGlobalParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
			}
		}
		storageManager.exitTag();
	}
}
