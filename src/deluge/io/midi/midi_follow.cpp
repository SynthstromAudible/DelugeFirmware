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
#include "gui/l10n/l10n.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_takeover.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"

namespace params = deluge::modulation::params;
using deluge::modulation::params::kNoParamID;
using deluge::modulation::params::patchedParamShortcuts;
using deluge::modulation::params::unpatchedGlobalParamShortcuts;
using deluge::modulation::params::unpatchedNonGlobalParamShortcuts;

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

// initialize variables
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
/// 1) pressing and holding a clip pad in arranger view, song row view, song grid view
/// 2) pressing and holding the audition pad of a row in arranger view and in
///	   arranger performance view, arranger automation view
/// 3) entering a clip or previousy held a clip
Clip* MidiFollow::getSelectedOrActiveClip() {
	Clip* clip = nullptr;

	clip = getSelectedClip();

	// if the clip is null, it means you're in a song view and you aren't holding a clip
	// in that case, we want to control the active clip
	if (!clip) {
		clip = getCurrentClip();
		if (clip) {
			Output* output = clip->output;
			if (output) {
				clip = output->getActiveClip();
			}
		}
	}

	return clip;
}

/// see if you are pressing and holding a clip in arranger view, song row view, song grid view
/// see if you are pressing and holding audition pad in arranger view, arranger perf view, or
/// arranger automation view
/// if you're in a clip, this will return that clip
Clip* MidiFollow::getSelectedClip() {
	Clip* clip = nullptr;

	RootUI* rootUI = getRootUI();
	UIType uiType = UIType::NONE;
	if (rootUI) {
		uiType = rootUI->getUIType();
	}

	switch (uiType) {
	case UIType::SESSION:
		// if you're in session view, check if you're pressing a clip to control that clip
		clip = sessionView.getClipForLayout();
		break;
	case UIType::ARRANGER:
		clip = arrangerView.getClipForSelection();
		break;
	case UIType::PERFORMANCE_SESSION:
		// if you're in the arranger performance view, check if you're holding audition pad
		if (currentSong->lastClipInstanceEnteredStartPos != -1) {
			clip = arrangerView.getClipForSelection();
		}
		break;
	case UIType::AUTOMATION:
		// if you're in the arranger automation view, check if you're holding audition pad
		if (automationView.onArrangerView) {
			clip = arrangerView.getClipForSelection();
			break;
		}
		[[fallthrough]]; // you're in automation clip view
	default:             // if you're in a clip view, then return the current clip
		clip = getCurrentClip();
	}

	return clip;
}

// returns activeClip for the selected output
// special case for note and performance data where you want to let notes,
// midi modulation sources (e.g. mod wheel), and MPE through to the active clip
Clip* MidiFollow::getActiveClip(ModelStack* modelStack) {
	// If you have an output for which no clip is active,
	// when auditioning a clip for that output,
	// the active clip for that output should be set to the current clip.
	Clip* currentClip = getCurrentClip();
	if (currentClip && (currentClip->type == ClipType::INSTRUMENT)) {
		if (currentClip->output) {
			InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(modelStack);
			return currentClip->output->getActiveClip();
		}
	}
	return nullptr;
}

/// based on the current context, as determined by clip returned from the getSelectedClip function
/// obtain the modelStackWithParam for that context and return it so it can be used by midi follow
ModelStackWithAutoParam*
MidiFollow::getModelStackWithParam(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                   int32_t xDisplay, int32_t yDisplay, int32_t ccNumber, bool displayError) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	// non-null clip means you're dealing with the clip context
	if (clip) {
		if (modelStackWithTimelineCounter) {
			modelStackWithParam =
			    getModelStackWithParamForClip(modelStackWithTimelineCounter, clip, xDisplay, yDisplay);
		}
	}

	if (displayError && (paramToCC[xDisplay][yDisplay] == ccNumber)
	    && (!modelStackWithParam || !modelStackWithParam->autoParam)) {
		displayParamControlError(xDisplay, yDisplay);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                          int32_t xDisplay, int32_t yDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	OutputType outputType = clip->output->type;

	switch (outputType) {
	case OutputType::SYNTH:
		modelStackWithParam =
		    getModelStackWithParamForSynthClip(modelStackWithTimelineCounter, clip, xDisplay, yDisplay);
		break;
	case OutputType::KIT:
		modelStackWithParam = getModelStackWithParamForKitClip(modelStackWithTimelineCounter, clip, xDisplay, yDisplay);
		break;
	case OutputType::AUDIO:
		modelStackWithParam =
		    getModelStackWithParamForAudioClip(modelStackWithTimelineCounter, clip, xDisplay, yDisplay);
		break;
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForSynthClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                               int32_t xDisplay, int32_t yDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	params::Kind paramKind = params::Kind::NONE;
	int32_t paramID = kNoParamID;

	if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = params::Kind::PATCHED;
		paramID = patchedParamShortcuts[xDisplay][yDisplay];
	}
	else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = params::Kind::UNPATCHED_SOUND;
		paramID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
	}
	if ((paramKind != params::Kind::NONE) && (paramID != kNoParamID)) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam =
		    clip->output->getModelStackWithParam(modelStackWithTimelineCounter, clip, paramID, paramKind, true, false);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForKitClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                             int32_t xDisplay, int32_t yDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	params::Kind paramKind = params::Kind::NONE;
	int32_t paramID = kNoParamID;
	InstrumentClip* instrumentClip = (InstrumentClip*)clip;

	if (!instrumentClip->affectEntire) {
		if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			paramKind = params::Kind::PATCHED;
			paramID = patchedParamShortcuts[xDisplay][yDisplay];
		}
		else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			// don't allow control of Portamento in Kit's
			if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != params::UNPATCHED_PORTAMENTO) {
				paramKind = params::Kind::UNPATCHED_SOUND;
				paramID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
			}
		}
	}
	else {
		if (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			paramKind = params::Kind::UNPATCHED_GLOBAL;
			paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];
		}
	}

	if ((paramKind != params::Kind::NONE) && (paramID != kNoParamID)) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam = clip->output->getModelStackWithParam(modelStackWithTimelineCounter, clip, paramID,
		                                                           paramKind, instrumentClip->affectEntire, false);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForAudioClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                               int32_t xDisplay, int32_t yDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	params::Kind paramKind = params::Kind::UNPATCHED_GLOBAL;
	int32_t paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];

	if (paramID != kNoParamID) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam =
		    clip->output->getModelStackWithParam(modelStackWithTimelineCounter, clip, paramID, paramKind, true, false);
	}

	return modelStackWithParam;
}

void MidiFollow::displayParamControlError(int32_t xDisplay, int32_t yDisplay) {
	params::Kind paramKind = params::Kind::NONE;
	int32_t paramID = kNoParamID;

	if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = params::Kind::PATCHED;
		paramID = patchedParamShortcuts[xDisplay][yDisplay];
	}
	else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = params::Kind::UNPATCHED_SOUND;
		paramID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
	}
	else if (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = params::Kind::UNPATCHED_GLOBAL;
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
int32_t MidiFollow::getCCFromParam(params::Kind paramKind, int32_t paramID) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			bool foundParamShortcut =
			    (((paramKind == params::Kind::PATCHED) && (patchedParamShortcuts[xDisplay][yDisplay] == paramID))
			     || ((paramKind == params::Kind::UNPATCHED_SOUND)
			         && (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == paramID))
			     || ((paramKind == params::Kind::UNPATCHED_GLOBAL)
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

/// removes a specific clip pointer from the clipForLastNoteReceived array
/// if a clip is deleted, it should be removed from this array to ensure note off's don't get sent
/// to delete clips
void MidiFollow::removeClip(Clip* clip) {
	for (int32_t i = 0; i <= 127; i++) {
		if (clipForLastNoteReceived[i] == clip) {
			clipForLastNoteReceived[i] = nullptr;
		}
	}
}

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
				clip = getActiveClip(modelStack);
			}
			else {
				// for note off's, see if a note on message was previously sent so you can
				// direct the note off to the right place
				clip = clipForLastNoteReceived[note];
			}

			sendNoteToClip(fromDevice, clip, match, on, channel, note, velocity, doingMidiThru, shouldRecordNotesNowNow,
			               modelStack);
		}
		// all notes off
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
	if (clip && (!on || currentSong->isOutputActiveInArrangement(clip->output))) {
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
		// Output is a kit or melodic instrument
		if (modelStackWithTimelineCounter) {
			// Definitely don't record if muted in arrangement
			bool shouldRecordNotes = shouldRecordNotesNowNow && currentSong->isOutputActiveInArrangement(clip->output);
			if (clip->output->type == OutputType::KIT) {
				auto kit = (Kit*)clip->output;
				kit->receivedNoteForKit(modelStackWithTimelineCounter, fromDevice, on, channel,
				                        note - midiEngine.midiFollowKitRootNote, velocity, shouldRecordNotes,
				                        doingMidiThru, (InstrumentClip*)clip);
			}
			else {
				MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
				melodicInstrument->receivedNote(modelStackWithTimelineCounter, fromDevice, on, channel, match, note,
				                                velocity, shouldRecordNotes, doingMidiThru);
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

/// called from playback handler
/// determines whether a midi cc received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiFollow::midiCCReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
                                bool* doingMidiThru, ModelStack* modelStack) {
	MIDIMatchType match = checkMidiFollowMatch(fromDevice, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		// obtain clip for active context (for params that's only for the active mod controllable stack)
		Clip* clip = getSelectedOrActiveClip();

		bool isMIDIClip = false;
		bool isCVClip = false;
		if (clip) {
			if (clip->output->type == OutputType::MIDI_OUT) {
				isMIDIClip = true;
			}
			if (clip->output->type == OutputType::CV) {
				isCVClip = true;
			}
		}

		// don't offer to handleReceivedCC if it's a MIDI or CV Clip
		// this is because this function is used to control internal deluge parameters only (patched, unpatched)
		// midi/cv clip cc parameters are handled below in the offerReceivedCCToMelodicInstrument function
		if (clip && !isMIDIClip && !isCVClip
		    && (match == MIDIMatchType::MPE_MASTER || match == MIDIMatchType::CHANNEL)) {
			// if midi follow feedback and feedback filter is enabled,
			// check time elapsed since last midi cc was sent with midi feedback for this same ccNumber
			// if it was greater or equal than 1 second ago, allow received midi cc to go through
			// this helps avoid additional processing of midi cc's received
			if (!isFeedbackEnabled()
			    || (isFeedbackEnabled()
			        && (!midiEngine.midiFollowFeedbackFilter
			            || (midiEngine.midiFollowFeedbackFilter
			                && ((AudioEngine::audioSampleTimer - timeLastCCSent[ccNumber]) >= kSampleRate))))) {
				// setup model stack for the active context
				if (modelStack) {
					auto modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

					// See if it's learned to a parameter
					handleReceivedCC(*modelStackWithTimelineCounter, clip, ccNumber, value);
				}
			}
		}
		// for these cc's, always use the active clip for the output selected
		clip = getActiveClip(modelStack);
		if (clip) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
			if (modelStackWithTimelineCounter) {
				if (clip->output->type == OutputType::KIT) {
					Kit* kit = (Kit*)clip->output;
					kit->receivedCCForKit(modelStackWithTimelineCounter, fromDevice, match, channel, ccNumber, value,
					                      doingMidiThru, clip);
				}
				else {
					MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
					melodicInstrument->receivedCC(modelStackWithTimelineCounter, fromDevice, match, channel, ccNumber,
					                              value, doingMidiThru);
				}
			}
		}
	}
}

/// checks if the ccNumber received has been learned to any parameters in midi learning view
/// if the cc has been learned, it sets the new value for that parameter
/// this function works by first checking the active context to see if there is an active clip
/// to determine if the cc intends to control a song level or clip level parameter
void MidiFollow::handleReceivedCC(ModelStackWithTimelineCounter& modelStackWithTimelineCounter, Clip* clip,
                                  int32_t ccNumber, int32_t value) {

	// loop through the grid to see if any parameters have been learned to the ccNumber received
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			if (paramToCC[xDisplay][yDisplay] == ccNumber) {
				// obtain the model stack for the parameter the ccNumber received is learned to
				ModelStackWithAutoParam* modelStackWithParam =
				    getModelStackWithParam(&modelStackWithTimelineCounter, clip, xDisplay, yDisplay, ccNumber,
				                           midiEngine.midiFollowDisplayParam);
				// check if model stack is valid
				if (modelStackWithParam && modelStackWithParam->autoParam) {
					// get current value
					int32_t oldValue =
					    modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);

					// convert current value to knobPos to compare to cc value being received
					int32_t knobPos =
					    modelStackWithParam->paramCollection->paramValueToKnobPos(oldValue, modelStackWithParam);

					// calculate new knob position based on value received and deluge current value
					int32_t newKnobPos = MidiTakeover::calculateKnobPos(knobPos, value, nullptr, true, ccNumber);
					// is the cc being received for the same value as the current knob pos? If so, do nothing
					if (newKnobPos != knobPos) {
						// Convert the New Knob Position to a Parameter Value
						int32_t newValue =
						    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

						// Set the new Parameter Value for the MIDI Learned Parameter
						modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam,
						                                                          view.modPos, view.modLength);

						// check if you're currently editing the same learned param in automation view or
						// performance view if so, you will need to refresh the automation editor grid or the
						// performance view
						bool editingParamInAutomationOrPerformanceView = false;
						RootUI* rootUI = getRootUI();
						if (rootUI == &automationView || rootUI == &performanceSessionView) {
							int32_t id = modelStackWithParam->paramId;
							params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

							if (rootUI == &automationView) {
								// pass the current clip because you want to check that you're editing the param
								// for the same clip active in automation view
								editingParamInAutomationOrPerformanceView =
								    automationView.possiblyRefreshAutomationEditorGrid(clip, kind, id);
							}
							else {
								editingParamInAutomationOrPerformanceView =
								    performanceSessionView.possiblyRefreshPerformanceViewDisplay(kind, id, newKnobPos);
							}
						}

						// check if you should display name of the parameter that was changed and the value that
						// has been set if you're in the automation view editor or performance view non-editing
						// mode don't display popup if you're currently editing the same param
						if (midiEngine.midiFollowDisplayParam && !editingParamInAutomationOrPerformanceView) {
							params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
							view.displayModEncoderValuePopup(kind, modelStackWithParam->paramId, newKnobPos);
						}
					}
				}
			}
		}
	}
}

/// called when updating the context,
/// e.g. switching from song to clip, changing instruments presets, peeking a clip in song view
/// this function:
/// 1) checks the active context
/// 2) sets up the model stack for that context
/// 3) checks what parameters have been learned and obtains the model stack for those params
/// 4) sends midi feedback of the current parameter value to the cc numbers learned to those parameters
void MidiFollow::sendCCWithoutModelStackForMidiFollowFeedback(int32_t channel, bool isAutomation) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;

	// obtain clip for active context
	Clip* clip = getSelectedOrActiveClip();

	// setup model stack for the active context
	if (clip) {
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
		if (modelStack) {
			modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
		}
	}

	// check that model stack is valid
	if (modelStackWithTimelineCounter) {
		// loop through the grid to see if any parameters have been learned
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				if (paramToCC[xDisplay][yDisplay] != MIDI_CC_NONE) {
					// obtain the model stack for the parameter that has been learned
					ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(
					    modelStackWithTimelineCounter, clip, xDisplay, yDisplay, MIDI_CC_NONE, false);
					// check that model stack is valid
					if (modelStackWithParam && modelStackWithParam->autoParam) {
						if (!isAutomation || (isAutomation && modelStackWithParam->autoParam->isAutomated())) {
							// obtain current value of the learned parameter
							int32_t currentValue =
							    modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);

							// convert current value to a knob position
							int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(
							    currentValue, modelStackWithParam);

							// send midi feedback to the ccNumber learned to the param with the current knob
							// position
							sendCCForMidiFollowFeedback(channel, paramToCC[xDisplay][yDisplay], knobPos);
						}
					}
				}
			}
		}
	}
}

/// called when updating parameter values using mod (gold) encoders or the select encoder in the soudnEditor menu
void MidiFollow::sendCCForMidiFollowFeedback(int32_t channel, int32_t ccNumber, int32_t knobPos) {
	if (midiEngine.midiFollowFeedbackChannelType != MIDIFollowChannelType::NONE) {
		LearnedMIDI& midiInput =
		    midiEngine.midiFollowChannelType[util::to_underlying(midiEngine.midiFollowFeedbackChannelType)];

		if (midiInput.isForMPEZone()) {
			channel = midiInput.getMasterChannel();
		}

		int32_t midiOutputFilter = midiInput.channelOrZone;

		midiEngine.sendCC(this, channel, ccNumber, knobPos + kKnobPosOffset, midiOutputFilter);

		timeLastCCSent[ccNumber] = AudioEngine::audioSampleTimer;
	}
}

/// called from playback handler
/// determines whether a pitch bend received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiFollow::pitchBendReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
                                   bool* doingMidiThru, ModelStack* modelStack) {
	MIDIMatchType match = checkMidiFollowMatch(fromDevice, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		// obtain clip for active context
		Clip* clip = getActiveClip(modelStack);
		if (clip) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			if (modelStackWithTimelineCounter) {
				if (clip->output->type == OutputType::KIT) {
					Kit* kit = (Kit*)clip->output;
					kit->receivedPitchBendForKit(modelStackWithTimelineCounter, fromDevice, match, channel, data1,
					                             data2, doingMidiThru);
				}
				else {
					MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
					melodicInstrument->receivedPitchBend(modelStackWithTimelineCounter, fromDevice, match, channel,
					                                     data1, data2, doingMidiThru);
				}
			}
		}
	}
}

/// called from playback handler
/// determines whether aftertouch received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiFollow::aftertouchReceived(MIDIDevice* fromDevice, int32_t channel, int32_t value, int32_t noteCode,
                                    bool* doingMidiThru, ModelStack* modelStack) {
	MIDIMatchType match = checkMidiFollowMatch(fromDevice, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		// obtain clip for active context
		Clip* clip = getActiveClip(modelStack);
		if (clip) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			if (modelStackWithTimelineCounter) {
				if (clip->output->type == OutputType::KIT) {
					Kit* kit = (Kit*)clip->output;
					kit->receivedAftertouchForKit(modelStackWithTimelineCounter, fromDevice, match, channel, value,
					                              noteCode, doingMidiThru);
				}
				else {
					MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
					melodicInstrument->receivedAftertouch(modelStackWithTimelineCounter, fromDevice, match, channel,
					                                      value, noteCode, doingMidiThru);
				}
			}
		}
	}
}

/// obtain match to check if device is compatible with the midi follow channel
/// a valid match is passed through to the instruments for further evaluation
MIDIMatchType MidiFollow::checkMidiFollowMatch(MIDIDevice* fromDevice, uint8_t channel) {
	MIDIMatchType m = MIDIMatchType::NO_MATCH;
	for (auto i = 0; i < kNumMIDIFollowChannelTypes; i++) {
		m = midiEngine.midiFollowChannelType[i].checkMatch(fromDevice, channel);
		if (m != MIDIMatchType::NO_MATCH) {
			return m;
		}
	}
	return m;
}

bool MidiFollow::isFeedbackEnabled() {
	if (midiEngine.midiFollowFeedbackChannelType != MIDIFollowChannelType::NONE) {
		uint8_t channel =
		    midiEngine.midiFollowChannelType[util::to_underlying(midiEngine.midiFollowFeedbackChannelType)]
		        .channelOrZone;
		if (channel != MIDI_CHANNEL_NONE) {
			return true;
		}
	}
	return false;
}

/// create default XML file and write defaults
/// I should check if file exists before creating one
void MidiFollow::writeDefaultsToFile(StorageManager& bdsm) {
	// MidiFollow.xml
	Error error = bdsm.createXMLFile(MIDI_DEFAULTS_XML, smSerializer, true);
	if (error != Error::NONE) {
		return;
	}
	Serializer& writer = GetSerializer();
	//<defaults>
	writer.writeOpeningTagBeginning(MIDI_DEFAULTS_TAG);
	writer.writeOpeningTagEnd();

	//<defaultCCMappings>
	writer.writeOpeningTagBeginning(MIDI_DEFAULTS_CC_TAG);
	writer.writeOpeningTagEnd();

	writeDefaultMappingsToFile();

	writer.writeClosingTag(MIDI_DEFAULTS_CC_TAG);

	writer.writeClosingTag(MIDI_DEFAULTS_TAG);

	writer.closeFileAfterWriting();
}

/// convert paramID to a paramName to write to XML
void MidiFollow::writeDefaultMappingsToFile() {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			bool writeTag = false;
			char const* paramName;

			if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramName = params::paramNameForFile(params::Kind::PATCHED, patchedParamShortcuts[xDisplay][yDisplay]);
				writeTag = true;
			}
			else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramName = params::paramNameForFile(params::Kind::UNPATCHED_SOUND,
				                                     params::UNPATCHED_START
				                                         + unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay]);
				writeTag = true;
			}
			else if (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramName = params::paramNameForFile(params::Kind::UNPATCHED_GLOBAL,
				                                     params::UNPATCHED_START
				                                         + unpatchedGlobalParamShortcuts[xDisplay][yDisplay]);
				writeTag = true;
			}

			if (writeTag) {
				char buffer[10];
				intToString(paramToCC[xDisplay][yDisplay], buffer);
				Serializer& writer = GetSerializer();
				writer.writeTag(paramName, buffer);
			}
		}
	}
}

/// read defaults from XML
void MidiFollow::readDefaultsFromFile(StorageManager& bdsm) {
	// no need to keep reading from SD card after first load
	if (successfullyReadDefaultsFromFile) {
		return;
	}
	else {
		init();
	}

	FilePointer fp;
	// MIDIFollow.XML
	bool success = bdsm.fileExists(MIDI_DEFAULTS_XML, &fp);
	if (!success) {
		writeDefaultsToFile(bdsm);
		successfullyReadDefaultsFromFile = true;
		return;
	}

	//<defaults>
	Error error = bdsm.openXMLFile(&fp, smDeserializer, MIDI_DEFAULTS_TAG);
	if (error != Error::NONE) {
		writeDefaultsToFile(bdsm);
		successfullyReadDefaultsFromFile = true;
		return;
	}
	Deserializer& reader = smDeserializer;
	char const* tagName;
	// step into the <defaultCCMappings> tag
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, MIDI_DEFAULTS_CC_TAG)) {
			readDefaultMappingsFromFile(reader);
		}
		reader.exitTag();
	}

	bdsm.closeFile(smDeserializer.readFIL);

	successfullyReadDefaultsFromFile = true;
}

/// compares param name tag to the list of params available are midi controllable
/// if param is found, it loads the CC mapping info for that param into the view
void MidiFollow::readDefaultMappingsFromFile(Deserializer& reader) {
	char const* tagName;
	bool foundParam;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		foundParam = false;
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				// let's see if this x, y corresponds to a valid param shortcut
				// if we have a valid param shortcut, let's confirm the tag name corresponds to that shortcut
				if (((patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID)
				     && !strcmp(tagName, params::paramNameForFile(params::Kind::PATCHED,
				                                                  patchedParamShortcuts[xDisplay][yDisplay])))
				    || ((unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)
				        && !strcmp(tagName,
				                   params::paramNameForFile(
				                       params::Kind::UNPATCHED_SOUND,
				                       params::UNPATCHED_START + unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay])))
				    || ((unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)
				        && !strcmp(tagName,
				                   params::paramNameForFile(
				                       params::Kind::UNPATCHED_GLOBAL,
				                       params::UNPATCHED_START + unpatchedGlobalParamShortcuts[xDisplay][yDisplay])))) {
					// tag name matches the param shortcut, so we can load the cc mapping for that param
					// into the paramToCC grid shortcut array which holds the cc value for each param
					paramToCC[xDisplay][yDisplay] = reader.readTagOrAttributeValueInt();
					// now that we've handled this tag, we need to break out of these for loops
					// as you can only read from a tag once (otherwise next read will result in a crash "BBBB")
					foundParam = true;
					break;
				}
			}
			// break out of the first for loop if a param was found in the second for loop above
			if (foundParam) {
				break;
			}
		}
		// exit out of this tag so you can check the next tag
		reader.exitTag();
	}
}
