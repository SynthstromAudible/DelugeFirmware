/*
 * Copyright (c) 2024 Sean Ditny
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
#include "effects/global_effectable/params.h"
#include "gui/l10n/l10n.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "instruments/deluge/params.h"
#include "io/file.hpp"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_takeover.h"
#include "model/clip/clip_array.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/drum/drum.h"
#include "model/edits/edit.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_param_value.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/note/note_row.h"
#include "model/project_bridge/param_ids.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "playback/mode/session.h"
#include "playback/render_requests.h"
#include "processing/engines/audio_engine.h"
#include "storage/byte_channel.h"
#include "storage/existence_policy.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include "util/c_string.h"
#include "util/etl_string.h"
#include "util/notify.h"
#include <algorithm>
#include <cstdlib>

using namespace deluge;
using namespace gui;
using deluge::bridge::OwnerPlugin;
using deluge::bridge::RuntimeParam;
using deluge::project::ParamTarget;

#define SETTINGS_FOLDER "SETTINGS"
#define MIDI_FOLLOW_XML "SETTINGS/MIDIFollow.XML"
#define MIDI_DEFAULTS_TAG "defaults"
#define MIDI_DEFAULTS_SETTINGS_TAG "settings"
// channels
#define MIDI_DEFAULTS_SETTINGS_CHANNELS_TAG "channels"
#define MIDI_DEFAULTS_SETTINGS_CHANNEL_TAG "channel"
#define MIDI_DEFAULTS_SETTINGS_DEVICE_TAG "device"
// root note
#define MIDI_DEFAULTS_SETTINGS_KITROOTNOTE_TAG "kit_root_note"
#define MIDI_DEFAULTS_SETTINGS_KITROOTNOTE_NOTE_TAG "note"
#define MIDI_DEFAULTS_SETTINGS_FEEDBACK_TAG "feedback"
#define MIDI_DEFAULTS_SETTINGS_FEEDBACK_AUTOMATION_TAG "automation"
#define MIDI_DEFAULTS_SETTINGS_FEEDBACK_FILTER_TAG "filter"
#define MIDI_DEFAULTS_SETTINGS_DISPLAYPARAM_TAG "display_param"
#define MIDI_DEFAULTS_SETTINGS_DISPLAYPARAM_POPUP_TAG "popup"
#define MIDI_DEFAULTS_CC_TAG "cc_mappings"

namespace {

/// The runtime param a sound's target names, if any.
std::optional<RuntimeParam> soundParamOf(const std::optional<ParamTarget>& target) {
	return target ? deluge::bridge::runtimeParamOf(OwnerPlugin::INSTRUMENT, *target) : std::nullopt;
}

/// The runtime param a global effectable's target names, if any.
std::optional<RuntimeParam> globalParamOf(const std::optional<ParamTarget>& target) {
	return target ? deluge::bridge::runtimeParamOf(OwnerPlugin::GLOBAL_EFFECTABLE, *target) : std::nullopt;
}

} // namespace

constexpr int32_t MIDI_CC_MUTE = 89;
constexpr int32_t MIDI_CC_SOLO = 90;

EnumStringMap<MIDIFollowChannelType, kNumMIDIFollowChannelTypesIncludingTracks> channelTypeMap = {
    {{{MIDIFollowChannelType::A, "a"},
      {MIDIFollowChannelType::B, "b"},
      {MIDIFollowChannelType::C, "c"},
      {MIDIFollowChannelType::Track1, "track_1"},
      {MIDIFollowChannelType::Track2, "track_2"},
      {MIDIFollowChannelType::Track3, "track_3"},
      {MIDIFollowChannelType::Track4, "track_4"},
      {MIDIFollowChannelType::Track5, "track_5"},
      {MIDIFollowChannelType::Track6, "track_6"},
      {MIDIFollowChannelType::Track7, "track_7"},
      {MIDIFollowChannelType::Track8, "track_8"},
      {MIDIFollowChannelType::Track9, "track_9"},
      {MIDIFollowChannelType::Track10, "track_10"},
      {MIDIFollowChannelType::Track11, "track_11"},
      {MIDIFollowChannelType::Track12, "track_12"},
      {MIDIFollowChannelType::Track13, "track_13"},
      {MIDIFollowChannelType::Track14, "track_14"},
      {MIDIFollowChannelType::Track15, "track_15"},
      {MIDIFollowChannelType::Track16, "track_16"}}}};

EnumStringMap<MIDIFollowFeedbackChannelType, kNumMIDIFollowFeedbackChannelTypes> feedbackChannelTypeMap = {
    {{{MIDIFollowFeedbackChannelType::NONE, "none"},
      {MIDIFollowFeedbackChannelType::A, "a"},
      {MIDIFollowFeedbackChannelType::B, "b"},
      {MIDIFollowFeedbackChannelType::C, "c"},
      {MIDIFollowFeedbackChannelType::Track, "track"},
      {MIDIFollowFeedbackChannelType::TrackAndA, "track_a"},
      {MIDIFollowFeedbackChannelType::TrackAndB, "track_b"},
      {MIDIFollowFeedbackChannelType::TrackAndC, "track_c"}}}};

EnumStringMap<MIDIFollowFeedbackAutomationMode, kNumMIDIFollowFeedbackAutomationModes> feedbackAutomationModeMap = {
    {{{MIDIFollowFeedbackAutomationMode::DISABLED, "disabled"},
      {MIDIFollowFeedbackAutomationMode::LOW, "low"},
      {MIDIFollowFeedbackAutomationMode::MEDIUM, "medium"},
      {MIDIFollowFeedbackAutomationMode::HIGH, "high"}}}};

PLACE_SDRAM_BSS MidiFollow midiFollow{};

// initialize variables
MidiFollow::MidiFollow() {
	init();
}

void MidiFollow::init() {
	initState();
	initDefaultMappings();
}

void MidiFollow::factoryReset(bool showPopup) {
	if (showPopup) {
		deluge::notify::notice(l10n::String::STRING_FOR_RESET_MIDI_FOLLOW);
	}

	// May give an error (e.g. no such file), but there is no real consequence from that.
	(void)deluge::io::unlink(MIDI_FOLLOW_XML);
	for (auto& midiChannelType : midiEngine.midiFollowChannelType) {
		midiChannelType.clear();
	}
	midiEngine.midiFollowKitRootNote = 36;
	midiEngine.midiFollowDisplayParam = false;
	midiEngine.midiFollowFeedbackChannelType = MIDIFollowFeedbackChannelType::NONE;
	midiEngine.midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::DISABLED;
	midiEngine.midiFollowFeedbackFilter = false;
	successfullyReadDefaultsFromFile = false;
	readDefaultsFromFile();
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
	ccToSoundTarget.fill(std::nullopt);
	ccToGlobalTarget.fill(std::nullopt);

	soundParamToCC.fill(MIDI_CC_NONE);
	globalParamToCC.fill(MIDI_CC_NONE);
}

void MidiFollow::mapSound(int32_t cc, ParamTarget target) {
	soundParamToCC[target.id.value] = cc;
	ccToSoundTarget[cc] = target;
}

void MidiFollow::mapGlobal(int32_t cc, ParamTarget target) {
	globalParamToCC[*deluge::global_effectable::runtime_index(target.id)] = cc;
	ccToGlobalTarget[cc] = target;
}

void MidiFollow::initDefaultMappings() {
	namespace ip = deluge::instrument::param;
	namespace gp = deluge::global_effectable::param;
	auto mapSound = [this](int32_t cc, deluge::plugin::ParamId id) {
		this->mapSound(cc, deluge::project::engine_target(id.value));
	};
	auto mapGlobal = [this](int32_t cc, deluge::plugin::ParamId id) {
		this->mapGlobal(cc, deluge::project::engine_target(id.value));
	};

	clearMappings();

	// A sound's params
	mapSound(3, ip::kPitch);
	mapSound(5, ip::kPortamento);
	mapSound(7, ip::kVolumePostFX);
	mapSound(10, ip::kPan);
	mapSound(12, ip::kOscAPitch);
	mapSound(13, ip::kOscBPitch);
	mapSound(14, ip::kModulator1Pitch);
	mapSound(15, ip::kModulator2Pitch);
	mapSound(16, ip::kModFXRate);
	mapSound(17, ip::kModFXFeedback);
	mapSound(18, ip::kModFXOffset);
	mapSound(19, ip::kWaveFold);
	mapSound(20, ip::kStutterRate);
	mapSound(21, ip::kOscAVolume);
	mapSound(23, ip::kOscAPhaseWidth);
	mapSound(24, ip::kCarrier1Feedback);
	mapSound(25, ip::kOscAWavetablePosition);
	mapSound(26, ip::kOscBVolume);
	mapSound(27, ip::kCompressorThreshold);
	mapSound(28, ip::kOscBPhaseWidth);
	mapSound(29, ip::kCarrier2Feedback);
	mapSound(30, ip::kOscBWavetablePosition);
	mapSound(36, ip::kReverseProbability);
	mapSound(37, ip::kSpreadVelocity);
	mapSound(39, ip::kSpreadOctave);
	mapSound(40, ip::kSpreadGate);
	mapSound(41, ip::kNoiseVolume);
	mapSound(42, ip::kRhythm);
	mapSound(43, ip::kSequenceLength);
	mapSound(44, ip::kChordPolyphony);
	mapSound(45, ip::kRatchetAmount);
	mapSound(46, ip::kNoteProbability);
	mapSound(47, ip::kBassProbability);
	mapSound(48, ip::kChordProbability);
	mapSound(49, ip::kRatchetProbability);
	mapSound(50, ip::kArpGate);
	mapSound(51, ip::kArpRate);
	mapSound(52, ip::kDelayFeedback);
	mapSound(53, ip::kDelayRate);
	mapSound(54, ip::kModulator1Volume);
	mapSound(55, ip::kModulator1Feedback);
	mapSound(56, ip::kModulator2Volume);
	mapSound(57, ip::kModulator2Feedback);
	mapSound(58, ip::kLfo1Rate);
	mapSound(59, ip::kLfo2Rate);
	mapSound(60, ip::kCompressorShape);
	mapSound(62, ip::kBitcrushAmount);
	mapSound(63, ip::kSampleRateReduction);
	mapSound(70, ip::kLpfMorph);
	mapSound(71, ip::kLpfResonance);
	mapSound(72, ip::kEnv1Release);
	mapSound(73, ip::kEnv1Attack);
	mapSound(74, ip::kLpfFrequency);
	mapSound(75, ip::kEnv1Decay);
	mapSound(76, ip::kEnv1Sustain);
	mapSound(77, ip::kEnv2Attack);
	mapSound(78, ip::kEnv2Decay);
	mapSound(79, ip::kEnv2Sustain);
	mapSound(80, ip::kEnv2Release);
	mapSound(81, ip::kHpfFrequency);
	mapSound(82, ip::kHpfResonance);
	mapSound(83, ip::kHpfMorph);
	mapSound(84, ip::kBassFreq);
	mapSound(85, ip::kTrebleFreq);
	mapSound(86, ip::kBass);
	mapSound(87, ip::kTreble);
	mapSound(91, ip::kReverbAmount);
	mapSound(93, ip::kModFXDepth);
	mapSound(102, ip::kEnv3Attack);
	mapSound(103, ip::kEnv3Decay);
	mapSound(104, ip::kEnv3Sustain);
	mapSound(105, ip::kEnv3Release);
	mapSound(106, ip::kEnv4Attack);
	mapSound(107, ip::kEnv4Decay);
	mapSound(108, ip::kEnv4Sustain);
	mapSound(109, ip::kEnv4Release);
	mapSound(110, ip::kLfo3Rate);
	mapSound(111, ip::kLfo4Rate);
	mapSound(112, ip::kSwapProbability);
	mapSound(113, ip::kGlideProbability);

	// A global effectable's params, on the CCs of their sound counterparts
	mapGlobal(3, gp::kPitchAdjust);
	mapGlobal(7, gp::kVolume);
	mapGlobal(10, gp::kPan);
	mapGlobal(16, gp::kModFXRate);
	mapGlobal(17, gp::kModFXFeedback);
	mapGlobal(18, gp::kModFXOffset);
	mapGlobal(20, gp::kStutterRate);
	mapGlobal(51, gp::kArpRate);
	mapGlobal(52, gp::kDelayFeedback);
	mapGlobal(53, gp::kDelayRate);
	mapGlobal(60, gp::kCompressorShape);
	mapGlobal(61, gp::kSidechainCompressorVolume);
	mapGlobal(62, gp::kBitcrushAmount);
	mapGlobal(63, gp::kSampleRateReduction);
	mapGlobal(70, gp::kLpfMorph);
	mapGlobal(71, gp::kLpfResonance);
	mapGlobal(74, gp::kLpfFrequency);
	mapGlobal(81, gp::kHpfFrequency);
	mapGlobal(82, gp::kHpfResonance);
	mapGlobal(83, gp::kHpfMorph);
	mapGlobal(84, gp::kBassFreq);
	mapGlobal(85, gp::kTrebleFreq);
	mapGlobal(86, gp::kBass);
	mapGlobal(87, gp::kTreble);
	mapGlobal(91, gp::kReverbAmount);
	mapGlobal(93, gp::kModFXDepth);
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
		if (currentSong->persistentView.lastClipInstanceEnteredStartPos != -1) {
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

/// returns activeClip for the selected output
/// special case for note and performance data where you want to let notes,
/// midi modulation sources (e.g. mod wheel), and MPE through to the active clip
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

/// used to forward midi messages to specific tracks
const size_t MidiFollow::getTrackCount() const {
	size_t count = 0;
	Output* currentTrack = currentSong->live.media.first_output;
	while (currentTrack != nullptr) {
		if (currentTrack->getActiveClip() != nullptr) {
			++count;
		}
		currentTrack = currentTrack->next;
	}

	return count;
}

/// used to forward midi messages to specific tracks
Output* MidiFollow::getTrackFromIndex(uint32_t track_index, uint32_t maxTrack) {
	uint32_t count = 0;
	Output* currentTrack = currentSong->live.media.first_output;
	while (currentTrack != nullptr) {
		if (currentTrack->getActiveClip() != nullptr) {
			if (((maxTrack - 1) - count) == track_index) {
				return currentTrack;
			}

			++count;
		}
		currentTrack = currentTrack->next;
	}

	return nullptr;
}

/// based on the current context, as determined by clip returned from the getSelectedClip function
/// obtain the modelStackWithParam for that context and return it so it can be used by midi follow
ModelStackWithAutoParam*
MidiFollow::getModelStackWithParam(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                   Target sound, Target global, bool displayError) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	// non-null clip means you're dealing with the clip context
	if (clip) {
		if (modelStackWithTimelineCounter) {
			modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip, sound, global);
		}
	}

	if (displayError && (!modelStackWithParam || !modelStackWithParam->autoParam)) {
		displayParamControlError(sound, global);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                          Target sound, Target global) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	OutputType outputType = clip->output->type;

	switch (outputType) {
	case OutputType::SYNTH:
		modelStackWithParam = getModelStackWithParamForSynthClip(modelStackWithTimelineCounter, clip, sound);
		break;
	case OutputType::KIT:
		modelStackWithParam = getModelStackWithParamForKitClip(modelStackWithTimelineCounter, clip, sound, global);
		break;
	case OutputType::AUDIO:
		modelStackWithParam = getModelStackWithParamForAudioClip(modelStackWithTimelineCounter, clip, global);
		break;
	// explicit fallthrough cases
	case OutputType::CV:
	case OutputType::MIDI_OUT:
	case OutputType::NONE:;
	}

	return modelStackWithParam;
}

// MIDI follow's params are a sound's or a kit's own, never a cable's depth, so its lookups never create a cable.
ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForSynthClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                               Target sound) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	if (std::optional<RuntimeParam> param = soundParamOf(sound)) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam = clip->output->getModelStackWithParam(modelStackWithTimelineCounter, clip, param->index,
		                                                           param->kind, true, false, false);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForKitClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                             Target sound, Target global) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	InstrumentClip* instrumentClip = (InstrumentClip*)clip;

	std::optional<RuntimeParam> param = instrumentClip->affectEntire ? globalParamOf(global) : soundParamOf(sound);
	// don't allow control of Portamento in Kit's
	if (param && param->kind == params::Kind::UNPATCHED_SOUND && param->index == params::UNPATCHED_PORTAMENTO) {
		param = std::nullopt;
	}

	if (param) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam = clip->output->getModelStackWithParam(
		    modelStackWithTimelineCounter, clip, param->index, param->kind, instrumentClip->affectEntire, false, false);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForAudioClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                               Target global) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (std::optional<RuntimeParam> param = globalParamOf(global)) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam = clip->output->getModelStackWithParam(modelStackWithTimelineCounter, clip, param->index,
		                                                           param->kind, true, false, false);
	}

	return modelStackWithParam;
}

void MidiFollow::displayParamControlError(Target sound, Target global) {
	std::optional<RuntimeParam> param = sound ? soundParamOf(sound) : globalParamOf(global);

	if (param) {
		params::Kind paramKind = param->kind;
		int32_t paramID = param->index;
		etl::string<40> popupMsg;

		const char* name = deluge::modulation::params::getParamDisplayName(paramKind, paramID);
		if (name != l10n::get(l10n::String::STRING_FOR_NONE)) {
			popupMsg.append("Can't control: \n");
			popupMsg.append(name);
		}
		deluge::notify::notice(std::string_view(popupMsg));
	}
}

std::optional<uint8_t> MidiFollow::ccOf(OwnerPlugin owner, ParamTarget target) const {
	if (target.space != deluge::project::ParamSpace::ENGINE) {
		return std::nullopt;
	}
	uint8_t cc = MIDI_CC_NONE;
	if (owner == OwnerPlugin::INSTRUMENT) {
		// a cable's depth id is past the sound's params: no CC follows a cable
		if (target.id.value < soundParamToCC.size()) {
			cc = soundParamToCC[target.id.value];
		}
	}
	else if (std::optional<uint8_t> index = deluge::global_effectable::runtime_index(target.id)) {
		cc = globalParamToCC[*index];
	}
	if (cc == MIDI_CC_NONE) {
		return std::nullopt;
	}
	return cc;
}

/// Check if midi follow is controlling the global effectable context
/// if so, we should only be controlling the global effectable parameters
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

/// [interaction] The clip each note received was sent to, so its note-off can be sent to the same clip. Written and
/// read only by the interaction tier: MIDI input, clip deletion, and the interaction side of a song swap.
PLACE_SDRAM_DATA Clip* clipForLastNoteReceived[kMaxMIDIValue + 1] = {0};

void MidiFollow::forgetClipsNotOfCurrentSong() {
	deluge::playback::noteInteractionOwnedWrite(deluge::playback::InteractionOwned::MidiFollowStoredClips);
	for (Clip*& stored : clipForLastNoteReceived) {
		if (stored == nullptr) {
			continue;
		}
		const bool ofCurrentSong =
		    currentSong != nullptr
		    && (std::ranges::contains(currentSong->live.clips.session_clips, stored)
		        || std::ranges::contains(currentSong->live.clips.arrangement_only_clips, stored));
		if (!ofCurrentSong) {
			stored = nullptr;
		}
	}
}

void MidiFollow::removeClip(Clip* clip) {
	deluge::playback::noteInteractionOwnedWrite(deluge::playback::InteractionOwned::MidiFollowStoredClips);
	for (int32_t i = 0; i <= 127; i++) {
		if (clipForLastNoteReceived[i] == clip) {
			clipForLastNoteReceived[i] = nullptr;
		}
	}
}

/// called from playback handler
/// determines whether a note message received is midi follow relevant
/// and should be routed to the active context or a specific track for further processing
void MidiFollow::noteMessageReceived(MIDICable& cable, bool on, int32_t channel, int32_t note, int32_t velocity,
                                     bool* doingMidiThru, bool shouldRecordNotesNowNow, ModelStack* modelStack) {
	// first try to process note received through regular midi follow channels (A/B/C) against the selected / active
	// clip
	Output* selected_track = noteMessageReceivedForSelectedOrActiveClip(
	    cable, on, channel, note, velocity, doingMidiThru, shouldRecordNotesNowNow, modelStack);

	// logic for forwarding message to specific track
	// get number of tracks
	auto track_count = getTrackCount();

	if (track_count != 0) {
		// iterate through all the instruments
		for (int32_t track_index = 0; track_index < track_count; ++track_index) {
			// ignore track outputs > maximum number of midi follow track channels we have
			if (track_index >= kNumMIDIFollowChannelTrackTypes) {
				break;
			}
			Output* track = getTrackFromIndex(track_index, track_count);
			// if track is not null and we did not already send message to this track above
			if (track != nullptr && track != selected_track) {
				// next try to process note received through track midi follow channels (Track1-16)
				noteMessageReceivedForSpecificTrack(cable, on, channel, note, velocity, doingMidiThru,
				                                    shouldRecordNotesNowNow, modelStack, track, track_index);
			}
		}
	}
}

/// determines whether a midi note received is midi follow relevant
/// and should be routed to the active context for further processing
Output* MidiFollow::noteMessageReceivedForSelectedOrActiveClip(MIDICable& cable, bool on, int32_t channel, int32_t note,
                                                               int32_t velocity, bool* doingMidiThru,
                                                               bool shouldRecordNotesNowNow, ModelStack* modelStack) {
	Output* selected_track = nullptr;

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

			selected_track = sendNoteToClip(cable, clip, match, on, channel, note, velocity, doingMidiThru,
			                                shouldRecordNotesNowNow, modelStack);
		}
		// all notes off
		else if (note == ALL_NOTES_OFF) {
			for (int32_t i = 0; i <= 127; i++) {
				if (clipForLastNoteReceived[i]) {
					selected_track = sendNoteToClip(cable, clipForLastNoteReceived[i], match, on, channel, i, velocity,
					                                doingMidiThru, shouldRecordNotesNowNow, modelStack);
				}
			}
		}
	}
	return selected_track;
}

/// determines whether a midi note received is midi follow relevant
/// and should be routed to a specific track for further processing
void MidiFollow::noteMessageReceivedForSpecificTrack(MIDICable& cable, bool on, int32_t channel, int32_t note,
                                                     int32_t velocity, bool* doingMidiThru,
                                                     bool shouldRecordNotesNowNow, ModelStack* modelStack,
                                                     Output* specific_track, int32_t specific_track_index) {
	MIDIMatchType match = checkMidiFollowMatchForSpecificTrack(cable, channel, specific_track_index);
	if (match != MIDIMatchType::NO_MATCH) {
		// obtain active clip for specific track
		Clip* clip = specific_track->getActiveClip();

		if (note >= 0 && note <= 127) {
			sendNoteToClip(cable, clip, match, on, channel, note, velocity, doingMidiThru, shouldRecordNotesNowNow,
			               modelStack, false);
		}
		// all notes off
		else if (note == ALL_NOTES_OFF) {
			for (int32_t i = 0; i <= 127; i++) {
				sendNoteToClip(cable, clip, match, on, channel, i, velocity, doingMidiThru, shouldRecordNotesNowNow,
				               modelStack, false);
			}
		}
	}
}

Output* MidiFollow::sendNoteToClip(MIDICable& cable, Clip* clip, MIDIMatchType match, bool on, int32_t channel,
                                   int32_t note, int32_t velocity, bool* doingMidiThru, bool shouldRecordNotesNowNow,
                                   ModelStack* modelStack, bool updateClipForLastNoteReceived) {
	Output* selected_track = nullptr;

	// Only send if not muted - but let note-offs through always, for safety
	if (clip && (!on || currentSong->isOutputActiveInArrangement(clip->output))) {
		selected_track = clip->output;

		// ensure output is a kit or melodic instrument
		if (clip->type != ClipType::INSTRUMENT) {
			// if it's an audio clip, return the track so it can be skipped by specific track processing
			return selected_track;
		}

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
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
			// don't update if we're sending note to a specific track
			if (updateClipForLastNoteReceived) {
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
	return selected_track;
}

/// called from playback handler
/// determines whether a midi cc received is midi follow relevant
/// and should be routed to the active context or a specific track for further processing
void MidiFollow::midiCCReceived(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t ccValue,
                                bool* doingMidiThru, ModelStack* modelStack) {
	// first try to process cc received through regular midi follow channels (A/B/C) against the selected / active clip
	Output* selected_track =
	    midiCCReceivedForSelectedOrActiveClip(cable, channel, ccNumber, ccValue, doingMidiThru, modelStack);

	// logic for forwarding message to specific track
	// get number of tracks
	auto track_count = getTrackCount();

	if (track_count != 0) {
		// iterate through all the instruments
		for (int32_t track_index = 0; track_index < track_count; ++track_index) {
			// ignore track outputs > maximum number of midi follow track channels we have
			if (track_index >= kNumMIDIFollowChannelTrackTypes) {
				break;
			}
			Output* track = getTrackFromIndex(track_index, track_count);
			// if track is not null and we did not already send message to this track above
			if (track != nullptr && track != selected_track) {
				// next try to process cc received through track midi follow channels (Track1-16)
				midiCCReceivedForSpecificTrack(cable, channel, ccNumber, ccValue, doingMidiThru, modelStack, track,
				                               track_index);
			}
		}
	}
}

/// determines whether a midi cc received is midi follow relevant
/// and should be routed to the active context for further processing
Output* MidiFollow::midiCCReceivedForSelectedOrActiveClip(MIDICable& cable, uint8_t channel, uint8_t ccNumber,
                                                          uint8_t ccValue, bool* doingMidiThru,
                                                          ModelStack* modelStack) {
	Output* selected_track = nullptr;

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
			selected_track = clip->output;
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
						handleReceivedCC(cable, *modelStackWithTimelineCounter, clip, ccNumber, ccValue);
					}
				}
			}
		}
		// for these cc's, always use the active clip for the output selected
		clip = getActiveClip(modelStack);
		// these cc's are only relevant for instrument clips
		if (clip && clip->type == ClipType::INSTRUMENT) {
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
	return selected_track;
}

/// determines whether a midi cc received is midi follow relevant
/// and should be routed to a specific track for further processing
void MidiFollow::midiCCReceivedForSpecificTrack(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t ccValue,
                                                bool* doingMidiThru, ModelStack* modelStack, Output* specific_track,
                                                int32_t specific_track_index) {
	MIDIMatchType match = checkMidiFollowMatchForSpecificTrack(cable, channel, specific_track_index);
	if (match != MIDIMatchType::NO_MATCH) {
		// obtain active clip for specific track
		Clip* clip = specific_track->getActiveClip();

		bool isMIDIClip = false;
		bool isCVClip = false;

		if (clip != nullptr) {
			if (ccNumber == MIDI_CC_MUTE) {
				if (ccValue > 0) {
					session.toggleClipStatus(clip, nullptr, Buttons::isShiftButtonPressed(), kMIDIKeyInputLatency);
				}
				return;
			}
			else if (ccNumber == MIDI_CC_SOLO) {
				if (ccValue > 0) {
					session.soloClipAction(clip, Buttons::isShiftButtonPressed(), kMIDIKeyInputLatency);
				}
				return;
			}
			else if (specific_track->type == OutputType::MIDI_OUT) {
				isMIDIClip = true;
			}
			else if (specific_track->type == OutputType::CV) {
				isCVClip = true;
			}

			// don't offer to handleReceivedCC if it's a MIDI or CV Clip
			// this is because this function is used to control internal deluge parameters only (patched, unpatched)
			// midi/cv clip cc parameters are handled below in the offerReceivedCCToMelodicInstrument function
			if (!isMIDIClip && !isCVClip && (match == MIDIMatchType::MPE_MASTER || match == MIDIMatchType::CHANNEL)) {
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
							handleReceivedCC(cable, *modelStackWithTimelineCounter, clip, ccNumber, ccValue);
						}
					}
				}
			}
			if (clip->type == ClipType::INSTRUMENT) {
				ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
				if (modelStackWithTimelineCounter) {
					if (specific_track->type == OutputType::KIT) {
						Kit* kit = (Kit*)specific_track;
						kit->receivedCCForKit(modelStackWithTimelineCounter, cable, match, channel, ccNumber, ccValue,
						                      doingMidiThru, clip);
					}
					else {
						MelodicInstrument* melodicInstrument = (MelodicInstrument*)specific_track;
						melodicInstrument->receivedCC(modelStackWithTimelineCounter, cable, match, channel, ccNumber,
						                              ccValue, doingMidiThru);
					}
				}
			}
		}
	}
}

/// checks if the ccNumber received has been learned to any parameters in midi learning view
/// if the cc has been learned, it sets the new value for that parameter
/// this function works by first checking the active context to see if there is an active clip
/// to determine if the cc intends to control a song level or clip level parameter
void MidiFollow::handleReceivedCC(MIDICable& cable, ModelStackWithTimelineCounter& modelStackWithTimelineCounter,
                                  Clip* clip, int32_t ccNumber, int32_t ccValue) {

	int32_t modPos = 0;
	int32_t modLength = 0;
	bool isStepEditing = false;

	if (modelStackWithTimelineCounter.timelineCounterIsSet()) {
		TimelineCounter* timelineCounter = modelStackWithTimelineCounter.getTimelineCounter();

		// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
		// region.
		if (view.stepEditRegionHolds(*timelineCounter)) {
			modPos = view.modPos();
			modLength = view.modLength();
			isStepEditing = true;
		}

		timelineCounter->possiblyCloneForArrangementRecording(&modelStackWithTimelineCounter);
	}

	// directly access the parameter from the CC number
	const Target sound = ccToSoundTarget[ccNumber];
	const Target global = ccToGlobalTarget[ccNumber];
	if (!sound && !global) {
		// Abort
		return;
	}

	ModelStackWithAutoParam* modelStackWithParam =
	    getModelStackWithParam(&modelStackWithTimelineCounter, clip, sound, global, midiEngine.midiFollowDisplayParam);
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
		int32_t newKnobPos =
		    MidiTakeover::calculateKnobPos(cable, knobPos, ccValue, nullptr, true, ccNumber, isStepEditing);

		// is the cc being received for the same value as the current knob pos? If so, do nothing
		if (newKnobPos != knobPos) {
			// Convert the New Knob Position to a Parameter Value
			int32_t newValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

			// Set the new Parameter Value for the MIDI Learned Parameter
			deluge::edits::Context editCtx{*currentSong, currentSong->project};
			deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithParam), newValue, modPos,
			                                  modLength, modelStackWithParam};
			deluge::edits::apply(editCtx, edit);

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

MIDIFollowChannelType MidiFollow::getChannelTypeForTrackFeedback() {
	// obtain clip for active context
	Clip* clip = getSelectedOrActiveClip();

	// check if clip is valid
	if (clip) {
		// get track
		Output* selected_track = clip->output;

		// get number of tracks
		auto track_count = getTrackCount();

		if (track_count != 0) {
			// iterate through all the instruments
			for (int32_t track_index = 0; track_index < track_count; ++track_index) {
				// ignore track outputs > maximum number of midi follow track channels we have
				if (track_index >= kNumMIDIFollowChannelTrackTypes) {
					break;
				}
				Output* track = getTrackFromIndex(track_index, track_count);
				// if track is not null and track is the selected track
				if (track != nullptr && track == selected_track) {
					auto i = kNumMIDIFollowChannelTypes + track_index;
					if (i < kNumMIDIFollowChannelTypesIncludingTracks) {
						return static_cast<MIDIFollowChannelType>(i);
					}
				}
			}
		}
	}
	return MIDIFollowChannelType::NONE;
}

/// Add a configured MIDI Follow channel type to the feedback target list.
/// Returns false when the type is not a real feedback target, has not been learned, is a duplicate MIDI channel,
/// or would exceed the fixed feedback target list.
bool MidiFollow::addChannelTypeForFeedback(FeedbackChannelTypes& feedbackChannelTypes, size_t& numChannelTypes,
                                           MIDIFollowChannelType feedbackChannelType) {
	// Track feedback returns NONE when the selected clip does not belong to a configured track channel.
	if (feedbackChannelType == MIDIFollowChannelType::NONE) {
		return false;
	}

	auto channelTypeIndex = util::to_underlying(feedbackChannelType);
	// Only A/B/C and Track1-16 have LearnedMIDI slots.
	if (channelTypeIndex >= kNumMIDIFollowChannelTypesIncludingTracks) {
		return false;
	}

	LearnedMIDI& midiInput = midiEngine.midiFollowChannelType[channelTypeIndex];
	// A channel type with no learned MIDI channel cannot receive feedback.
	if (midiInput.channelOrZone == MIDI_CHANNEL_NONE) {
		return false;
	}

	// Combined feedback modes can resolve to the same MIDI channel through Track and A/B/C; send once.
	for (size_t i = 0; i < numChannelTypes; i++) {
		LearnedMIDI& existingMidiInput = midiEngine.midiFollowChannelType[util::to_underlying(feedbackChannelTypes[i])];
		if (existingMidiInput.channelOrZone == midiInput.channelOrZone) {
			return false;
		}
	}

	// Keep writes inside the fixed-size feedback target buffer.
	if (numChannelTypes >= feedbackChannelTypes.size()) {
		return false;
	}

	feedbackChannelTypes[numChannelTypes] = feedbackChannelType;
	numChannelTypes++;
	return true;
}

/// Resolve the configured MIDI Follow feedback mode into one or more learned channel types.
/// Combined Track + A/B/C modes add Track first, then the regular channel, so duplicate gating happens in order.
size_t MidiFollow::getChannelTypesForFeedback(FeedbackChannelTypes& feedbackChannelTypes) {
	feedbackChannelTypes.fill(MIDIFollowChannelType::NONE);

	size_t numChannelTypes = 0;
	MIDIFollowFeedbackChannelType feedbackChannelType = midiEngine.midiFollowFeedbackChannelType;

	switch (feedbackChannelType) {
	case MIDIFollowFeedbackChannelType::A:
		// Feedback is sent only to regular follow channel A when it is configured.
		addChannelTypeForFeedback(feedbackChannelTypes, numChannelTypes, MIDIFollowChannelType::A);
		break;

	case MIDIFollowFeedbackChannelType::B:
		// Feedback is sent only to regular follow channel B when it is configured.
		addChannelTypeForFeedback(feedbackChannelTypes, numChannelTypes, MIDIFollowChannelType::B);
		break;

	case MIDIFollowFeedbackChannelType::C:
		// Feedback is sent only to regular follow channel C when it is configured.
		addChannelTypeForFeedback(feedbackChannelTypes, numChannelTypes, MIDIFollowChannelType::C);
		break;

	case MIDIFollowFeedbackChannelType::Track:
		// Feedback follows the configured track channel for the selected clip.
		addChannelTypeForFeedback(feedbackChannelTypes, numChannelTypes, getChannelTypeForTrackFeedback());
		break;

	case MIDIFollowFeedbackChannelType::TrackAndA:
		// Track feedback has priority; channel A is added only if configured and not the same MIDI channel.
		addChannelTypeForFeedback(feedbackChannelTypes, numChannelTypes, getChannelTypeForTrackFeedback());
		addChannelTypeForFeedback(feedbackChannelTypes, numChannelTypes, MIDIFollowChannelType::A);
		break;

	case MIDIFollowFeedbackChannelType::TrackAndB:
		// Track feedback has priority; channel B is added only if configured and not the same MIDI channel.
		addChannelTypeForFeedback(feedbackChannelTypes, numChannelTypes, getChannelTypeForTrackFeedback());
		addChannelTypeForFeedback(feedbackChannelTypes, numChannelTypes, MIDIFollowChannelType::B);
		break;

	case MIDIFollowFeedbackChannelType::TrackAndC:
		// Track feedback has priority; channel C is added only if configured and not the same MIDI channel.
		addChannelTypeForFeedback(feedbackChannelTypes, numChannelTypes, getChannelTypeForTrackFeedback());
		addChannelTypeForFeedback(feedbackChannelTypes, numChannelTypes, MIDIFollowChannelType::C);
		break;

	case MIDIFollowFeedbackChannelType::NONE:
	case MIDIFollowFeedbackChannelType::INVALID:
		// Feedback is disabled, or the saved setting was not recognized.
		break;
	}

	return numChannelTypes;
}

/// called when updating the context,
/// e.g. switching from song to clip, changing instruments presets, peeking a clip in song view
/// this function:
/// 1) checks the active context
/// 2) sets up the model stack for that context
/// 3) checks what parameters have been learned and obtains the model stack for those params
/// 4) sends midi feedback of the current parameter value to the cc numbers learned to those parameters
void MidiFollow::sendCCWithoutModelStackForMidiFollowFeedback(bool isAutomation) {
	FeedbackChannelTypes feedbackChannelTypes;
	size_t numChannelTypes = getChannelTypesForFeedback(feedbackChannelTypes);
	if (numChannelTypes == 0) {
		return;
	}

	alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];

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
			if (view.stepEditRegionHolds(*timelineCounter)) {

				// don't send automation feedback if you're step editing
				if (isAutomation) {
					return;
				}

				modPos = view.modPos();
				isStepEditing = true;
			}
		}

		// loop through all params to see if any parameters have been learned
		for (int32_t ccNumber = 0; ccNumber <= kMaxMIDIValue; ccNumber++) {
			// obtain the model stack for the parameter that has been learned
			ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(
			    modelStackWithTimelineCounter, clip, ccToSoundTarget[ccNumber], ccToGlobalTarget[ccNumber], false);
			// check that model stack is valid
			if (modelStackWithParam && modelStackWithParam->autoParam) {
				if (!isAutomation
				    || (isAutomation && modelStackWithParam->autoParam->isAutomated(*modelStackWithParam->doc()))) {
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
					sendCCForMidiFollowFeedback(feedbackChannelTypes, numChannelTypes, ccNumber, knobPos);
				}
			}
		}
	}
}

/// Resolve the current MIDI Follow feedback configuration, then send this learned CC value to each target.
/// Called when updating parameter values using mod (gold) encoders or the select encoder in the soundEditor menu.
/// Also called when updating the context, e.g. switching from song to clip, changing instruments presets, peeking a
/// clip in song view.
void MidiFollow::sendCCForMidiFollowFeedback(int32_t ccNumber, int32_t knobPos) {
	FeedbackChannelTypes feedbackChannelTypes;
	size_t numChannelTypes = getChannelTypesForFeedback(feedbackChannelTypes);
	sendCCForMidiFollowFeedback(feedbackChannelTypes, numChannelTypes, ccNumber, knobPos);
}

/// Send this learned CC value to each resolved feedback target.
/// The target list is expected to have already been filtered for configured channels and duplicate MIDI channels.
void MidiFollow::sendCCForMidiFollowFeedback(FeedbackChannelTypes const& feedbackChannelTypes, size_t numChannelTypes,
                                             int32_t ccNumber, int32_t knobPos) {
	for (size_t i = 0; i < numChannelTypes; i++) {
		sendCCForMidiFollowFeedback(feedbackChannelTypes[i], ccNumber, knobPos);
	}
}

/// Send this learned CC value to a single MIDI Follow feedback target.
/// MPE zones send on their master channel while keeping the original channel/zone as the output filter.
void MidiFollow::sendCCForMidiFollowFeedback(MIDIFollowChannelType feedbackChannelType, int32_t ccNumber,
                                             int32_t knobPos) {
	LearnedMIDI& midiInput = midiEngine.midiFollowChannelType[util::to_underlying(feedbackChannelType)];

	int32_t channel = midiInput.channelOrZone;
	if (midiInput.isForMPEZone()) {
		channel = midiInput.getMasterChannel();
	}

	int32_t midiOutputFilter = midiInput.channelOrZone;

	midiEngine.sendCC(this, channel, ccNumber, knobPos + kKnobPosOffset, midiOutputFilter);

	timeLastCCSent[ccNumber] = AudioEngine::audioSampleTimer;
}

/// called from playback handler
/// determines whether a pitch bend received is midi follow relevant
/// and should be routed to the active context or a specific track for further processing
void MidiFollow::pitchBendReceived(MIDICable& cable, uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru,
                                   ModelStack* modelStack) {
	// first try to process pitch bend received through regular midi follow channels (A/B/C) against the selected /
	// active clip
	Output* selected_track =
	    pitchBendReceivedForSelectedOrActiveClip(cable, channel, data1, data2, doingMidiThru, modelStack);

	// logic for forwarding message to specific track
	// get number of tracks
	auto track_count = getTrackCount();

	if (track_count != 0) {
		// iterate through all the instruments
		for (int32_t track_index = 0; track_index < track_count; ++track_index) {
			// ignore track outputs > maximum number of midi follow track channels we have
			if (track_index >= kNumMIDIFollowChannelTrackTypes) {
				break;
			}
			Output* track = getTrackFromIndex(track_index, track_count);
			// if track is not null and we did not already send message to this track above
			if (track != nullptr && track != selected_track) {
				// next try to process pitch bend received through track midi follow channels (Track1-16)
				pitchBendReceivedForSpecificTrack(cable, channel, data1, data2, doingMidiThru, modelStack, track,
				                                  track_index);
			}
		}
	}
}

/// determines whether a pitch bend received is midi follow relevant
/// and should be routed to the active context for further processing
Output* MidiFollow::pitchBendReceivedForSelectedOrActiveClip(MIDICable& cable, uint8_t channel, uint8_t data1,
                                                             uint8_t data2, bool* doingMidiThru,
                                                             ModelStack* modelStack) {
	Output* selected_track = nullptr;

	MIDIMatchType match = checkMidiFollowMatch(cable, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		// obtain clip for active context
		Clip* clip = getActiveClip(modelStack);
		if (clip) {
			selected_track = clip->output;

			// ensure output is a kit or melodic instrument
			if (clip->type != ClipType::INSTRUMENT) {
				// if it's an audio clip, return the track so it can be skipped by specific track processing
				return selected_track;
			}

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
	return selected_track;
}

/// determines whether a pitch bend received is midi follow relevant
/// and should be routed to a specific track for further processing
void MidiFollow::pitchBendReceivedForSpecificTrack(MIDICable& cable, uint8_t channel, uint8_t data1, uint8_t data2,
                                                   bool* doingMidiThru, ModelStack* modelStack, Output* specific_track,
                                                   int32_t specific_track_index) {
	MIDIMatchType match = checkMidiFollowMatchForSpecificTrack(cable, channel, specific_track_index);
	if (match != MIDIMatchType::NO_MATCH) {
		// obtain active clip for specific track
		Clip* clip = specific_track->getActiveClip();
		if (clip && clip->type == ClipType::INSTRUMENT) {
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
/// and should be routed to the active context or a specific track for further processing
void MidiFollow::aftertouchReceived(MIDICable& cable, int32_t channel, int32_t value, int32_t noteCode,
                                    bool* doingMidiThru, ModelStack* modelStack) {
	// first try to process after touch received through regular midi follow channels (A/B/C) against the selected /
	// active clip
	Output* selected_track =
	    aftertouchReceivedForSelectedOrActiveClip(cable, channel, value, noteCode, doingMidiThru, modelStack);

	// logic for forwarding message to specific track
	// get number of tracks
	auto track_count = getTrackCount();

	if (track_count != 0) {
		// iterate through all the instruments
		for (int32_t track_index = 0; track_index < track_count; ++track_index) {
			// ignore track outputs > maximum number of midi follow track channels we have
			if (track_index >= kNumMIDIFollowChannelTrackTypes) {
				break;
			}
			Output* track = getTrackFromIndex(track_index, track_count);
			// if track is not null and we did not already send message to this track above
			if (track != nullptr && track != selected_track) {
				// next try to process after touch received through track midi follow channels (Track1-16)
				aftertouchReceivedForSpecificTrack(cable, channel, value, noteCode, doingMidiThru, modelStack, track,
				                                   track_index);
			}
		}
	}
}

/// determines whether aftertouch received is midi follow relevant
/// and should be routed to the active context for further processing
Output* MidiFollow::aftertouchReceivedForSelectedOrActiveClip(MIDICable& cable, int32_t channel, int32_t value,
                                                              int32_t noteCode, bool* doingMidiThru,
                                                              ModelStack* modelStack) {
	Output* selected_track = nullptr;

	MIDIMatchType match = checkMidiFollowMatch(cable, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		// obtain clip for active context
		Clip* clip = getActiveClip(modelStack);
		if (clip) {
			selected_track = clip->output;

			// ensure output is a kit or melodic instrument
			if (clip->type != ClipType::INSTRUMENT) {
				// if it's an audio clip, return the track so it can be skipped by specific track processing
				return selected_track;
			}

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
	return selected_track;
}

/// determines whether aftertouch received is midi follow relevant
/// and should be routed to a specific track for further processing
void MidiFollow::aftertouchReceivedForSpecificTrack(MIDICable& cable, int32_t channel, int32_t value, int32_t noteCode,
                                                    bool* doingMidiThru, ModelStack* modelStack, Output* specific_track,
                                                    int32_t specific_track_index) {
	MIDIMatchType match = checkMidiFollowMatchForSpecificTrack(cable, channel, specific_track_index);
	if (match != MIDIMatchType::NO_MATCH) {
		// obtain active clip for specific track
		Clip* clip = specific_track->getActiveClip();
		if (clip && clip->type == ClipType::INSTRUMENT) {
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
/// used with regular midi follow channels A/B/C
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

/// obtain match to check if device is compatible with the midi follow channel
/// a valid match is passed through to the instruments for further evaluation
/// used with midi follow channels for specific tracks 1-16
MIDIMatchType MidiFollow::checkMidiFollowMatchForSpecificTrack(MIDICable& cable, uint8_t channel,
                                                               int32_t specific_track_index) {
	MIDIMatchType m = MIDIMatchType::NO_MATCH;
	auto i = kNumMIDIFollowChannelTypes + specific_track_index;
	if (i < kNumMIDIFollowChannelTypesIncludingTracks) {
		m = midiEngine.midiFollowChannelType[i].checkMatch(&cable, channel);
		if (m != MIDIMatchType::NO_MATCH) {
			return m;
		}
	}
	return m;
}

bool MidiFollow::isFeedbackEnabled() {
	FeedbackChannelTypes feedbackChannelTypes;
	return getChannelTypesForFeedback(feedbackChannelTypes) != 0;
}

/// create default XML file and write defaults
/// I should check if file exists before creating one
void MidiFollow::writeDefaultsToFile() {
	if (deluge::storage::bytes::usingByteChannel()) {
		// Serialise into memory -- no card contact at all -- and hand the bytes over. The serialiser
		// dies at the end of this scope, which is safe because `submitWrite` copies.
		XMLSerializer writer;
		writer.setMemoryBased();
		writer.write(StorageManager::kXmlDocumentHead); // the inline arm gets this from createXMLFile
		serialiseDefaults(writer);
		if (deluge::storage::bytes::submitWrite(MIDI_FOLLOW_XML, writer.getBufferPtr(),
		                                        static_cast<uint32_t>(writer.bytesWritten()),
		                                        &MidiFollow::defaultsWritten, this)) {
			return;
		}
		// Not taken: too large for the channel (a busy channel queues instead). The body below runs only where
		// blocking is legal; after boot it is refused and nothing is written.
	}
	(void)deluge::storage::bytes::runDuringBoot(
	    [](void* ctx) {
		    auto* self = static_cast<MidiFollow*>(ctx);
		    self->completeLegacyFlashMigration(self->writeDefaultsInline());
	    },
	    this);
}

void MidiFollow::defaultsWritten(void* ctx, deluge::storage::bytes::SettingsOutcome outcome) {
	static_cast<MidiFollow*>(ctx)->completeLegacyFlashMigration(outcome
	                                                            == deluge::storage::bytes::SettingsOutcome::Written);
}

bool MidiFollow::writeDefaultsInline() {
	deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Settings);
	// MidiFollow.xml
	Error error = StorageManager::createXMLFile(MIDI_FOLLOW_XML, smSerializer, true);
	if (error != Error::NONE) {
		return false;
	}
	serialiseDefaults(GetSerializer());
	return GetSerializer().closeFileAfterWriting() == Error::NONE;
}

void MidiFollow::serialiseDefaults(Serializer& writer) {
	//<defaults>
	writer.writeOpeningTagBeginning(MIDI_DEFAULTS_TAG);
	writer.writeOpeningTagEnd();

	//<cc_mappings>
	writer.writeOpeningTagBeginning(MIDI_DEFAULTS_CC_TAG);
	writer.writeOpeningTagEnd();
	writeDefaultMappingsToFile(writer);
	writer.writeClosingTag(MIDI_DEFAULTS_CC_TAG);

	//<settings>
	writer.writeOpeningTagBeginning(MIDI_DEFAULTS_SETTINGS_TAG);
	writer.writeOpeningTagEnd();
	writeDefaultSettingsToFile(writer);
	writer.writeClosingTag(MIDI_DEFAULTS_SETTINGS_TAG);

	writer.writeClosingTag(MIDI_DEFAULTS_TAG);
}

/// convert paramID to a paramName to write to XML
void MidiFollow::writeDefaultMappingsToFile(Serializer& writer) {
	for (int32_t ccNumber = 0; ccNumber <= kMaxMIDIValue; ccNumber++) {
		const std::optional<RuntimeParam> sound = soundParamOf(ccToSoundTarget[ccNumber]);
		const std::optional<RuntimeParam> global = globalParamOf(ccToGlobalTarget[ccNumber]);

		bool writeTag = false;
		char const* paramNameSound = nullptr;
		char const* paramNameGlobal;
		if (sound && sound->kind == params::Kind::PATCHED) {
			paramNameSound = params::paramNameForFile(params::Kind::PATCHED, sound->index, true);
			writeTag = true;
		}
		else if (sound) {
			paramNameSound =
			    params::paramNameForFile(params::Kind::UNPATCHED_SOUND, params::UNPATCHED_START + sound->index, true);
			writeTag = true;
		}
		if (writeTag) {
			char buffer[10];
			intToString(ccNumber, buffer);
			writer.writeTag(paramNameSound, buffer);
		}
		else {
			if (global) {
				paramNameGlobal = params::paramNameForFile(params::Kind::UNPATCHED_GLOBAL,
				                                           params::UNPATCHED_START + global->index, true);
				// Reaching here means no sound param claimed this CC (paramNameSound is still null), so the global
				// mapping should be written. Only compare names when a sound name actually exists, to avoid passing a
				// null pointer to strcmp.
				writeTag = (paramNameSound == nullptr) || strcmp(paramNameGlobal, paramNameSound) != 0;
			}
			if (writeTag) {
				char buffer[10];
				intToString(ccNumber, buffer);
				writer.writeTag(paramNameGlobal, buffer);
			}
		}
	}
}

/// write default settings (channels, kit root note, feedback, display param) to XML
void MidiFollow::writeDefaultSettingsToFile(Serializer& writer) {
	//<channels>
	writer.writeOpeningTagBeginning(MIDI_DEFAULTS_SETTINGS_CHANNELS_TAG);
	writer.writeOpeningTagEnd();

	writeChannelSettingsToFile(writer);

	writer.writeClosingTag(MIDI_DEFAULTS_SETTINGS_CHANNELS_TAG);

	//<kit_root_note>
	writer.writeOpeningTagBeginning(MIDI_DEFAULTS_SETTINGS_KITROOTNOTE_TAG);
	writer.writeOpeningTagEnd();

	writeKitRootNoteSettingToFile(writer);

	writer.writeClosingTag(MIDI_DEFAULTS_SETTINGS_KITROOTNOTE_TAG);

	//<feedback>
	writer.writeOpeningTagBeginning(MIDI_DEFAULTS_SETTINGS_FEEDBACK_TAG);
	writer.writeOpeningTagEnd();

	writeFeedbackSettingsToFile(writer);

	writer.writeClosingTag(MIDI_DEFAULTS_SETTINGS_FEEDBACK_TAG);

	//<display_param>
	writer.writeOpeningTagBeginning(MIDI_DEFAULTS_SETTINGS_DISPLAYPARAM_TAG);
	writer.writeOpeningTagEnd();

	writeDisplayParamSettingToFile(writer);

	writer.writeClosingTag(MIDI_DEFAULTS_SETTINGS_DISPLAYPARAM_TAG);
}

/// Write Channel settings to XML
void MidiFollow::writeChannelSettingsToFile(Serializer& writer) {
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::A);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::B);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::C);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track1);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track2);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track3);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track4);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track5);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track6);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track7);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track8);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track9);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track10);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track11);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track12);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track13);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track14);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track15);
	writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::Track16);
}

/// Write Specific Channel settings to XML
void MidiFollow::writeSpecificChannelSettingsToFile(Serializer& writer, MIDIFollowChannelType type) {
	char const* tag_name = getNameFromChannelType(type);

	writer.writeOpeningTagBeginning(tag_name);
	writer.writeOpeningTagEnd();

	writer.writeTag(MIDI_DEFAULTS_SETTINGS_CHANNEL_TAG,
	                midiEngine.midiFollowChannelType[util::to_underlying(type)].channelOrZone + 1);

	MIDICable* cable = midiEngine.midiFollowChannelType[util::to_underlying(type)].cable;

	if (cable != nullptr) {
		// write reference inside <device> tag
		cable->writeReferenceToFile(writer, MIDI_DEFAULTS_SETTINGS_DEVICE_TAG);
	}

	writer.writeClosingTag(tag_name);
}

/// Write Kit Root Note setting to XML
void MidiFollow::writeKitRootNoteSettingToFile(Serializer& writer) {
	char note[10];
	intToString(midiEngine.midiFollowKitRootNote, note);
	writer.writeTag(MIDI_DEFAULTS_SETTINGS_KITROOTNOTE_NOTE_TAG, note);
}

/// Write Feedback settings to XML
void MidiFollow::writeFeedbackSettingsToFile(Serializer& writer) {
	writer.writeTag(MIDI_DEFAULTS_SETTINGS_CHANNEL_TAG,
	                getNameFromFeedbackChannelType(midiEngine.midiFollowFeedbackChannelType));
	writer.writeTag(MIDI_DEFAULTS_SETTINGS_FEEDBACK_AUTOMATION_TAG,
	                getNameFromFeedbackAutomationMode(midiEngine.midiFollowFeedbackAutomation));
	writer.writeTag(MIDI_DEFAULTS_SETTINGS_FEEDBACK_FILTER_TAG, getNameFromBool(midiEngine.midiFollowFeedbackFilter));
}

/// Write Display Param setting to XML
void MidiFollow::writeDisplayParamSettingToFile(Serializer& writer) {
	writer.writeTag(MIDI_DEFAULTS_SETTINGS_DISPLAYPARAM_POPUP_TAG, getNameFromBool(midiEngine.midiFollowDisplayParam));
}

/// Close out the pre-c1.3 flash migration started by FlashStorage::readSettings().
///
/// Stamping this firmware's version into flash is what stops readSettings() migrating again on the next boot - it is
/// not otherwise written until the user happens to exit the settings menu, so without this the migration would repeat
/// on every boot and keep overwriting the <settings> block of MIDIFollow.XML. That same write zeroes the legacy bytes
/// 126-145, so it must not happen until the recovered values are safely on the card: if the XML write failed (no card,
/// card full, write-protected) we leave both the flash bytes and the pending flag alone and retry next boot.
void MidiFollow::completeLegacyFlashMigration(bool defaultsWereWritten) {
	if (!FlashStorage::areLegacyMidiFollowSettingsPending() || !defaultsWereWritten) {
		return;
	}
	FlashStorage::clearLegacyMidiFollowSettingsPending();
	FlashStorage::writeSettings();
}

/// read defaults from XML
bool MidiFollow::readDefaultsFromFile(deluge::storage::bytes::DoneFn onDone) {
	if (deluge::storage::bytes::usingByteChannel()) {
		// The early-out and the in-memory defaults happen HERE, before anything is submitted, because
		// they are exactly what must be true whether the file turns up or not -- the inline arm sets
		// them up before its own read for the same reason.
		if (successfullyReadDefaultsFromFile) {
			return false; // nothing submitted, so nothing will report
		}
		resetToDefaults();
		if (deluge::storage::bytes::submitRead(MIDI_FOLLOW_XML, /*legacyPath=*/nullptr, &MidiFollow::parseDefaultsBytes,
		                                       &MidiFollow::writeDefaultsAbsent, this, onDone)) {
			return true;
		}
		// Not taken (a busy channel queues instead): the body below runs only where blocking is legal, which
		// here is nowhere, so the defaults set up above stand.
	}
	// No blocking arm -- see readSettingsFromFile for why. The defaults set up above stand.
	return false;
}

void MidiFollow::parseDefaultsBytes(void* ctx, char* bytes, uint32_t len) {
	// `activeDeserializer` goes back on EVERY path, the failed open included -- see
	// `RuntimeFeatureSettings::parseSettingsBytes`, which carries the full reasoning.
	auto* self = static_cast<MidiFollow*>(ctx);
	FileDeserializer* previous = activeDeserializer;
	if (StorageManager::openDelugeBuffer(std::span{reinterpret_cast<uint8_t*>(bytes), len}, false, MIDI_DEFAULTS_TAG)
	    == Error::NONE) {
		self->parseDefaults(*activeDeserializer);
		self->successfullyReadDefaultsFromFile = true;
		activeDeserializer = previous;
		// Write the migrated settings back out over the <settings> block parseDefaults skipped
		if (FlashStorage::areLegacyMidiFollowSettingsPending()) {
			self->writeDefaultsToFile();
		}
		return;
	}
	// A document that will not open is treated exactly as the inline arm treats a failed
	// `openXMLFile`: write the defaults over it. Not a silent give-up -- that path is
	// pre-existing behaviour, preserved.
	activeDeserializer = previous;
	self->writeDefaultsToFile();
	self->successfullyReadDefaultsFromFile = true;
}

void MidiFollow::writeDefaultsAbsent(void* ctx) {
	// Only a genuinely absent file reaches here, never an unreadable card -- see
	// `deluge::storage::bytes::AbsentFn`.
	//
	// ⚠️ This does not write; the name is kept only because it is the `AbsentFn` the read
	// registers. **A read must not mutate.** Writing the defaults file from a read's completion made
	// the next run see a different card: save-roundtrip's SL-EVICT compares a clamped-heap run
	// against an unclamped one byte-for-byte, and on a cold card the first run created the file
	// while the second parsed it -- different channel traffic, different eviction, different bytes.
	// Deferring the write was tried and does not help: the asymmetry is the file's EXISTENCE, not
	// the write's timing.
	//
	// Nothing is lost. The defaults in memory are already the configuration, and the file is written
	// by the path that should write it -- `sound_editor.cpp` when the user edits a setting.
	//
	// The one exception is a pre-c1.3 save whose MIDI-follow settings FlashStorage::readSettings() recovered from
	// flash: those exist nowhere else, and the flash bytes holding them are zeroed by the next settings write, so
	// they go to the card now. Only an upgrade from c1.1/c1.2 flash takes this, never a test run.
	auto* self = static_cast<MidiFollow*>(ctx);
	self->successfullyReadDefaultsFromFile = true;
	if (FlashStorage::areLegacyMidiFollowSettingsPending()) {
		self->writeDefaultsToFile();
	}
}

void MidiFollow::readDefaultsInline() {
	deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Settings);
	// no need to keep reading from SD card after first load
	if (successfullyReadDefaultsFromFile) {
		return;
	}

	resetToDefaults();

	// MIDIFollow.XML
	switch (deluge::storage::bootstrap_action(StorageManager::fileExists(MIDI_FOLLOW_XML))) {
	case deluge::storage::Bootstrap::Load:
		break; // fall through to parsing, exactly as today

	case deluge::storage::Bootstrap::WriteDefaults:
		// Genuinely absent: if file doesn't exist, lets make SETTINGS folder if it doesn't already
		// exist.
		{
			auto result = deluge::io::mkdir(SETTINGS_FOLDER);
			if (result.has_value() || result.error() == deluge::io::Status::EXISTS) {
				// folder eixsts now, write defaults
				// (this also persists any settings migrated out of flash by FlashStorage::readSettings())
				writeDefaultsToFile();
				successfullyReadDefaultsFromFile = true;
				return;
			}
			// mkdir failed: falls through to openXMLFile below for a file we just decided does not
			// exist. Pre-existing behaviour, preserved as-is (not fixed here).
		}
		break;

	case deluge::storage::Bootstrap::UseInMemoryOnly:
		// Existence unknown: use in-memory defaults for this session and write nothing -- the file
		// may well be there, and overwriting it would lose the user's settings.
		successfullyReadDefaultsFromFile = false;
		return;
	}

	//<defaults>
	Error error = StorageManager::openDocumentSynchronously(MIDI_FOLLOW_XML, MIDI_DEFAULTS_TAG);
	if (error != Error::NONE) {
		writeDefaultsToFile();
		successfullyReadDefaultsFromFile = true;
		return;
	}

	parseDefaults(*activeDeserializer);
	activeDeserializer->closeWriter();
	StorageManager::releaseDocumentStaging();
	successfullyReadDefaultsFromFile = true;

	// Write the migrated settings back out over the <settings> block parseDefaults skipped
	if (FlashStorage::areLegacyMidiFollowSettingsPending()) {
		writeDefaultsToFile();
	}
}

void MidiFollow::resetToDefaults() {
	init();

	// Default MIDI-follow input: channel 1 on any device, as MidiEngine starts with, so incoming MIDI plays the
	// active instrument when there is no MIDIFollow.XML. A user's MIDIFollow.XML, when present, overrides channel A
	// when the settings below are read.
	// Not while settings recovered from a pre-c1.3 save are waiting to be persisted: channel A is one of them.
	if (FlashStorage::areLegacyMidiFollowSettingsPending()) {
		return;
	}
	LearnedMIDI& a = midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::A)];
	a.clear();
	a.channelOrZone = 0; // MIDI channel 1; cable left null = match any input device (omni)
}

void MidiFollow::parseDefaults(Deserializer& reader) {
	// Clear mappings before reading the settings from the file
	clearMappings();

	char const* tag_name;
	while (*(tag_name = reader.readNextTagOrAttributeName())) {
		// step into the <cc_mappings> tag
		if (!strcmp(tag_name, MIDI_DEFAULTS_CC_TAG)) {
			readDefaultMappingsFromFile(reader);
		}
		// step into the <settings> tag
		// (skipped while migrating a pre-c1.3 save - back then these settings lived in flash, so whatever this file
		// says about them is either absent or a default we wrote over the top of the real values)
		else if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_TAG)) {
			if (!FlashStorage::areLegacyMidiFollowSettingsPending()) {
				readDefaultSettingsFromFile(reader);
			}
		}
		reader.exitTag();
	}
}

/// compares param name tag to the list of params available are midi controllable
/// if param is found, it loads the CC mapping info for that param into the view
void MidiFollow::readDefaultMappingsFromFile(Deserializer& reader) {
	char const* tag_name;
	bool foundParam;
	while (*(tag_name = reader.readNextTagOrAttributeName())) {
		foundParam = false;
		int32_t value = reader.readTagOrAttributeValueInt();
		// Loop through patched sound params
		for (uint8_t paramId = 0; paramId < params::GLOBAL_NONE; paramId++) {
			if (!strcmp(tag_name, params::paramNameForFile(params::Kind::PATCHED, paramId, true))) {
				mapSound(value, *deluge::bridge::targetOf(OwnerPlugin::INSTRUMENT, params::Kind::PATCHED, paramId));
				foundParam = true;
				break;
			}
		}
		if (!foundParam) {
			// Loop through unpatched sound params (if not found before)
			for (uint8_t paramId = 0; paramId < params::UNPATCHED_SOUND_MAX_NUM; paramId++) {
				if (!strcmp(tag_name, params::paramNameForFile(params::Kind::UNPATCHED_SOUND,
				                                               params::UNPATCHED_START + paramId, true))) {
					mapSound(value, *deluge::bridge::targetOf(OwnerPlugin::INSTRUMENT, params::Kind::UNPATCHED_SOUND,
					                                          paramId));
					foundParam = true;
					break;
				}
			}
		}
		// Loop through global params
		for (uint8_t paramId = 0; paramId < params::UNPATCHED_GLOBAL_MAX_NUM; paramId++) {
			if (!strcmp(tag_name, params::paramNameForFile(params::Kind::UNPATCHED_GLOBAL,
			                                               params::UNPATCHED_START + paramId, true))) {
				mapGlobal(value, *deluge::bridge::targetOf(OwnerPlugin::GLOBAL_EFFECTABLE,
				                                           params::Kind::UNPATCHED_GLOBAL, paramId));
				break;
			}
		}
		// exit out of this tag so you can check the next tag
		reader.exitTag();
	}
}

/// read default settings (channels, kit root note, feedback, display param) from XML
void MidiFollow::readDefaultSettingsFromFile(Deserializer& reader) {
	char const* tag_name;
	while (*(tag_name = reader.readNextTagOrAttributeName())) {
		// step into <channels> tag
		if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_CHANNELS_TAG)) {
			readChannelSettingsFromFile(reader);
		}
		// step into <kit_root_note> tag
		else if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_KITROOTNOTE_TAG)) {
			readKitRootNoteSettingFromFile(reader);
		}
		// step into <feedback> tag
		else if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_FEEDBACK_TAG)) {
			readFeedbackSettingsFromFile(reader);
		}
		// step into <display_param> tag
		else if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_DISPLAYPARAM_TAG)) {
			readDisplayParamSettingFromFile(reader);
		}
		reader.exitTag();
	}
}

/// Read Channel settings from XML
void MidiFollow::readChannelSettingsFromFile(Deserializer& reader) {
	char const* tag_name;
	while (*(tag_name = reader.readNextTagOrAttributeName())) {
		// step into first channel type tag
		MIDIFollowChannelType type = getChannelTypeFromName(tag_name);
		if (type != MIDIFollowChannelType::INVALID) {
			readSpecificChannelSettingsFromFile(reader, type);
		}
		// exit so we can step into next tag
		reader.exitTag();
	}
}

/// Read Specific Channel settings from XML
void MidiFollow::readSpecificChannelSettingsFromFile(Deserializer& reader, MIDIFollowChannelType type) {
	char const* tag_name;
	while (*(tag_name = reader.readNextTagOrAttributeName())) {
		// step into <channel> tag
		if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_CHANNEL_TAG)) {
			int32_t value = reader.readTagOrAttributeValueInt();
			if (value >= 0 && value < NUM_CHANNELS) {
				midiEngine.midiFollowChannelType[util::to_underlying(type)].channelOrZone = value - 1;
			}
		}
		// step into <device> tag
		else if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_DEVICE_TAG)) {
			midiEngine.midiFollowChannelType[util::to_underlying(type)].cable =
			    MIDIDeviceManager::readDeviceReferenceFromFile(reader);
		}
		// exit so we can step into next tag
		reader.exitTag();
	}
}

/// Read Kit Root Note setting from XML
void MidiFollow::readKitRootNoteSettingFromFile(Deserializer& reader) {
	char const* tag_name;
	while (*(tag_name = reader.readNextTagOrAttributeName())) {
		// step into <note> tag
		if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_KITROOTNOTE_NOTE_TAG)) {
			int32_t value = reader.readTagOrAttributeValueInt();
			if (value >= 0 && value <= kMaxMIDIValue) {
				midiEngine.midiFollowKitRootNote = value;
			}
		}
		// exit so we can step into next tag
		reader.exitTag();
	}
}

/// Read Feedback settings from XML
void MidiFollow::readFeedbackSettingsFromFile(Deserializer& reader) {
	char const* tag_name;
	while (*(tag_name = reader.readNextTagOrAttributeName())) {
		// step into <channel> tag
		if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_CHANNEL_TAG)) {
			char const* value = reader.readTagOrAttributeValue();
			MIDIFollowFeedbackChannelType type = getFeedbackChannelTypeFromName(value);
			if (type != MIDIFollowFeedbackChannelType::INVALID) {
				midiEngine.midiFollowFeedbackChannelType = type;
			}
		}
		// step into <automation> tag
		else if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_FEEDBACK_AUTOMATION_TAG)) {
			char const* value = reader.readTagOrAttributeValue();
			MIDIFollowFeedbackAutomationMode mode = getFeedbackAutomationModeFromName(value);
			if (mode != MIDIFollowFeedbackAutomationMode::INVALID) {
				midiEngine.midiFollowFeedbackAutomation = mode;
			}
		}
		// step into <filter> tag
		else if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_FEEDBACK_FILTER_TAG)) {
			char const* value = reader.readTagOrAttributeValue();
			midiEngine.midiFollowFeedbackFilter = getBoolFromName(value);
		}
		// exit so we can step into next tag
		reader.exitTag();
	}
}

/// Read Display Param setting from XML
void MidiFollow::readDisplayParamSettingFromFile(Deserializer& reader) {
	char const* tag_name;
	while (*(tag_name = reader.readNextTagOrAttributeName())) {
		// step into <popup> tag
		if (!strcmp(tag_name, MIDI_DEFAULTS_SETTINGS_DISPLAYPARAM_POPUP_TAG)) {
			char const* value = reader.readTagOrAttributeValue();
			midiEngine.midiFollowDisplayParam = getBoolFromName(value);
		}
		// exit so we can step into next tag
		reader.exitTag();
	}
}

/// enum / string lookup functions

char const* MidiFollow::getNameFromChannelType(MIDIFollowChannelType type) {
	return channelTypeMap(type);
}

MIDIFollowChannelType MidiFollow::getChannelTypeFromName(char const* name) {
	return channelTypeMap(name, MIDIFollowChannelType::INVALID);
}

char const* MidiFollow::getNameFromFeedbackChannelType(MIDIFollowFeedbackChannelType type) {
	return feedbackChannelTypeMap(type);
}

MIDIFollowFeedbackChannelType MidiFollow::getFeedbackChannelTypeFromName(char const* name) {
	return feedbackChannelTypeMap(name, MIDIFollowFeedbackChannelType::INVALID);
}

char const* MidiFollow::getNameFromFeedbackAutomationMode(MIDIFollowFeedbackAutomationMode mode) {
	return feedbackAutomationModeMap(mode);
}

MIDIFollowFeedbackAutomationMode MidiFollow::getFeedbackAutomationModeFromName(char const* name) {
	return feedbackAutomationModeMap(name);
}

char const* MidiFollow::getNameFromBool(bool value) {
	return value == true ? "enabled" : "disabled";
}

bool MidiFollow::getBoolFromName(char const* name) {
	return !strcmp(name, "enabled");
}
