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

#include "gui/views/midi_session_view.h"
#include "definitions_cxx.hpp"
#include "gui/ui/menus.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_instrument_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/print.h"
#include "io/midi/midi_engine.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "util/d_string.h"
#include "util/functions.h"
#include <new>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

using namespace deluge;
using namespace gui;

#define MIDI_DEFAULTS_XML "MIDIFollow.XML"
#define MIDI_DEFAULTS_TAG "defaults"
#define MIDI_DEFAULTS_CC_TAG "defaultCCMappings"

MidiSessionView midiSessionView{};

//initialize variables
MidiSessionView::MidiSessionView() {
	initView();
}

void MidiSessionView::initView() {
	successfullyReadDefaultsFromFile = false;

	initMapping(paramToCC);
	initMapping(previousKnobPos);

	for (int32_t i = 0; i < 128; i++) {
		timeLastCCSent[i] = 0;
	}

	clipForLastNoteReceived = nullptr;

	timeAutomationFeedbackLastSent = 0;
}

void MidiSessionView::initMapping(int32_t mapping[kDisplayWidth][kDisplayHeight]) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			mapping[xDisplay][yDisplay] = kNoSelection;
		}
	}
}

/// checks to see if there is an active clip for the current context
/// cases where there is an active clip:
/// 1) pressing and holding a clip pad in arranger view, song view, grid view
/// 2) pressing and holding the audition pad of a row in arranger view
/// 3) entering a clip
Clip* MidiSessionView::getClipForMidiFollow(bool useActiveClip) {
	Clip* clip = nullptr;
	if (getRootUI() == &sessionView) {
		clip = sessionView.getClipForLayout();
	}
	else if ((getRootUI() == &arrangerView)) {
		if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW) && arrangerView.lastInteractedClipInstance) {
			clip = arrangerView.lastInteractedClipInstance->clip;
		}
		else if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
			Output* output = arrangerView.outputsOnScreen[arrangerView.yPressedEffective];
			clip = currentSong->getClipWithOutput(output);
		}
	}
	else {
		clip = currentSong->currentClip;
	}
	//special case for instruments where you want to let notes and MPE through to the active clip
	if (!clip && useActiveClip) {
		clip = currentSong->currentClip;
	}
	return clip;
}

/// based on the current context, as determined by clip returned from the getClipForMidiFollow function
/// obtain the modelStackWithParam for that context and return it so it can be used by midi follow
ModelStackWithAutoParam*
MidiSessionView::getModelStackWithParam(ModelStackWithThreeMainThings* modelStackWithThreeMainThings,
                                        ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                        int32_t xDisplay, int32_t yDisplay, int32_t ccNumber, bool displayError) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	Param::Kind paramKind = Param::Kind::NONE;
	int32_t paramID = kNoParamID;

	bool isUISessionView =
	    (getRootUI() == &performanceSessionView) || (getRootUI() == &sessionView) || (getRootUI() == &arrangerView);

	if (!clip && isUISessionView) {
		if (modelStackWithThreeMainThings) {
			if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramKind = Param::Kind::UNPATCHED_SOUND;
				paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
			}
			else if (globalEffectableParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramKind = Param::Kind::UNPATCHED_GLOBAL;
				paramID = globalEffectableParamShortcuts[xDisplay][yDisplay];
			}
			if (paramID != kNoParamID) {
				modelStackWithParam =
				    performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings, paramID);
			}
		}
	}
	else {
		if (modelStackWithTimelineCounter) {
			InstrumentClip* instrumentClip = (InstrumentClip*)clip;
			Instrument* instrument = (Instrument*)clip->output;

			if (instrument->type == InstrumentType::SYNTH) {
				if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					paramKind = Param::Kind::PATCHED;
					paramID = patchedParamShortcuts[xDisplay][yDisplay];
				}
				else if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					paramKind = Param::Kind::UNPATCHED_SOUND;
					paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
				}
			}
			else if (instrument->type == InstrumentType::KIT) {
				if (!instrumentClipView.getAffectEntire()) {
					if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
						paramKind = Param::Kind::PATCHED;
						paramID = patchedParamShortcuts[xDisplay][yDisplay];
					}
					else if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
						paramKind = Param::Kind::UNPATCHED_SOUND;
						paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
					}
				}
				else {
					if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
						paramKind = Param::Kind::UNPATCHED_SOUND;
						paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
					}
					else if (globalEffectableParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
						paramKind = Param::Kind::UNPATCHED_GLOBAL;
						paramID = globalEffectableParamShortcuts[xDisplay][yDisplay];
					}
				}
			}
			else if (instrument->type == InstrumentType::AUDIO) {
				if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					paramKind = Param::Kind::UNPATCHED_SOUND;
					paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
				}
				else if (globalEffectableParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					paramKind = Param::Kind::UNPATCHED_GLOBAL;
					paramID = globalEffectableParamShortcuts[xDisplay][yDisplay];
				}
			}
			if ((paramKind != Param::Kind::NONE) && (paramID != kNoParamID)) {
				modelStackWithParam = automationInstrumentClipView.getModelStackWithParam(
				    modelStackWithTimelineCounter, instrumentClip, paramID, paramKind);
			}
		}
	}

	if (displayError && (paramToCC[xDisplay][yDisplay] == ccNumber)
	    && (!modelStackWithParam || !modelStackWithParam->autoParam)) {
		if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			paramKind = Param::Kind::PATCHED;
			paramID = patchedParamShortcuts[xDisplay][yDisplay];
		}
		else if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			paramKind = Param::Kind::UNPATCHED_SOUND;
			paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
		}
		else if (globalEffectableParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			paramKind = Param::Kind::UNPATCHED_GLOBAL;
			paramID = globalEffectableParamShortcuts[xDisplay][yDisplay];
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

	return modelStackWithParam;
}

/// a parameter can be learned t one cc at a time
/// for a given parameter, find and return the cc that has been learned (if any)
/// it does this by finding the grid shortcut that corresponds to that param
/// and then returns what cc or no cc (255) has been mapped to that param shortcut
int32_t MidiSessionView::getCCFromParam(Param::Kind paramKind, int32_t paramID) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			bool foundParamShortcut =
			    (((paramKind == Param::Kind::PATCHED) && (patchedParamShortcuts[xDisplay][yDisplay] == paramID))
			     || ((paramKind == Param::Kind::UNPATCHED_SOUND)
			         && (unpatchedParamShortcuts[xDisplay][yDisplay] == paramID))
			     || ((paramKind == Param::Kind::UNPATCHED_GLOBAL)
			         && (globalEffectableParamShortcuts[xDisplay][yDisplay] == paramID)));

			if (foundParamShortcut) {
				return paramToCC[xDisplay][yDisplay];
			}
		}
	}
	return kNoSelection;
}

/// called from playback handler
/// determines whether a note message received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiSessionView::noteMessageReceived(MIDIDevice* fromDevice, bool on, int32_t channel, int32_t note,
                                          int32_t velocity, bool* doingMidiThru, bool shouldRecordNotesNowNow,
                                          ModelStack* modelStack) {
	// midi follow mode
	if (midiEngine.midiFollow) {
		Clip* clip = getClipForMidiFollow(true);
		if (!on && !clip) {
			// for note off's when you're no longer in an active clip,
			// see if a note on message was previously sent so you can
			// direct the note off to the right place
			clip = clipForLastNoteReceived;
		}

		// Only send if not muted - but let note-offs through always, for safety
		if (clip && (!on || currentSong->isOutputActiveInArrangement(clip->output))) {
			if (clip->output->type != InstrumentType::MIDI_OUT) {
				ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
				//Output is a kit or melodic instrument
				//Note: Midi instruments not currently supported for midi follow mode
				if (modelStackWithTimelineCounter) {
					clip->output->offerReceivedNote(
					    modelStackWithTimelineCounter, fromDevice, on, channel, note, velocity,
					    shouldRecordNotesNowNow
					        && currentSong->isOutputActiveInArrangement(
					            clip->output), // Definitely don't record if muted in arrangement
					    doingMidiThru, true);
					if (on) {
						clipForLastNoteReceived = clip;
					}
				}
			}
		}
	}
}

/// called from playback handler
/// determines whether a midi cc received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiSessionView::midiCCReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
                                     bool* doingMidiThru, bool isMPE, ModelStack* modelStack) {
	// midi follow mode
	if (midiEngine.midiFollow) {
		//obtain clip for active context (for params that's only for the active mod controllable stack)
		Clip* clip = getClipForMidiFollow();
		if (!isMPE && view.activeModControllableModelStack.modControllable) {
			MIDIMatchType match =
			    midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::PARAM)].checkMatch(
			        fromDevice, channel);
			if (match) {
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
					    ->offerReceivedCCToMidiFollow(modelStack, clip, ccNumber, value);
				}
			}
		}
		//for these cc's, check if there's an active clip if the clip returned above is NULL
		if (!clip) {
			clip = currentSong->currentClip;
		}
		if (clip) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			if (modelStackWithTimelineCounter) {
				clip->output->offerReceivedCC(modelStackWithTimelineCounter, fromDevice, channel, ccNumber, value,
				                              doingMidiThru, true);
			}
		}
	}
}

/// called from playback handler
/// determines whether a pitch bend received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiSessionView::pitchBendReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
                                        bool* doingMidiThru, ModelStack* modelStack) {
	// midi follow mode
	if (midiEngine.midiFollow) {
		//obtain clip for active context
		Clip* clip = getClipForMidiFollow(true);
		if (clip) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			if (modelStackWithTimelineCounter) {
				clip->output->offerReceivedPitchBend(modelStackWithTimelineCounter, fromDevice, channel, data1, data2,
				                                     doingMidiThru, true);
			}
		}
	}
}

/// called from playback handler
/// determines whether aftertouch received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiSessionView::aftertouchReceived(MIDIDevice* fromDevice, int32_t channel, int32_t value, int32_t noteCode,
                                         bool* doingMidiThru, ModelStack* modelStack) {
	// midi follow mode
	if (midiEngine.midiFollow) {
		//obtain clip for active context
		Clip* clip = getClipForMidiFollow(true);
		if (clip) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			if (modelStackWithTimelineCounter) {
				clip->output->offerReceivedAftertouch(modelStackWithTimelineCounter, fromDevice, channel, value,
				                                      noteCode, doingMidiThru, true);
			}
		}
	}
}

/// called from song.cpp
/// determines whether bend range update received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiSessionView::bendRangeUpdateReceived(ModelStack* modelStack, MIDIDevice* device, int32_t channelOrZone,
                                              int32_t whichBendRange, int32_t bendSemitones) {
	// midi follow mode
	if (midiEngine.midiFollow) {
		//obtain clip for active context
		Clip* clip = getClipForMidiFollow(true);
		if (clip) {
			clip->output->offerBendRangeUpdate(modelStack, device, channelOrZone, whichBendRange, bendSemitones, true);
		}
	}
}

/// read defaults from XML
void MidiSessionView::readDefaultsFromFile() {
	//no need to keep reading from SD card after first load
	if (successfullyReadDefaultsFromFile) {
		return;
	}
	else {
		initView();
	}

	FilePointer fp;
	//MIDIFollow.XML
	bool success = storageManager.fileExists(MIDI_DEFAULTS_XML, &fp);
	if (!success) {
		return;
	}

	//<defaults>
	int32_t error = storageManager.openXMLFile(&fp, MIDI_DEFAULTS_TAG);
	if (error) {
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
void MidiSessionView::readDefaultMappingsFromFile() {
	char const* paramName;
	char const* tagName;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				if (!strcmp(tagName, ((Sound*)NULL)->Sound::paramToString(patchedParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName, ((Sound*)NULL)
				                              ->Sound::paramToString(Param::Unpatched::START
				                                                     + unpatchedParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName, ModControllableAudio::paramToString(
				                              Param::Unpatched::START + unpatchedParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName,
				                 GlobalEffectable::paramToString(
				                     Param::Unpatched::START + globalEffectableParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
			}
		}
		storageManager.exitTag();
	}
}

