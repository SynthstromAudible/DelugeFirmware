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
#include "gui/views/performance_view.h"
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
#include <cstdlib>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

using namespace deluge;
using namespace gui;

#define SETTINGS_FOLDER "SETTINGS"
#define MIDI_FOLLOW_XML "SETTINGS/MIDIFollow.XML"
#define MIDI_DEFAULTS_TAG "defaults"
#define MIDI_DEFAULTS_CC_TAG "defaultCCMappings"

constexpr int32_t PARAM_ID_NONE = 255;

PLACE_SDRAM_BSS MidiFollow midiFollow{};

// initialize variables
MidiFollow::MidiFollow() {
	init();
}

void MidiFollow::init() {
	initState();
	initDefaultMappings();
}

void MidiFollow::initState() {
	successfullyReadDefaultsFromFile = false;
	for (int32_t i = 0; i < (kMaxMIDIValue + 1); i++) {
		timeLastCCSent[i] = 0;
		previousKnobPos[i] = kNoSelection;
	}

	timeAutomationFeedbackLastSent = 0;
}

void MidiFollow::clearMappings() {
	ccToSoundParam.fill(PARAM_ID_NONE);
	ccToGlobalParam.fill(PARAM_ID_NONE);

	soundParamToCC.fill(MIDI_CC_NONE);
	globalParamToCC.fill(MIDI_CC_NONE);
}

void MidiFollow::initDefaultMappings() {
	clearMappings();

	// SOUND PARAMS
	// NOTE: when adding an UNPATCHED param, make sure you add params::UNPATCHED_START to it for the indexes

	ccToSoundParam[3] = params::LOCAL_PITCH_ADJUST;
	soundParamToCC[params::LOCAL_PITCH_ADJUST] = 3;
	ccToSoundParam[5] = params::UNPATCHED_START + params::UNPATCHED_PORTAMENTO;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_PORTAMENTO] = 5;
	ccToSoundParam[7] = params::GLOBAL_VOLUME_POST_FX;
	soundParamToCC[params::GLOBAL_VOLUME_POST_FX] = 7;
	ccToSoundParam[10] = params::LOCAL_PAN;
	soundParamToCC[params::LOCAL_PAN] = 10;
	ccToSoundParam[12] = params::LOCAL_OSC_A_PITCH_ADJUST;
	soundParamToCC[params::LOCAL_OSC_A_PITCH_ADJUST] = 12;
	ccToSoundParam[13] = params::LOCAL_OSC_B_PITCH_ADJUST;
	soundParamToCC[params::LOCAL_OSC_B_PITCH_ADJUST] = 13;
	ccToSoundParam[14] = params::LOCAL_MODULATOR_0_PITCH_ADJUST;
	soundParamToCC[params::LOCAL_MODULATOR_0_PITCH_ADJUST] = 14;
	ccToSoundParam[15] = params::LOCAL_MODULATOR_1_PITCH_ADJUST;
	soundParamToCC[params::LOCAL_MODULATOR_1_PITCH_ADJUST] = 15;
	ccToSoundParam[16] = params::GLOBAL_MOD_FX_RATE;
	soundParamToCC[params::GLOBAL_MOD_FX_RATE] = 16;
	ccToSoundParam[17] = params::UNPATCHED_START + params::UNPATCHED_MOD_FX_FEEDBACK;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_MOD_FX_FEEDBACK] = 17;
	ccToSoundParam[18] = params::UNPATCHED_START + params::UNPATCHED_MOD_FX_OFFSET;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_MOD_FX_OFFSET] = 18;
	ccToSoundParam[19] = params::LOCAL_FOLD;
	soundParamToCC[params::LOCAL_FOLD] = 19;
	ccToSoundParam[20] = params::UNPATCHED_START + params::UNPATCHED_STUTTER_RATE;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_STUTTER_RATE] = 20;
	ccToSoundParam[21] = params::LOCAL_OSC_A_VOLUME;
	soundParamToCC[params::LOCAL_OSC_A_VOLUME] = 21;
	ccToSoundParam[23] = params::LOCAL_OSC_A_PHASE_WIDTH;
	soundParamToCC[params::LOCAL_OSC_A_PHASE_WIDTH] = 23;
	ccToSoundParam[24] = params::LOCAL_CARRIER_0_FEEDBACK;
	soundParamToCC[params::LOCAL_CARRIER_0_FEEDBACK] = 24;
	ccToSoundParam[25] = params::LOCAL_OSC_A_WAVE_INDEX;
	soundParamToCC[params::LOCAL_OSC_A_WAVE_INDEX] = 25;
	ccToSoundParam[26] = params::LOCAL_OSC_B_VOLUME;
	soundParamToCC[params::LOCAL_OSC_B_VOLUME] = 26;
	ccToSoundParam[27] = params::UNPATCHED_START + params::UNPATCHED_COMPRESSOR_THRESHOLD;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_COMPRESSOR_THRESHOLD] = 27;
	ccToSoundParam[28] = params::LOCAL_OSC_B_PHASE_WIDTH;
	soundParamToCC[params::LOCAL_OSC_B_PHASE_WIDTH] = 28;
	ccToSoundParam[29] = params::LOCAL_CARRIER_1_FEEDBACK;
	soundParamToCC[params::LOCAL_CARRIER_1_FEEDBACK] = 29;
	ccToSoundParam[30] = params::LOCAL_OSC_A_WAVE_INDEX;
	soundParamToCC[params::LOCAL_OSC_A_WAVE_INDEX] = 30;
	ccToSoundParam[36] = params::UNPATCHED_START + params::UNPATCHED_REVERSE_PROBABILITY;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_REVERSE_PROBABILITY] = 36;
	ccToSoundParam[37] = params::UNPATCHED_START + params::UNPATCHED_SPREAD_VELOCITY;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_SPREAD_VELOCITY] = 37;
	ccToSoundParam[39] = params::UNPATCHED_START + params::UNPATCHED_ARP_SPREAD_OCTAVE;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_SPREAD_OCTAVE] = 39;
	ccToSoundParam[40] = params::UNPATCHED_START + params::UNPATCHED_ARP_SPREAD_GATE;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_SPREAD_GATE] = 40;
	ccToSoundParam[41] = params::LOCAL_NOISE_VOLUME;
	soundParamToCC[params::LOCAL_NOISE_VOLUME] = 41;
	ccToSoundParam[42] = params::UNPATCHED_START + params::UNPATCHED_ARP_RHYTHM;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_RHYTHM] = 42;
	ccToSoundParam[43] = params::UNPATCHED_START + params::UNPATCHED_ARP_SEQUENCE_LENGTH;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_SEQUENCE_LENGTH] = 43;
	ccToSoundParam[44] = params::UNPATCHED_START + params::UNPATCHED_ARP_CHORD_POLYPHONY;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_CHORD_POLYPHONY] = 44;
	ccToSoundParam[45] = params::UNPATCHED_START + params::UNPATCHED_ARP_RATCHET_AMOUNT;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_RATCHET_AMOUNT] = 45;
	ccToSoundParam[46] = params::UNPATCHED_START + params::UNPATCHED_NOTE_PROBABILITY;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_NOTE_PROBABILITY] = 46;
	ccToSoundParam[47] = params::UNPATCHED_START + params::UNPATCHED_ARP_BASS_PROBABILITY;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_BASS_PROBABILITY] = 47;
	ccToSoundParam[48] = params::UNPATCHED_START + params::UNPATCHED_ARP_CHORD_PROBABILITY;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_CHORD_PROBABILITY] = 48;
	ccToSoundParam[49] = params::UNPATCHED_START + params::UNPATCHED_ARP_RATCHET_PROBABILITY;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_RATCHET_PROBABILITY] = 49;
	ccToSoundParam[50] = params::UNPATCHED_START + params::UNPATCHED_ARP_GATE;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_GATE] = 50;
	ccToSoundParam[51] = params::GLOBAL_ARP_RATE;
	soundParamToCC[params::GLOBAL_ARP_RATE] = 51;
	ccToSoundParam[52] = params::GLOBAL_DELAY_FEEDBACK;
	soundParamToCC[params::GLOBAL_DELAY_FEEDBACK] = 52;
	ccToSoundParam[53] = params::GLOBAL_DELAY_RATE;
	soundParamToCC[params::GLOBAL_DELAY_RATE] = 53;
	ccToSoundParam[54] = params::LOCAL_MODULATOR_0_VOLUME;
	soundParamToCC[params::LOCAL_MODULATOR_0_VOLUME] = 54;
	ccToSoundParam[55] = params::LOCAL_MODULATOR_0_FEEDBACK;
	soundParamToCC[params::LOCAL_MODULATOR_0_FEEDBACK] = 55;
	ccToSoundParam[56] = params::LOCAL_MODULATOR_1_VOLUME;
	soundParamToCC[params::LOCAL_MODULATOR_1_VOLUME] = 56;
	ccToSoundParam[57] = params::LOCAL_MODULATOR_1_FEEDBACK;
	soundParamToCC[params::LOCAL_MODULATOR_1_FEEDBACK] = 57;
	ccToSoundParam[58] = params::GLOBAL_LFO_FREQ_1;
	soundParamToCC[params::GLOBAL_LFO_FREQ_1] = 58;
	ccToSoundParam[59] = params::LOCAL_LFO_LOCAL_FREQ_1;
	soundParamToCC[params::LOCAL_LFO_LOCAL_FREQ_1] = 59;
	ccToSoundParam[60] = params::UNPATCHED_START + params::UNPATCHED_SIDECHAIN_SHAPE;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_SIDECHAIN_SHAPE] = 60;
	// TODO: replace this with the patch cable from sidechain to volume, once midi follow supports patch cables
	// ccToSoundParam[61] = params::GLOBAL_VOLUME_POST_REVERB_SEND;
	// soundParamToCC[params::GLOBAL_VOLUME_POST_REVERB_SEND] = 61;
	ccToSoundParam[62] = params::UNPATCHED_START + params::UNPATCHED_BITCRUSHING;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_BITCRUSHING] = 62;
	ccToSoundParam[63] = params::UNPATCHED_START + params::UNPATCHED_SAMPLE_RATE_REDUCTION;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_SAMPLE_RATE_REDUCTION] = 63;
	ccToSoundParam[70] = params::LOCAL_LPF_MORPH;
	soundParamToCC[params::LOCAL_LPF_MORPH] = 70;
	ccToSoundParam[71] = params::LOCAL_LPF_RESONANCE;
	soundParamToCC[params::LOCAL_LPF_RESONANCE] = 71;
	ccToSoundParam[72] = params::LOCAL_ENV_0_RELEASE;
	soundParamToCC[params::LOCAL_ENV_0_RELEASE] = 72;
	ccToSoundParam[73] = params::LOCAL_ENV_0_ATTACK;
	soundParamToCC[params::LOCAL_ENV_0_ATTACK] = 73;
	ccToSoundParam[74] = params::LOCAL_LPF_FREQ;
	soundParamToCC[params::LOCAL_LPF_FREQ] = 74;
	ccToSoundParam[75] = params::LOCAL_ENV_0_DECAY;
	soundParamToCC[params::LOCAL_ENV_0_DECAY] = 75;
	ccToSoundParam[76] = params::LOCAL_ENV_0_SUSTAIN;
	soundParamToCC[params::LOCAL_ENV_0_SUSTAIN] = 76;
	ccToSoundParam[77] = params::LOCAL_ENV_1_ATTACK;
	soundParamToCC[params::LOCAL_ENV_1_ATTACK] = 77;
	ccToSoundParam[78] = params::LOCAL_ENV_1_DECAY;
	soundParamToCC[params::LOCAL_ENV_1_DECAY] = 78;
	ccToSoundParam[79] = params::LOCAL_ENV_1_SUSTAIN;
	soundParamToCC[params::LOCAL_ENV_1_SUSTAIN] = 79;
	ccToSoundParam[80] = params::LOCAL_ENV_1_RELEASE;
	soundParamToCC[params::LOCAL_ENV_1_RELEASE] = 80;
	ccToSoundParam[81] = params::LOCAL_HPF_FREQ;
	soundParamToCC[params::LOCAL_HPF_FREQ] = 81;
	ccToSoundParam[82] = params::LOCAL_HPF_RESONANCE;
	soundParamToCC[params::LOCAL_HPF_RESONANCE] = 82;
	ccToSoundParam[83] = params::LOCAL_HPF_MORPH;
	soundParamToCC[params::LOCAL_HPF_MORPH] = 83;
	ccToSoundParam[84] = params::UNPATCHED_START + params::UNPATCHED_BASS_FREQ;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_BASS_FREQ] = 84;
	ccToSoundParam[85] = params::UNPATCHED_START + params::UNPATCHED_TREBLE_FREQ;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_TREBLE_FREQ] = 85;
	ccToSoundParam[86] = params::UNPATCHED_START + params::UNPATCHED_BASS;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_BASS] = 86;
	ccToSoundParam[87] = params::UNPATCHED_START + params::UNPATCHED_TREBLE;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_TREBLE] = 87;
	ccToSoundParam[91] = params::GLOBAL_REVERB_AMOUNT;
	soundParamToCC[params::GLOBAL_REVERB_AMOUNT] = 91;
	ccToSoundParam[93] = params::GLOBAL_MOD_FX_DEPTH;
	soundParamToCC[params::GLOBAL_MOD_FX_DEPTH] = 93;
	ccToSoundParam[102] = params::LOCAL_ENV_2_ATTACK;
	soundParamToCC[params::LOCAL_ENV_2_ATTACK] = 102;
	ccToSoundParam[103] = params::LOCAL_ENV_2_DECAY;
	soundParamToCC[params::LOCAL_ENV_2_DECAY] = 103;
	ccToSoundParam[104] = params::LOCAL_ENV_2_SUSTAIN;
	soundParamToCC[params::LOCAL_ENV_2_SUSTAIN] = 104;
	ccToSoundParam[105] = params::LOCAL_ENV_2_RELEASE;
	soundParamToCC[params::LOCAL_ENV_2_RELEASE] = 105;
	ccToSoundParam[106] = params::LOCAL_ENV_3_ATTACK;
	soundParamToCC[params::LOCAL_ENV_3_ATTACK] = 106;
	ccToSoundParam[107] = params::LOCAL_ENV_3_DECAY;
	soundParamToCC[params::LOCAL_ENV_3_DECAY] = 107;
	ccToSoundParam[108] = params::LOCAL_ENV_3_SUSTAIN;
	soundParamToCC[params::LOCAL_ENV_3_SUSTAIN] = 108;
	ccToSoundParam[109] = params::LOCAL_ENV_3_RELEASE;
	soundParamToCC[params::LOCAL_ENV_3_RELEASE] = 109;
	ccToSoundParam[110] = params::GLOBAL_LFO_FREQ_2;
	soundParamToCC[params::GLOBAL_LFO_FREQ_2] = 110;
	ccToSoundParam[111] = params::LOCAL_LFO_LOCAL_FREQ_2;
	soundParamToCC[params::LOCAL_LFO_LOCAL_FREQ_2] = 111;
	ccToSoundParam[112] = params::UNPATCHED_START + params::UNPATCHED_ARP_SWAP_PROBABILITY;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_SWAP_PROBABILITY] = 112;
	ccToSoundParam[113] = params::UNPATCHED_START + params::UNPATCHED_ARP_GLIDE_PROBABILITY;
	soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_ARP_GLIDE_PROBABILITY] = 113;

	// GLOBAL PARAMS
	// NOTE: Here you add the global param, assigning the same CC as its relative sound param

	ccToGlobalParam[3] = params::UNPATCHED_PITCH_ADJUST;
	globalParamToCC[params::UNPATCHED_PITCH_ADJUST] = 3;
	ccToGlobalParam[7] = params::UNPATCHED_VOLUME;
	globalParamToCC[params::UNPATCHED_VOLUME] = 7;
	ccToGlobalParam[10] = params::UNPATCHED_PAN;
	globalParamToCC[params::UNPATCHED_PAN] = 10;
	ccToGlobalParam[16] = params::UNPATCHED_MOD_FX_RATE;
	globalParamToCC[params::UNPATCHED_MOD_FX_RATE] = 16;
	ccToGlobalParam[17] = params::UNPATCHED_MOD_FX_FEEDBACK;
	globalParamToCC[params::UNPATCHED_MOD_FX_FEEDBACK] = 17;
	ccToGlobalParam[18] = params::UNPATCHED_MOD_FX_OFFSET;
	globalParamToCC[params::UNPATCHED_MOD_FX_OFFSET] = 18;
	ccToGlobalParam[20] = params::UNPATCHED_STUTTER_RATE;
	globalParamToCC[params::UNPATCHED_STUTTER_RATE] = 20;
	ccToGlobalParam[51] = params::UNPATCHED_ARP_RATE;
	globalParamToCC[params::UNPATCHED_ARP_RATE] = 51;
	ccToGlobalParam[52] = params::UNPATCHED_DELAY_AMOUNT;
	globalParamToCC[params::UNPATCHED_DELAY_AMOUNT] = 52;
	ccToGlobalParam[53] = params::UNPATCHED_DELAY_RATE;
	globalParamToCC[params::UNPATCHED_DELAY_RATE] = 53;
	ccToGlobalParam[60] = params::UNPATCHED_SIDECHAIN_SHAPE;
	globalParamToCC[params::UNPATCHED_SIDECHAIN_SHAPE] = 60;
	ccToGlobalParam[61] = params::UNPATCHED_SIDECHAIN_VOLUME;
	globalParamToCC[params::UNPATCHED_SIDECHAIN_VOLUME] = 61;
	ccToGlobalParam[62] = params::UNPATCHED_BITCRUSHING;
	globalParamToCC[params::UNPATCHED_BITCRUSHING] = 62;
	ccToGlobalParam[63] = params::UNPATCHED_SAMPLE_RATE_REDUCTION;
	globalParamToCC[params::UNPATCHED_SAMPLE_RATE_REDUCTION] = 63;
	ccToGlobalParam[70] = params::UNPATCHED_LPF_MORPH;
	globalParamToCC[params::UNPATCHED_LPF_MORPH] = 70;
	ccToGlobalParam[71] = params::UNPATCHED_LPF_RES;
	globalParamToCC[params::UNPATCHED_LPF_RES] = 71;
	ccToGlobalParam[74] = params::UNPATCHED_LPF_FREQ;
	globalParamToCC[params::UNPATCHED_LPF_FREQ] = 74;
	ccToGlobalParam[81] = params::UNPATCHED_HPF_FREQ;
	globalParamToCC[params::UNPATCHED_HPF_FREQ] = 81;
	ccToGlobalParam[82] = params::UNPATCHED_HPF_RES;
	globalParamToCC[params::UNPATCHED_HPF_RES] = 82;
	ccToGlobalParam[83] = params::UNPATCHED_HPF_MORPH;
	globalParamToCC[params::UNPATCHED_HPF_MORPH] = 83;
	ccToGlobalParam[84] = params::UNPATCHED_BASS_FREQ;
	globalParamToCC[params::UNPATCHED_BASS_FREQ] = 74;
	ccToGlobalParam[85] = params::UNPATCHED_TREBLE_FREQ;
	globalParamToCC[params::UNPATCHED_TREBLE_FREQ] = 81;
	ccToGlobalParam[86] = params::UNPATCHED_BASS;
	globalParamToCC[params::UNPATCHED_BASS] = 71;
	ccToGlobalParam[87] = params::UNPATCHED_TREBLE;
	globalParamToCC[params::UNPATCHED_TREBLE] = 82;
	ccToGlobalParam[91] = params::UNPATCHED_REVERB_SEND_AMOUNT;
	globalParamToCC[params::UNPATCHED_REVERB_SEND_AMOUNT] = 91;
	ccToGlobalParam[93] = params::UNPATCHED_MOD_FX_DEPTH;
	globalParamToCC[params::UNPATCHED_MOD_FX_DEPTH] = 93;
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
	case UIType::PERFORMANCE:
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
                                   int32_t soundParamId, int32_t globalParamId, bool displayError) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	// non-null clip means you're dealing with the clip context
	if (clip) {
		if (modelStackWithTimelineCounter) {
			modelStackWithParam =
			    getModelStackWithParamForClip(modelStackWithTimelineCounter, clip, soundParamId, globalParamId);
		}
	}

	if (displayError && (!modelStackWithParam || !modelStackWithParam->autoParam)) {
		displayParamControlError(soundParamId, globalParamId);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                          int32_t soundParamId, int32_t globalParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	OutputType outputType = clip->output->type;

	switch (outputType) {
	case OutputType::SYNTH:
		modelStackWithParam =
		    getModelStackWithParamForSynthClip(modelStackWithTimelineCounter, clip, soundParamId, globalParamId);
		break;
	case OutputType::KIT:
		modelStackWithParam =
		    getModelStackWithParamForKitClip(modelStackWithTimelineCounter, clip, soundParamId, globalParamId);
		break;
	case OutputType::AUDIO:
		modelStackWithParam =
		    getModelStackWithParamForAudioClip(modelStackWithTimelineCounter, clip, soundParamId, globalParamId);
		break;
	// explicit fallthrough cases
	case OutputType::CV:
	case OutputType::MIDI_OUT:
	case OutputType::NONE:;
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForSynthClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                               int32_t soundParamId, int32_t globalParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	params::Kind paramKind = params::Kind::NONE;
	int32_t paramID = PARAM_ID_NONE;

	if (soundParamId != PARAM_ID_NONE && soundParamId < params::UNPATCHED_START) {
		paramKind = params::Kind::PATCHED;
		paramID = soundParamId;
	}
	else if (soundParamId != PARAM_ID_NONE && soundParamId >= params::UNPATCHED_START) {
		paramKind = params::Kind::UNPATCHED_SOUND;
		paramID = soundParamId - params::UNPATCHED_START;
	}
	if ((paramKind != params::Kind::NONE) && (paramID != PARAM_ID_NONE)) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam =
		    clip->output->getModelStackWithParam(modelStackWithTimelineCounter, clip, paramID, paramKind, true, false);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForKitClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                             int32_t soundParamId, int32_t globalParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	params::Kind paramKind = params::Kind::NONE;
	int32_t paramID = PARAM_ID_NONE;
	InstrumentClip* instrumentClip = (InstrumentClip*)clip;

	if (!instrumentClip->affectEntire) {
		if (soundParamId != PARAM_ID_NONE && soundParamId < params::UNPATCHED_START) {
			paramKind = params::Kind::PATCHED;
			paramID = soundParamId;
		}
		else if (soundParamId != PARAM_ID_NONE && soundParamId >= params::UNPATCHED_START) {
			// don't allow control of Portamento in Kit's
			if (soundParamId - params::UNPATCHED_START != params::UNPATCHED_PORTAMENTO) {
				paramKind = params::Kind::UNPATCHED_SOUND;
				paramID = soundParamId - params::UNPATCHED_START;
			}
		}
	}
	else {
		if (globalParamId != PARAM_ID_NONE) {
			paramKind = params::Kind::UNPATCHED_GLOBAL;
			paramID = globalParamId;
		}
	}

	if ((paramKind != params::Kind::NONE) && (paramID != PARAM_ID_NONE)) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam = clip->output->getModelStackWithParam(modelStackWithTimelineCounter, clip, paramID,
		                                                           paramKind, instrumentClip->affectEntire, false);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForAudioClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                               int32_t soundParamId, int32_t globalParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	params::Kind paramKind = params::Kind::UNPATCHED_GLOBAL;
	int32_t paramID = globalParamId;

	if (paramID != PARAM_ID_NONE) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam =
		    clip->output->getModelStackWithParam(modelStackWithTimelineCounter, clip, paramID, paramKind, true, false);
	}

	return modelStackWithParam;
}

void MidiFollow::displayParamControlError(int32_t soundParamId, int32_t globalParamId) {
	params::Kind paramKind = params::Kind::NONE;
	int32_t paramID = PARAM_ID_NONE;

	if (soundParamId != PARAM_ID_NONE && soundParamId < params::UNPATCHED_START) {
		paramKind = params::Kind::PATCHED;
		paramID = soundParamId;
	}
	else if (soundParamId != PARAM_ID_NONE && soundParamId >= params::UNPATCHED_START) {
		paramKind = params::Kind::UNPATCHED_SOUND;
		paramID = soundParamId - params::UNPATCHED_START;
	}
	else if (globalParamId != PARAM_ID_NONE) {
		paramKind = params::Kind::UNPATCHED_GLOBAL;
		paramID = globalParamId;
	}

	if (paramID != PARAM_ID_NONE) {
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
}

/// a parameter can be learned to one cc at a time
/// for a given parameter, find and return the cc that has been learned (if any)
/// for the current midi follow controllable context
/// if no cc is found, then MIDI_CC_NONE (255) is returned
int32_t MidiFollow::getCCFromParam(params::Kind paramKind, int32_t paramID) {
	// audio clip or kit with affect entire enabled
	if (isGlobalEffectableContext()) {
		if (paramKind == params::Kind::UNPATCHED_GLOBAL) {
			return globalParamToCC[paramID];
		}
	}
	// synth clip or kit row
	else {
		if (paramKind == params::Kind::PATCHED) {
			return soundParamToCC[paramID];
		}
		else if (paramKind == params::Kind::UNPATCHED_SOUND) {
			return soundParamToCC[params::UNPATCHED_START + paramID];
		}
	}
	return MIDI_CC_NONE;
}

// Check if midi follow is controlling the global effectable context
// if so, we should only be controlling the global effectable parameters
bool MidiFollow::isGlobalEffectableContext() {
	// obtain clip for active context (for params that's only for the active mod controllable stack)
	Clip* clip = getSelectedOrActiveClip();
	if (clip != nullptr) {
		// audio clips are always global effectable
		if (clip->output->type == OutputType::AUDIO) {
			return true;
		}
		// kits may be global effectable depending on affect entire status
		else if (clip->output->type == OutputType::KIT) {
			bool affectEntire = ((InstrumentClip*)clip)->affectEntire;
			// if affect entire is enabled, then midi follow controls global effectable params
			if (affectEntire) {
				return true;
			}
		}
	}
	return false;
}

/// used to store the clip's for each note received so that note off's can be sent to the right clip
PLACE_SDRAM_BSS Clip* clipForLastNoteReceived[kMaxMIDIValue + 1] = {0};

/// initializes the clipForLastNoteReceived array
/// called when swapping songs to make sure that you aren't using clips from the old song
void MidiFollow::clearStoredClips() {
	for (int32_t i = 0; i <= 127; i++) {
		clipForLastNoteReceived[i] = nullptr;
	}
}

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
void MidiFollow::noteMessageReceived(MIDICable& cable, bool on, int32_t channel, int32_t note, int32_t velocity,
                                     bool* doingMidiThru, bool shouldRecordNotesNowNow, ModelStack* modelStack) {
	MIDIMatchType match = checkMidiFollowMatch(cable, channel);
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

			sendNoteToClip(cable, clip, match, on, channel, note, velocity, doingMidiThru, shouldRecordNotesNowNow,
			               modelStack);
		}
		// all notes off
		else if (note == ALL_NOTES_OFF) {
			for (int32_t i = 0; i <= 127; i++) {
				if (clipForLastNoteReceived[i]) {
					sendNoteToClip(cable, clipForLastNoteReceived[i], match, on, channel, i, velocity, doingMidiThru,
					               shouldRecordNotesNowNow, modelStack);
				}
			}
		}
	}
}

void MidiFollow::sendNoteToClip(MIDICable& cable, Clip* clip, MIDIMatchType match, bool on, int32_t channel,
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
				kit->receivedNoteForKit(modelStackWithTimelineCounter, cable, on, channel,
				                        note - midiEngine.midiFollowKitRootNote, velocity, shouldRecordNotes,
				                        doingMidiThru, (InstrumentClip*)clip);
			}
			else {
				MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
				melodicInstrument->receivedNote(modelStackWithTimelineCounter, cable, on, channel, match, note,
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
void MidiFollow::midiCCReceived(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t ccValue,
                                bool* doingMidiThru, ModelStack* modelStack) {
	MIDIMatchType match = checkMidiFollowMatch(cable, channel);
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

					if (modelStackWithTimelineCounter) {
						// See if it's learned to a parameter
						handleReceivedCC(*modelStackWithTimelineCounter, clip, ccNumber, ccValue);
					}
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
					kit->receivedCCForKit(modelStackWithTimelineCounter, cable, match, channel, ccNumber, ccValue,
					                      doingMidiThru, clip);
				}
				else {
					MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
					melodicInstrument->receivedCC(modelStackWithTimelineCounter, cable, match, channel, ccNumber,
					                              ccValue, doingMidiThru);
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
                                  int32_t ccNumber, int32_t ccValue) {

	int32_t modPos = 0;
	int32_t modLength = 0;
	bool isStepEditing = false;

	if (modelStackWithTimelineCounter.timelineCounterIsSet()) {
		TimelineCounter* timelineCounter = modelStackWithTimelineCounter.getTimelineCounter();

		// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
		// region.
		if (view.modLength && timelineCounter == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
			modPos = view.modPos;
			modLength = view.modLength;
			isStepEditing = true;
		}

		timelineCounter->possiblyCloneForArrangementRecording(&modelStackWithTimelineCounter);
	}

	// directly access the parameter from the CC number
	uint8_t soundParamId = ccToSoundParam[ccNumber];
	uint8_t globalParamId = ccToGlobalParam[ccNumber];
	if (soundParamId == PARAM_ID_NONE && globalParamId == PARAM_ID_NONE) {
		// Abort
		return;
	}

	ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(
	    &modelStackWithTimelineCounter, clip, soundParamId, globalParamId, midiEngine.midiFollowDisplayParam);
	// check if model stack is valid
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t currentValue;

		// get current value
		if (isStepEditing) {
			currentValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
		}
		else {
			currentValue = modelStackWithParam->autoParam->getCurrentValue();
		}

		// convert current value to knobPos to compare to cc value being received
		int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(currentValue, modelStackWithParam);

		// calculate new knob position based on cc value received and deluge current value
		int32_t newKnobPos = MidiTakeover::calculateKnobPos(knobPos, ccValue, nullptr, true, ccNumber, isStepEditing);

		// is the cc being received for the same value as the current knob pos? If so, do nothing
		if (newKnobPos != knobPos) {
			// Convert the New Knob Position to a Parameter Value
			int32_t newValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

			// Set the new Parameter Value for the MIDI Learned Parameter
			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos, modLength);

			// check if you're currently editing the same learned param in automation view or
			// performance view if so, you will need to refresh the automation editor grid or the
			// performance view
			bool editingParamInAutomationOrPerformanceView = false;
			RootUI* rootUI = getRootUI();
			if (rootUI == &automationView || rootUI == &performanceView) {
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
					    performanceView.possiblyRefreshPerformanceViewDisplay(kind, id, newKnobPos);
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
		int32_t modPos = 0;
		bool isStepEditing = false;

		if (modelStackWithTimelineCounter->timelineCounterIsSet()) {
			TimelineCounter* timelineCounter = modelStackWithTimelineCounter->getTimelineCounter();

			// Only if this exact TimelineCounter is having automation step-edited, we can send the value for just a
			// region.
			if (view.modLength
			    && timelineCounter == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

				// don't send automation feedback if you're step editing
				if (isAutomation) {
					return;
				}

				modPos = view.modPos;
				isStepEditing = true;
			}
		}

		// loop through all params to see if any parameters have been learned
		for (int32_t ccNumber = 0; ccNumber <= kMaxMIDIValue; ccNumber++) {
			uint8_t soundParamId = ccToSoundParam[ccNumber];
			uint8_t globalParamId = ccToGlobalParam[ccNumber];
			// obtain the model stack for the parameter that has been learned
			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStackWithTimelineCounter, clip, soundParamId, globalParamId, false);
			// check that model stack is valid
			if (modelStackWithParam && modelStackWithParam->autoParam) {
				if (!isAutomation || (isAutomation && modelStackWithParam->autoParam->isAutomated())) {
					int32_t currentValue;
					// obtain current value of the learned parameter
					if (isStepEditing) {
						currentValue =
						    modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
					}
					else {
						currentValue = modelStackWithParam->autoParam->getCurrentValue();
					}

					// convert current value to a knob position
					int32_t knobPos =
					    modelStackWithParam->paramCollection->paramValueToKnobPos(currentValue, modelStackWithParam);

					// send midi feedback to the ccNumber learned to the param with the current knob
					// position
					sendCCForMidiFollowFeedback(channel, ccNumber, knobPos);
				}
			}
		}
	}
}

/// called when updating parameter values using mod (gold) encoders or the select encoder in the soundEditor menu
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
void MidiFollow::pitchBendReceived(MIDICable& cable, uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru,
                                   ModelStack* modelStack) {
	MIDIMatchType match = checkMidiFollowMatch(cable, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		// obtain clip for active context
		Clip* clip = getActiveClip(modelStack);
		if (clip) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			if (modelStackWithTimelineCounter) {
				if (clip->output->type == OutputType::KIT) {
					Kit* kit = (Kit*)clip->output;
					kit->receivedPitchBendForKit(modelStackWithTimelineCounter, cable, match, channel, data1, data2,
					                             doingMidiThru);
				}
				else {
					MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
					melodicInstrument->receivedPitchBend(modelStackWithTimelineCounter, cable, match, channel, data1,
					                                     data2, doingMidiThru);
				}
			}
		}
	}
}

/// called from playback handler
/// determines whether aftertouch received is midi follow relevant
/// and should be routed to the active context for further processing
void MidiFollow::aftertouchReceived(MIDICable& cable, int32_t channel, int32_t value, int32_t noteCode,
                                    bool* doingMidiThru, ModelStack* modelStack) {
	MIDIMatchType match = checkMidiFollowMatch(cable, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		// obtain clip for active context
		Clip* clip = getActiveClip(modelStack);
		if (clip) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			if (modelStackWithTimelineCounter) {
				if (clip->output->type == OutputType::KIT) {
					Kit* kit = (Kit*)clip->output;
					kit->receivedAftertouchForKit(modelStackWithTimelineCounter, cable, match, channel, value, noteCode,
					                              doingMidiThru);
				}
				else {
					MelodicInstrument* melodicInstrument = (MelodicInstrument*)clip->output;
					melodicInstrument->receivedAftertouch(modelStackWithTimelineCounter, cable, match, channel, value,
					                                      noteCode, doingMidiThru);
				}
			}
		}
	}
}

/// obtain match to check if device is compatible with the midi follow channel
/// a valid match is passed through to the instruments for further evaluation
MIDIMatchType MidiFollow::checkMidiFollowMatch(MIDICable& cable, uint8_t channel) {
	MIDIMatchType m = MIDIMatchType::NO_MATCH;
	for (auto i = 0; i < kNumMIDIFollowChannelTypes; i++) {
		m = midiEngine.midiFollowChannelType[i].checkMatch(&cable, channel);
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
void MidiFollow::writeDefaultsToFile() {
	// MidiFollow.xml
	Error error = StorageManager::createXMLFile(MIDI_FOLLOW_XML, smSerializer, true);
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
	Serializer& writer = GetSerializer();
	for (int32_t ccNumber = 0; ccNumber <= kMaxMIDIValue; ccNumber++) {
		uint8_t soundParamId = ccToSoundParam[ccNumber];
		uint8_t globalParamId = ccToGlobalParam[ccNumber];

		bool writeTag = false;
		char const* paramNameSound;
		char const* paramNameGlobal;
		if (soundParamId != PARAM_ID_NONE && soundParamId < params::UNPATCHED_START) {
			paramNameSound = params::paramNameForFile(params::Kind::PATCHED, soundParamId);
			writeTag = true;
		}
		else if (soundParamId != PARAM_ID_NONE && soundParamId >= params::UNPATCHED_START) {
			paramNameSound = params::paramNameForFile(params::Kind::UNPATCHED_SOUND, soundParamId);
			writeTag = true;
		}
		if (writeTag) {
			char buffer[10];
			intToString(ccNumber, buffer);
			writer.writeTag(paramNameSound, buffer);
		}
		else {
			if (globalParamId != PARAM_ID_NONE) {
				paramNameGlobal =
				    params::paramNameForFile(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_START + globalParamId);
				writeTag = strcmp(paramNameGlobal, paramNameSound) != 0;
			}
			if (writeTag) {
				char buffer[10];
				intToString(ccNumber, buffer);
				writer.writeTag(paramNameGlobal, buffer);
			}
		}
	}
}

/// read defaults from XML
void MidiFollow::readDefaultsFromFile() {
	// no need to keep reading from SD card after first load
	if (successfullyReadDefaultsFromFile) {
		return;
	}

	init();

	FilePointer fp;
	// MIDIFollow.XML
	bool success = StorageManager::fileExists(MIDI_FOLLOW_XML, &fp);
	if (!success) {
		// since we changed the file path for the MIDIFollow.XML in c1.3, it's possible
		// that a MIDIFollow file may exists in the root of the SD card
		// if so, let's move it to the new SETTINGS folder (but first make sure folder exists)
		FRESULT result = f_mkdir(SETTINGS_FOLDER);
		if (result == FR_OK || result == FR_EXIST) {
			result = f_rename("MIDIFollow.XML", MIDI_FOLLOW_XML);
			if (result == FR_OK) {
				// this means we moved it
				// now let's open it
				success = StorageManager::fileExists(MIDI_FOLLOW_XML, &fp);
			}
		}
		if (!success) {
			writeDefaultsToFile();
			successfullyReadDefaultsFromFile = true;
			return;
		}
	}

	//<defaults>
	Error error = StorageManager::openXMLFile(&fp, smDeserializer, MIDI_DEFAULTS_TAG);
	if (error != Error::NONE) {
		writeDefaultsToFile();
		successfullyReadDefaultsFromFile = true;
		return;
	}

	// Clear mappings before reading the settings from the file
	clearMappings();

	Deserializer& reader = *activeDeserializer;
	char const* tagName;
	// step into the <defaultCCMappings> tag
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, MIDI_DEFAULTS_CC_TAG)) {
			readDefaultMappingsFromFile(reader);
		}
		reader.exitTag();
	}
	activeDeserializer->closeWriter();
	successfullyReadDefaultsFromFile = true;
}

/// compares param name tag to the list of params available are midi controllable
/// if param is found, it loads the CC mapping info for that param into the view
void MidiFollow::readDefaultMappingsFromFile(Deserializer& reader) {
	char const* tagName;
	bool foundParam;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		foundParam = false;
		int32_t value = reader.readTagOrAttributeValueInt();
		// Loop through patched sound params
		for (uint8_t paramId = 0; paramId < params::GLOBAL_NONE; paramId++) {
			if (!strcmp(tagName, params::paramNameForFile(params::Kind::PATCHED, paramId))) {
				soundParamToCC[paramId] = value;
				ccToSoundParam[value] = paramId;
				foundParam = true;
				break;
			}
		}
		if (!foundParam) {
			// Loop through unpatched sound params (if not found before)
			for (uint8_t paramId = 0; paramId < params::UNPATCHED_SOUND_MAX_NUM; paramId++) {
				if (!strcmp(tagName, params::paramNameForFile(params::Kind::UNPATCHED_SOUND,
				                                              params::UNPATCHED_START + paramId))) {
					soundParamToCC[params::UNPATCHED_START + paramId] = value;
					ccToSoundParam[value] = params::UNPATCHED_START + paramId;
					foundParam = true;
					break;
				}
			}
		}
		// Loop through global params
		for (uint8_t paramId = 0; paramId < params::UNPATCHED_GLOBAL_MAX_NUM; paramId++) {
			if (!strcmp(tagName,
			            params::paramNameForFile(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_START + paramId))) {
				globalParamToCC[paramId] = value;
				ccToGlobalParam[value] = paramId;
				break;
			}
		}
		// exit out of this tag so you can check the next tag
		reader.exitTag();
	}
}
