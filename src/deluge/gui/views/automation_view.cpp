/*
 * Copyright (c) 2023 Sean Ditny
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

#include "gui/views/automation_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/colour/palette.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/rename/rename_midi_cc_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation/editor_layout/mod_controllable.h"
#include "gui/views/automation/editor_layout/note.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/timeline_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/log.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "io/midi/midi_transpose.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_instrument_clip_multiply.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_note_row_horizontal_shift.h"
#include "model/consequence/consequence_note_row_length.h"
#include "model/consequence/consequence_note_row_mute.h"
#include "model/drum/drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/copied_note_row.h"
#include "model/note/note.h"
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/macros/macros.h"
#include "modulation/params/param.h"
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
#include "util/cfunctions.h"
#include "util/comparison.h"
#include "util/d_stringbuf.h"
#include "util/functions.h"
#include <new>
#include <string.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

namespace params = deluge::modulation::params;
using deluge::modulation::params::kNoParamID;
using deluge::modulation::params::ParamType;
using deluge::modulation::params::patchedParamShortcuts;
using deluge::modulation::params::unpatchedGlobalParamShortcuts;
using deluge::modulation::params::unpatchedNonGlobalParamShortcuts;

using namespace deluge::gui;

// If a macro lane is the selected automation param on this clip, return its instrument and (via
// macroIndex) which macro; else nullptr. Callers gate on being in the clip automation editor. Also
// null with the community feature off: a lane selection can outlive the feature toggle (it's
// deserialized with the clip), and none of the macro UI may act then.
static Output* macroLaneInstrument(Clip* clip, int32_t* macroIndex) {
	if (!Macros::isEnabled() || !clip) {
		return nullptr;
	}
	Output* instrument = Macros::macroHost(clip);
	if (!instrument) {
		return nullptr;
	}
	int32_t index =
	    Macros::macroIndexForLaneSelection(clip->output, clip->lastSelectedParamKind, clip->lastSelectedParamID);
	if (index < 0) {
		return nullptr;
	}
	if (macroIndex != nullptr) {
		*macroIndex = index;
	}
	return instrument;
}

const uint32_t auditionPadActionUIModes[] = {UI_MODE_NOTES_PRESSED,
                                             UI_MODE_AUDITIONING,
                                             UI_MODE_HORIZONTAL_SCROLL,
                                             UI_MODE_RECORD_COUNT_IN,
                                             UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON,
                                             0};

const uint32_t editPadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, 0};

const uint32_t mutePadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, 0};

const uint32_t verticalScrollUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN, 0};

// synth and kit rows FX - sorted in the order that Parameters are scrolled through on the display
const std::array<std::pair<params::Kind, ParamType>, kNumNonGlobalParamsForAutomation> nonGlobalParamsForAutomation{{
    // The four macro automation lanes scroll in first, mirroring MIDI clips (macros before CC 0)
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MACRO_1},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MACRO_2},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MACRO_3},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MACRO_4},
    // Master Volume, Pitch, Pan
    {params::Kind::PATCHED, params::GLOBAL_VOLUME_POST_FX},
    {params::Kind::PATCHED, params::LOCAL_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_PAN},
    // LPF Cutoff, Resonance, Morph
    {params::Kind::PATCHED, params::LOCAL_LPF_FREQ},
    {params::Kind::PATCHED, params::LOCAL_LPF_RESONANCE},
    {params::Kind::PATCHED, params::LOCAL_LPF_MORPH},
    // HPF Cutoff, Resonance, Morph
    {params::Kind::PATCHED, params::LOCAL_HPF_FREQ},
    {params::Kind::PATCHED, params::LOCAL_HPF_RESONANCE},
    {params::Kind::PATCHED, params::LOCAL_HPF_MORPH},
    // Bass, Bass Freq
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS_FREQ},
    // Treble, Treble Freq
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_TREBLE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_TREBLE_FREQ},
    // Reverb Amount
    {params::Kind::PATCHED, params::GLOBAL_REVERB_AMOUNT},
    // Delay Rate, Amount
    {params::Kind::PATCHED, params::GLOBAL_DELAY_RATE},
    {params::Kind::PATCHED, params::GLOBAL_DELAY_FEEDBACK},
    // Sidechain Shape
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SIDECHAIN_SHAPE},
    // Decimation, Bitcrush, Wavefolder
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SAMPLE_RATE_REDUCTION},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BITCRUSHING},
    {params::Kind::PATCHED, params::LOCAL_FOLD},
    // OSC 1 Volume, Pitch, Pulse Width, Carrier Feedback, Wave Index
    {params::Kind::PATCHED, params::LOCAL_OSC_A_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_OSC_A_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_OSC_A_PHASE_WIDTH},
    {params::Kind::PATCHED, params::LOCAL_CARRIER_0_FEEDBACK},
    {params::Kind::PATCHED, params::LOCAL_OSC_A_WAVE_INDEX},
    // OSC 2 Volume, Pitch, Pulse Width, Carrier Feedback, Wave Index
    {params::Kind::PATCHED, params::LOCAL_OSC_B_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_PHASE_WIDTH},
    {params::Kind::PATCHED, params::LOCAL_CARRIER_1_FEEDBACK},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_WAVE_INDEX},
    // FM Mod 1 Volume, Pitch, Feedback
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_FEEDBACK},
    // FM Mod 2 Volume, Pitch, Feedback
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_FEEDBACK},
    // Env 1 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_0_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_RELEASE},
    // Env 2 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_1_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_RELEASE},
    // Env 3 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_2_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_2_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_2_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_2_RELEASE},
    // Env 4 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_3_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_3_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_3_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_3_RELEASE},
    // LFO 1
    {params::Kind::PATCHED, params::GLOBAL_LFO_FREQ_1},
    // LFO 2
    {params::Kind::PATCHED, params::LOCAL_LFO_LOCAL_FREQ_1},
    // LFO 3
    {params::Kind::PATCHED, params::GLOBAL_LFO_FREQ_2},
    // LFO 4
    {params::Kind::PATCHED, params::LOCAL_LFO_LOCAL_FREQ_2},
    // Mod FX Offset, Feedback, Depth, Rate
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MOD_FX_OFFSET},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MOD_FX_FEEDBACK},
    {params::Kind::PATCHED, params::GLOBAL_MOD_FX_DEPTH},
    {params::Kind::PATCHED, params::GLOBAL_MOD_FX_RATE},
    // Arp Rate, Gate, Rhythm, Chord Polyphony, Sequence Length, Ratchet Amount, Note Prob, Bass Prob, Chord Prob,
    // Ratchet Prob, Spread Gate, Spread Octave, Spread Velocity
    {params::Kind::PATCHED, params::GLOBAL_ARP_RATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_GATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SPREAD_GATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SPREAD_OCTAVE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SPREAD_VELOCITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RATCHET_AMOUNT},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RATCHET_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_CHORD_POLYPHONY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_CHORD_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_NOTE_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_BASS_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SWAP_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_GLIDE_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_REVERSE_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RHYTHM},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SEQUENCE_LENGTH},
    // Noise
    {params::Kind::PATCHED, params::LOCAL_NOISE_VOLUME},
    // Portamento
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_PORTAMENTO},
    // Stutter Rate
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_STUTTER_RATE},
    // Compressor Threshold
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_COMPRESSOR_THRESHOLD},
    // Mono Expression: X - Pitch Bend
    {params::Kind::EXPRESSION, Expression::X_PITCH_BEND},
    // Mono Expression: Y - Mod Wheel
    {params::Kind::EXPRESSION, Expression::Y_SLIDE_TIMBRE},
    // Mono Expression: Z - Channel Pressure
    {params::Kind::EXPRESSION, Expression::Z_PRESSURE},
}};

// global FX - sorted in the order that Parameters are scrolled through on the display
// used with kit affect entire, audio clips, and arranger
const std::array<std::pair<params::Kind, ParamType>, kNumGlobalParamsForAutomation> globalParamsForAutomation{{
    // The four macro automation lanes scroll in first, mirroring the synth list (macros before params)
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_GLOBAL_MACRO_1},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_GLOBAL_MACRO_2},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_GLOBAL_MACRO_3},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_GLOBAL_MACRO_4},
    // Master Volume, Pitch, Pan
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_PITCH_ADJUST},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_PAN},
    // LPF Cutoff, Resonance
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_RES},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_MORPH},
    // HPF Cutoff, Resonance
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_HPF_FREQ},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_HPF_RES},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_HPF_MORPH},
    // Bass, Bass Freq
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BASS},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BASS_FREQ},
    // Treble, Treble Freq
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_TREBLE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_TREBLE_FREQ},
    // Reverb Amount
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_REVERB_SEND_AMOUNT},
    // Delay Rate, Amount
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_DELAY_RATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_DELAY_AMOUNT},
    // Sidechain Send, Shape
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SIDECHAIN_VOLUME},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SIDECHAIN_SHAPE},
    // Decimation, Bitcrush
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SAMPLE_RATE_REDUCTION},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BITCRUSHING},
    // Mod FX Offset, Feedback, Depth, Rate
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_OFFSET},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_FEEDBACK},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_DEPTH},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_RATE},
    // Stutter Rate
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE},
    // Compressor Threshold
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_COMPRESSOR_THRESHOLD},
    // Arp Rate, Gate, Rhythm, Chord Polyphony, Sequence Length, Ratchet Amount, Note Prob, Bass Prob, Chord Prob,
    // Ratchet Prob, Spread Gate, Spread Octave, Spread Velocity
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_GATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_SPREAD_GATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SPREAD_VELOCITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RATCHET_AMOUNT},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RATCHET_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_NOTE_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_BASS_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_SWAP_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_GLIDE_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_REVERSE_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RHYTHM},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_SEQUENCE_LENGTH},
}};

// The four GLOBAL macro lanes sit at the front of globalParamsForAutomation but are lane selections,
// not scrollable params, so the select-encoder scroll skips them: always on hosts that can't drive
// macros (the arranger, until Phase 4), and on macro-hosting clips (audio, kit) only while the feature
// is off - on, they stay scrollable, mirroring synth's nonGlobalParamsForAutomation. They remain
// reachable via the macro UI (gold-knob mode / lane editor).
static bool skipGlobalMacroLaneInScroll(ParamType id, bool hostCanDriveMacros) {
	return id >= params::UNPATCHED_GLOBAL_MACRO_1 && id <= params::UNPATCHED_GLOBAL_MACRO_4
	       && (!hostCanDriveMacros || !Macros::isEnabled());
}

// shortcuts for toggling interpolation and pad selection mode
constexpr uint8_t kInterpolationShortcutX = 0;
constexpr uint8_t kInterpolationShortcutY = 6;
constexpr uint8_t kPadSelectionShortcutX = 0;
constexpr uint8_t kPadSelectionShortcutY = 7;
constexpr uint8_t kVelocityShortcutX = 15;
constexpr uint8_t kVelocityShortcutY = 1;

PLACE_SDRAM_BSS AutomationView automationView{};

AutomationView::AutomationView() {

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

	// initialize automation view specific variables
	interpolation = true;
	interpolationBefore = false;
	interpolationAfter = false;
	// used to set parameter shortcut blinking
	parameterShortcutBlinking = false;
	// used to set interpolation shortcut blinking
	interpolationShortcutBlinking = false;
	// used to set pad selection shortcut blinking
	padSelectionShortcutBlinking = false;
	// used to enter pad selection mode
	padSelectionOn = false;
	multiPadPressSelected = false;
	multiPadPressActive = false;
	leftPadSelectedX = kNoSelection;
	leftPadSelectedY = kNoSelection;
	rightPadSelectedX = kNoSelection;
	rightPadSelectedY = kNoSelection;
	lastPadSelectedKnobPos = kNoSelection;
	onArrangerView = false;
	onMenuView = false;
	navSysId = NAVIGATION_CLIP;

	automationParamType = AutomationParamType::PER_SOUND;

	probabilityChanged = false;
	timeSelectKnobLastReleased = 0;
}

// called everytime you open up the automation view
bool AutomationView::opened() {
	initializeView();

	openedInBackground();

	focusRegained();

	return true;
}

void AutomationView::initializeView() {
	navSysId = getNavSysId();

	// grab the default setting for interpolation
	interpolation = FlashStorage::automationInterpolate;

	// re-initialize pad selection mode (so you start with the default automation editor)
	initPadSelection();

	// let the view know if we're dealing with an automation parameter or a note parameter
	setAutomationParamType();

	InstrumentClip* clip = getCurrentInstrumentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	if (!onArrangerView) {
		// only applies to instrument clips (not audio)
		if (clip) {
			// check if we for some reason, left the automation view, then switched clip types, then came back in
			// if you did that...reset the parameter selection and save the current parameter type selection
			// so we can check this again next time it happens
			if (outputType != clip->lastSelectedOutputType) {
				if (inAutomationEditor()) {
					initParameterSelection();
				}

				clip->lastSelectedOutputType = outputType;
			}

			// if we're in a kit, we want to make sure the param selected is valid for current context
			// e.g. only UNPATCHED_GLOBAL param kind's can be used with Kit Affect Entire enabled
			if ((outputType == OutputType::KIT) && (clip->lastSelectedParamKind != params::Kind::NONE)) {
				if (clip->lastSelectedParamKind == params::Kind::UNPATCHED_GLOBAL) {
					// A leftover macro lane selection must not resurrect affect-entire: kit macros only
					// apply while it's on (the user turned it off), so drop to the overview instead.
					if (!clip->affectEntire
					    && Macros::macroIndexForLaneSelection(output, clip->lastSelectedParamKind,
					                                          clip->lastSelectedParamID)
					           >= 0) {
						initParameterSelection();
					}
					else {
						clip->affectEntire = true;
					}
				}
				else {
					clip->affectEntire = false;
				}
			}

			// if you're not in note editor, turn led off if it's on
			if (clip->wrapEditing) {
				indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, inNoteEditor());
			}
		}
	}

	// if we're in the note editor and we're in a kit,
	// check that the lastAuditionedYDisplay is in sync with the selected drum
	if (inNoteEditor()) {
		if (outputType == OutputType::KIT) {
			potentiallyVerticalScrollToSelectedDrum(clip, output);
		}
	}
}

// Initializes some stuff to begin a new editing session
void AutomationView::focusRegained() {
	// clear any stale target quick-edit hold (and its persistent readout popup)
	if (heldTarget >= 0) {
		heldTarget = -1;
		heldTargetMacro = -1;
		display->cancelPopup();
	}
	if (onArrangerView) {
		indicator_leds::setLedState(IndicatorLED::BACK, false);
		indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);
		currentSong->affectEntire = true;
		view.focusRegained();
		view.setActiveModControllableTimelineCounter(currentSong);
	}
	else {
		ClipView::focusRegained();

		Clip* clip = getCurrentClip();
		if (clip->type == ClipType::AUDIO) {
			indicator_leds::setLedState(IndicatorLED::BACK, false);
			indicator_leds::setLedState(IndicatorLED::AFFECT_ENTIRE, true);
			view.focusRegained();
			view.setActiveModControllableTimelineCounter(clip);
		}
		else {
			// check if patch cable previously selected is still valid
			// if not we'll reset parameter selection and go back to overview
			if (clip->lastSelectedParamKind == params::Kind::PATCH_CABLE) {
				bool patchCableExists = false;
				ParamManagerForTimeline* paramManager = clip->getCurrentParamManager();
				if (paramManager) {
					PatchCableSet* set = paramManager->getPatchCableSetAllowJibberish();
					// make sure it's not jiberish
					if (set) {
						PatchSource s;
						ParamDescriptor destinationParamDescriptor;
						set->dissectParamId(clip->lastSelectedParamID, &destinationParamDescriptor, &s);
						if (set->getPatchCableIndex(s, destinationParamDescriptor) != kNoSelection) {
							patchCableExists = true;
						}
					}
				}
				if (!patchCableExists) {
					initParameterSelection();
				}
			}
			instrumentClipView.auditioningSilently = false; // Necessary?
			InstrumentClipMinder::focusRegained();
			instrumentClipView.setLedStates();
		}
	}

	// don't reset shortcut blinking if were still in the menu
	if (getCurrentUI() == &automationView) {
		// blink timer got reset by view.focusRegained() above
		parameterShortcutBlinking = false;
		interpolationShortcutBlinking = false;
		padSelectionShortcutBlinking = false;
		instrumentClipView.noteRowBlinking = false;
		// remove patch cable blink frequencies
		soundEditor.resetSourceBlinks();
		// possibly restablish parameter shortcut blinking (if parameter is selected)
		blinkShortcuts();
	}
	// re-show (or clear) the persistent Inactive status for the lane in view - UI transitions
	// cancel it, so returning here must restore it
	refreshMacroInactivePopup();
}

void AutomationView::openedInBackground() {
	Clip* clip = getCurrentClip();

	if (!onArrangerView) {
		// used when you're in song view / arranger view / keyboard view
		//(so it knows to come back to automation view)
		clip->onAutomationClipView = true;

		if (clip->type == ClipType::INSTRUMENT) {
			((InstrumentClip*)clip)->onKeyboardScreen = false;

			instrumentClipView.recalculateColours();
		}
	}

	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);

	AudioEngine::routineWithClusterLoading();

	AudioEngine::logAction("AutomationView::beginSession 2");

	if (renderingToStore) {
		renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight],
		               true);
		if (onArrangerView) {
			arrangerView.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
			                           &PadLEDs::occupancyMaskStore[kDisplayHeight]);
		}
		else {
			clip->renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
			                    &PadLEDs::occupancyMaskStore[kDisplayHeight]);
		}
	}
	else {
		uiNeedsRendering(&automationView);
	}

	// setup interpolation shortcut blinking when entering automation view from menu
	if (onMenuView && interpolation) {
		blinkInterpolationShortcut();
	}
}

// used for the play cursor
void AutomationView::graphicsRoutine() {
	if (onArrangerView) {
		arrangerView.graphicsRoutine();
	}
	else {
		if (getCurrentClip()->type == ClipType::AUDIO) {
			audioClipView.graphicsRoutine();
		}
		else {
			instrumentClipView.graphicsRoutine();
		}
	}
	// if we changed probability, then a pop-up may be currently stuck on display
	// if more than half a second has past since last knob turn, cancel the pop-up
	if (probabilityChanged
	    && ((uint32_t)(AudioEngine::audioSampleTimer - timeSelectKnobLastReleased) >= (kSampleRate / 2))) {
		display->cancelPopup();
		probabilityChanged = false;
	}
}

// used to return whether Automation View is in the AUTOMATION_ARRANGER_VIEW UI Type, AUTOMATION_INSTRUMENT_CLIP_VIEW or
// AUTOMATION_AUDIO_CLIP_VIEW UI Type
UIType AutomationView::getUIContextType() {
	if (onArrangerView) {
		return UIType::ARRANGER;
	}
	else {
		if (getCurrentClip()->type == ClipType::AUDIO) {
			return UIType::AUDIO_CLIP;
		}
		else {
			return UIType::INSTRUMENT_CLIP;
		}
	}
}

// rendering
bool AutomationView::possiblyRefreshAutomationEditorGrid(Clip* clip, params::Kind paramKind, int32_t paramID) {
	bool doRefreshGrid = false;
	if (clip && !automationView.onArrangerView) {
		if ((clip->lastSelectedParamID == paramID) && (clip->lastSelectedParamKind == paramKind)) {
			doRefreshGrid = true;
		}
	}
	else if (automationView.onArrangerView) {
		if ((currentSong->lastSelectedParamID == paramID) && (currentSong->lastSelectedParamKind == paramKind)) {
			doRefreshGrid = true;
		}
	}
	if (doRefreshGrid) {
		uiNeedsRendering(&automationView);
		return true;
	}
	return false;
}

// called whenever you call uiNeedsRendering(&automationView) somewhere else
// used to render automation overview, automation editor
// used to setup the shortcut blinking
bool AutomationView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {

	if (!image) {
		return true;
	}

	if (!occupancyMask) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
		return true;
	}

	PadLEDs::renderingLock = true;

	Clip* clip = getCurrentClip();
	if (!onArrangerView && clip->type == ClipType::INSTRUMENT) {
		instrumentClipView.recalculateColours();
	}

	// erase current occupancy mask as it will be refreshed
	memset(occupancyMask, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	performActualRender(image, occupancyMask, currentSong->xScroll[navSysId], currentSong->xZoom[navSysId],
	                    kDisplayWidth, kDisplayWidth + kSideBarWidth, drawUndefinedArea);

	PadLEDs::renderingLock = false;

	return true;
}

// determines whether you should render the automation editor, automation overview or just render some love <3
void AutomationView::performActualRender(RGB image[][kDisplayWidth + kSideBarWidth],
                                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll,
                                         uint32_t xZoom, int32_t renderWidth, int32_t imageWidth,
                                         bool drawUndefinedArea) {

	Clip* clip = getCurrentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	ModelStackWithNoteRow* modelStackWithNoteRow = nullptr;
	int32_t effectiveLength = 0;
	SquareInfo rowSquareInfo[kDisplayWidth];

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
		if (inNoteEditor()) {
			modelStackWithNoteRow = ((InstrumentClip*)clip)
			                            ->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
			                                                 modelStackWithTimelineCounter); // don't create
			effectiveLength = modelStackWithNoteRow->getLoopLength();
			if (modelStackWithNoteRow->getNoteRowAllowNull()) {
				NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
				noteRow->getRowSquareInfo(effectiveLength, rowSquareInfo);
			}
		}
	}

	if (!inNoteEditor()) {
		effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);
	}

	params::Kind kind = params::Kind::NONE;
	bool isBipolar = false;

	// if we have a valid model stack with param
	// get the param Kind and param bipolar status
	// so that it can be passed through the automation editor rendering
	// calls below
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		kind = modelStackWithParam->paramCollection->getParamKind();
		isBipolar = isParamBipolar(kind, modelStackWithParam->paramId);
	}

	// While a macro target quick-edit button is held, the main grid becomes the destination
	// picker: the parameter overview renders in place of the editor, with pickable pads lit.
	bool macroPicker = macroPickerActive(clip);

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		// only render if:
		// you're on arranger view
		// you're not in a CV clip type
		// you're not in a kit where you haven't selected a drum and you haven't selected affect entire either
		// you're not in a kit where no sound drum has been selected and you're not editing velocity
		// you're in a kit where midi or CV sound drum has been selected and you're editing velocity
		if (onArrangerView || !(outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)) {
			bool isMIDICVDrum = false;
			if (outputType == OutputType::KIT && !getAffectEntire()) {
				isMIDICVDrum = (((Kit*)output)->selectedDrum
				                && ((((Kit*)output)->selectedDrum->type == DrumType::MIDI)
				                    || (((Kit*)output)->selectedDrum->type == DrumType::GATE)));
			}

			// if parameter has been selected, show Automation Editor (unless a held quick-edit
			// button has flipped the grid into the macro destination picker)
			if (inAutomationEditor() && !isMIDICVDrum && !macroPicker) {
				automationEditorLayoutModControllable.renderAutomationEditor(
				    modelStackWithParam, clip, image, occupancyMask, renderWidth, xScroll, xZoom, effectiveLength,
				    xDisplay, drawUndefinedArea, kind, isBipolar);
			}

			// if note parameter has been selected, show Note Editor
			else if (inNoteEditor()) {
				automationEditorLayoutNote.renderNoteEditor(modelStackWithNoteRow, (InstrumentClip*)clip, image,
				                                            occupancyMask, renderWidth, xScroll, xZoom, effectiveLength,
				                                            xDisplay, drawUndefinedArea, rowSquareInfo[xDisplay]);
			}

			// if not editing a parameter, show Automation Overview
			else {
				renderAutomationOverview(modelStackWithTimelineCounter, modelStackWithThreeMainThings, clip, outputType,
				                         image, occupancyMask, xDisplay, isMIDICVDrum, macroPicker);
			}
		}
		else {
			PadLEDs::clearColumnWithoutSending(xDisplay);
		}
	}
}

// renders automation overview
void AutomationView::renderAutomationOverview(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                              ModelStackWithThreeMainThings* modelStackWithThreeMainThings, Clip* clip,
                                              OutputType outputType, RGB image[][kDisplayWidth + kSideBarWidth],
                                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
                                              bool isMIDICVDrum, bool macroPicker) {
	bool singleSoundDrum = (outputType == OutputType::KIT && !getAffectEntire()) && !isMIDICVDrum;
	bool affectEntireKit = (outputType == OutputType::KIT && getAffectEntire());
	// only ever true on a synth or MIDI clip, never a kit/arranger context
	Macros::Domain pickerDomain = macroPicker ? Macros::domainForOutput(clip->output) : Macros::Domain::MIDI;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		RGB& pixel = image[yDisplay][xDisplay];

		// The destination-picker overlay: assignable pads light grey; the pending pick lights white if
		// it's this pad's PRIMARY param, yellow if it's the shared second-layer param (matching the
		// second layer's colour elsewhere); pads that can't be a destination go dark.
		if (macroPicker) {
			int32_t destination = Macros::macroDestinationForPad(pickerDomain, xDisplay, yDisplay);
			if (destination < 0) {
				pixel = colours::black;
			}
			else {
				int32_t second = Macros::macroDestinationForPad(pickerDomain, xDisplay, yDisplay, true);
				if (heldTargetPendingDestination == destination) {
					pixel = colours::white_full; // primary layer picked
				}
				else if (second >= 0 && heldTargetPendingDestination == second) {
					pixel = colours::yellow; // second-layer param picked
				}
				else {
					pixel = colours::grey; // assignable, not the current pick
				}
				occupancyMask[yDisplay][xDisplay] = 64;
			}
			continue;
		}

		if (!isMIDICVDrum) {
			ModelStackWithAutoParam* modelStackWithParam = nullptr;

			if (!onArrangerView && (outputType == OutputType::SYNTH || singleSoundDrum)) {
				if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					modelStackWithParam =
					    getModelStackWithParamForClip(modelStackWithTimelineCounter, clip,
					                                  patchedParamShortcuts[xDisplay][yDisplay], params::Kind::PATCHED);
				}

				else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					// don't make portamento available for automation in kit rows
					if ((outputType == OutputType::KIT)
					    && (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == params::UNPATCHED_PORTAMENTO)) {
						pixel = colours::black; // erase pad
						continue;
					}

					modelStackWithParam = getModelStackWithParamForClip(
					    modelStackWithTimelineCounter, clip, unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay],
					    params::Kind::UNPATCHED_SOUND);
				}

				else if (params::isPatchCableShortcut(xDisplay, yDisplay)) {
					ParamDescriptor paramDescriptor;
					params::getPatchCableFromShortcut(xDisplay, yDisplay, &paramDescriptor);

					modelStackWithParam = getModelStackWithParamForClip(
					    modelStackWithTimelineCounter, clip, paramDescriptor.data, params::Kind::PATCH_CABLE);
				}
				// expression params, so sounds or midi/cv, or a single drum
				else if (params::expressionParamFromShortcut(xDisplay, yDisplay) != kNoParamID) {
					uint32_t paramID = params::expressionParamFromShortcut(xDisplay, yDisplay);
					if (paramID != kNoParamID) {
						modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip,
						                                                    paramID, params::Kind::EXPRESSION);
					}
				}
			}

			else if ((onArrangerView || (outputType == OutputType::AUDIO) || affectEntireKit)) {
				int32_t paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];
				if (paramID != kNoParamID) {
					if (onArrangerView) {
						// don't make pitch adjust or sidechain available for automation in arranger
						if ((paramID == params::UNPATCHED_PITCH_ADJUST)
						    || (paramID == params::UNPATCHED_SIDECHAIN_SHAPE)
						    || (paramID == params::UNPATCHED_SIDECHAIN_VOLUME)
						    || (paramID >= params::UNPATCHED_FIRST_ARP_PARAM
						        && paramID <= params::UNPATCHED_LAST_ARP_PARAM)
						    || (paramID == params::UNPATCHED_ARP_RATE)) {
							pixel = colours::black; // erase pad
							continue;
						}
						modelStackWithParam =
						    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, paramID);
					}
					else {
						if (outputType == OutputType::AUDIO
						    && ((paramID >= params::UNPATCHED_FIRST_ARP_PARAM
						         && paramID <= params::UNPATCHED_LAST_ARP_PARAM)
						        || paramID == params::UNPATCHED_ARP_RATE)) {
							pixel = colours::black; // erase pad
							continue;
						}
						modelStackWithParam =
						    getModelStackWithParamForClip(modelStackWithTimelineCounter, clip, paramID);
					}
				}
			}

			else if (outputType == OutputType::MIDI_OUT) {
				if (midiFollow.midiCCShortcutsForAutomation[xDisplay][yDisplay] != kNoParamID) {
					modelStackWithParam =
					    getModelStackWithParamForClip(modelStackWithTimelineCounter, clip,
					                                  midiFollow.midiCCShortcutsForAutomation[xDisplay][yDisplay]);
				}
			}
			else if (outputType == OutputType::CV) {
				uint32_t paramID = params::expressionParamFromShortcut(xDisplay, yDisplay);
				if (paramID != kNoParamID) {
					modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip, paramID,
					                                                    params::Kind::EXPRESSION);
				}
			}

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				// highlight pad white if the parameter it represents is currently automated
				if (modelStackWithParam->autoParam->isAutomated()) {
					pixel = {
					    .r = 130,
					    .g = 120,
					    .b = 130,
					};
				}

				else {
					pixel = colours::grey;
				}

				occupancyMask[yDisplay][xDisplay] = 64;
			}
			else {
				pixel = colours::black; // erase pad
			}
		}
		else {
			pixel = colours::black; // erase pad
		}
	}
}

// occupancyMask now optional
void AutomationView::renderUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay,
                                         RGB image[][kDisplayWidth + kSideBarWidth],
                                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t imageWidth,
                                         TimelineView* timelineView, bool tripletsOnHere, int32_t xDisplay) {
	// If the visible pane extends beyond the end of the Clip, draw it as grey
	int32_t greyStart = timelineView->getSquareFromPos(lengthToDisplay - 1, nullptr, xScroll, xZoom) + 1;

	if (greyStart < 0) {
		greyStart = 0; // This actually happened in a song of Marek's, due to another bug, but best to check
		               // for this
	}

	if (greyStart <= xDisplay) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			image[yDisplay][xDisplay] = colours::grey;
			occupancyMask[yDisplay][xDisplay] = 64;
		}
	}

	if (tripletsOnHere && timelineView->supportsTriplets()) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			if (!timelineView->isSquareDefined(xDisplay, xScroll, xZoom)) {
				image[yDisplay][xDisplay] = colours::grey;

				if (occupancyMask) {
					occupancyMask[yDisplay][xDisplay] = 64;
				}
			}
		}
	}
}

// defers to arranger, audio clip or instrument clip sidebar render functions
// depending on the active clip
bool AutomationView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (onArrangerView) {
		return arrangerView.renderSidebar(whichRows, image, occupancyMask);
	}
	else {
		return getCurrentClip()->renderSidebar(whichRows, image, occupancyMask);
	}
}

/*render's what is displayed on OLED or 7SEG screens when in Automation View

On Automation Overview:

- on OLED it renders "Automation Overview" (or "Can't Automate CV" if you're on a CV clip)
- on 7Seg it renders AUTO (or CANT if you're on a CV clip)

On Automation Editor:

- on OLED it renders Parameter Name, Automation Status and Parameter Value (for selected Pad or the
current value for the Parameter for the last selected Mod Position)
- on 7SEG it renders Parameter name if no pad is selected or mod encoder is turned. If selecting pad it
displays the pads value for as long as you hold the pad. if turning mod encoder, it displays value while
turning mod encoder. after value displaying is finished, it displays scrolling parameter name again.

This function replaces the two functions that were previously called:

DisplayParameterValue
DisplayParameterName */

void AutomationView::renderDisplay(int32_t knobPosLeft, int32_t knobPosRight, bool modEncoderAction) {
	// don't refresh display if we're not current in the automation view UI
	// (e.g. if you're editing automation while in the menu)
	if (getCurrentUI() != &automationView) {
		return;
	}

	Clip* clip = getCurrentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	// if you're not in a MIDI instrument clip, convert the knobPos to the same range as the menu (0-50)
	if (inAutomationEditor() && (onArrangerView || outputType != OutputType::MIDI_OUT)) {
		params::Kind lastSelectedParamKind = params::Kind::NONE;
		int32_t lastSelectedParamID = params::kNoParamID;
		if (onArrangerView) {
			lastSelectedParamKind = currentSong->lastSelectedParamKind;
			lastSelectedParamID = currentSong->lastSelectedParamID;
		}
		else {
			lastSelectedParamKind = clip->lastSelectedParamKind;
			lastSelectedParamID = clip->lastSelectedParamID;
		}
		if (knobPosLeft != kNoSelection) {
			knobPosLeft = view.calculateKnobPosForDisplay(lastSelectedParamKind, lastSelectedParamID, knobPosLeft);
		}
		if (knobPosRight != kNoSelection) {
			knobPosRight = view.calculateKnobPosForDisplay(lastSelectedParamKind, lastSelectedParamID, knobPosRight);
		}
	}

	// OLED Display
	if (display->haveOLED()) {
		renderDisplayOLED(clip, output, outputType, knobPosLeft, knobPosRight);
	}
	// 7SEG Display
	else {
		renderDisplay7SEG(clip, output, outputType, knobPosLeft, modEncoderAction);
	}
}

void AutomationView::renderDisplayOLED(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft,
                                       int32_t knobPosRight) {
	deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
	hid::display::OLED::clearMainImage();

	if (onAutomationOverview()) {
		renderAutomationOverviewDisplayOLED(canvas, output, outputType);
	}
	else {
		if (inAutomationEditor()) {
			automationEditorLayoutModControllable.renderAutomationEditorDisplayOLED(canvas, clip, outputType,
			                                                                        knobPosLeft, knobPosRight);
		}
		else {
			automationEditorLayoutNote.renderNoteEditorDisplayOLED(canvas, (InstrumentClip*)clip, outputType,
			                                                       knobPosLeft, knobPosRight);
		}
	}

	deluge::hid::display::OLED::markChanged();
}

void AutomationView::renderAutomationOverviewDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                         Output* output, OutputType outputType) {
	// align string to vertically to the centre of the display
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 24;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 15;
#endif

	// display Automation Overview
	char const* overviewText;
	if (!onArrangerView && (outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)) {
		overviewText = l10n::get(l10n::String::STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE);
		deluge::hid::display::OLED::drawPermanentPopupLookingText(overviewText);
	}
	else {
		overviewText = l10n::get(l10n::String::STRING_FOR_AUTOMATION_OVERVIEW);
		canvas.drawStringCentred(overviewText, yPos, kTextSpacingX, kTextSpacingY);
	}
}

void AutomationView::renderDisplay7SEG(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft,
                                       bool modEncoderAction) {
	// display OVERVIEW
	if (onAutomationOverview()) {
		renderAutomationOverviewDisplay7SEG(output, outputType);
	}
	else {
		if (inAutomationEditor()) {
			automationEditorLayoutModControllable.renderAutomationEditorDisplay7SEG(clip, outputType, knobPosLeft,
			                                                                        modEncoderAction);
		}
		else {
			automationEditorLayoutNote.renderNoteEditorDisplay7SEG((InstrumentClip*)clip, outputType, knobPosLeft);
		}
	}
}

void AutomationView::renderAutomationOverviewDisplay7SEG(Output* output, OutputType outputType) {
	char const* overviewText;
	if (!onArrangerView && (outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)) {
		overviewText = l10n::get(l10n::String::STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE);
	}
	else {
		overviewText = l10n::get(l10n::String::STRING_FOR_AUTOMATION);
	}
	display->setScrollingText(overviewText);
}

// adjust the LED meters and update the display

/*updated function for displaying automation when playback is enabled (called from ui_timer_manager).
Also used internally in the automation instrument clip view for updating the display and led
indicators.*/

void AutomationView::displayAutomation(bool padSelected, bool updateDisplay) {
	if ((!padSelectionOn && !isUIModeActive(UI_MODE_NOTES_PRESSED)) || padSelected) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (onArrangerView) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			modelStackWithParam =
			    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
		}
		else {
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			Clip* clip = getCurrentClip();

			modelStackWithParam = getModelStackWithParamForClip(modelStack, clip);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {

			if (modelStackWithParam->getTimelineCounter()
			    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

				int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, view.modPos) + kKnobPosOffset;

				bool displayValue = updateDisplay
				                    && (display->haveOLED()
				                        || (display->have7SEG() && inAutomationEditor()
				                            && (playbackHandler.isEitherClockActive() || padSelected)));

				// update value on the screen when playing back automation
				// don't update value displayed if there's no automation unless instructed to update display
				// don't update value displayed when playback is stopped
				if (displayValue) {
					renderDisplay(knobPos);
				}
				// on 7SEG re-render parameter name under certain circumstances
				// e.g. when entering pad selection mode, when stopping playback
				else {
					renderDisplay();
				}

				setAutomationKnobIndicatorLevels(modelStackWithParam, knobPos, knobPos);
			}
		}
	}
}

// button action

ActionResult AutomationView::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	using namespace hid::button;

	Clip* clip = getCurrentClip();
	bool isAudioClip = clip->type == ClipType::AUDIO;

	// these button actions are not used in the audio clip automation view
	if (isAudioClip) {
		if (b == SCALE_MODE || b == KIT || b == SYNTH || b == MIDI || b == CV) {
			return ActionResult::DEALT_WITH;
		}
	}

	OutputType outputType = clip->output->type;

	// Scale mode button
	if (b == SCALE_MODE) {
		return instrumentClipView.handleScaleButtonAction(on, inCardRoutine);
	}

	// Song view button
	else if (b == SESSION_VIEW) {
		handleSessionButtonAction(clip, on);
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		handleKeyboardButtonAction(on);
	}

	// Clip button - exit mode
	// if you're holding shift or holding an audition pad while pressed clip, don't exit out of
	// automation view reset parameter selection and short blinking instead
	else if (b == CLIP_VIEW) {
		handleClipButtonAction(on, isAudioClip);
	}

	// Auto scrolling
	// Or Cross Screen Note Editing in Note Editor
	// Does not currently work for Automation
	else if (b == CROSS_SCREEN_EDIT) {
		// toggle auto scroll or cross screen editing
		if (onArrangerView || inNoteEditor()) {
			handleCrossScreenButtonAction(on);
		}
		// don't toggle for automation editing
		else {
			return ActionResult::DEALT_WITH;
		}
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == KIT) {
		handleKitButtonAction(outputType, on);
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == SYNTH && currentUIMode != UI_MODE_HOLDING_SAVE_BUTTON
	         && currentUIMode != UI_MODE_HOLDING_LOAD_BUTTON) {
		handleSynthButtonAction(outputType, on);
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == MIDI) {
		handleMidiButtonAction(outputType, on);
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == CV) {
		handleCVButtonAction(outputType, on);
	}

	// Horizontal encoder button
	// Not relevant for arranger view
	else if (b == X_ENC) {
		if (handleHorizontalEncoderButtonAction(on, isAudioClip)) {
			goto passToOthers;
		}
	}

	// SHIFT + SAVE/DELETE while holding a target's param button: clear that target's assignment
	// immediately. No confirmation - holding one specific target button and hitting SHIFT+SAVE is a
	// deliberate gesture (and matches the note-view picker's delete). The baked automation on that
	// param is deleted and any shadowed co-target takes over, same as dialing the target to OFF.
	else if (b == SAVE && on && Buttons::isShiftButtonPressed() && heldTarget >= 0 && inAutomationEditor()
	         && !onArrangerView) {
		int32_t macroIndex = 0;
		if (macroLaneInstrument(clip, &macroIndex) != nullptr && macroIndex == heldTargetMacro) {
			clearMacroTargetAssignment(macroIndex, heldTarget);
			// End the hold now and tear it down exactly as the button's release would - its release will
			// fall through to the normal path since heldTarget is cleared, so it won't do this itself.
			heldTarget = -1;
			heldTargetMacro = -1;
			heldTargetPendingDestination = -1;
			macroPickerLastX = -1;
			macroPickerLastY = -1;
			macroPickerSecondLayer = false;
			view.setModLedStates();                           // re-sync the target LEDs now the hold is over
			view.setKnobIndicatorLevels();                    // restore the knob rings the hold displaced
			blinkShortcuts();                                 // re-arm the macro-pad blinking the overlay stopped
			uiNeedsRendering(&automationView, 0xFFFFFFFF, 0); // drop the picker overlay, back to the editor
			return ActionResult::DEALT_WITH;
		}
	}

	// if holding horizontal encoder button down and pressing back clear automation
	// if you're on automation overview, clear all automation
	// if you're in the automation editor, clear the automation for the parameter in focus
	else if (b == BACK && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		if (handleBackAndHorizontalEncoderButtonComboAction(clip, on)) {
			goto passToOthers;
		}
	}

	// Vertical encoder button
	// Not relevant for audio clip
	else if (b == Y_ENC && !isAudioClip) {
		// LEARN + vertical encoder press on a macro lane opens that macro's Source learn page (same
		// as Macros -> Macro N -> Source), keeping LEARN + audition pad as the clip-input learn. A
		// source learned there is consumed by the macro - it never reaches its own CC's lane.
		if (on && Buttons::isButtonPressed(deluge::hid::button::LEARN) && inAutomationEditor() && !onArrangerView) {
			int32_t macroIndex = 0;
			if (macroLaneInstrument(clip, &macroIndex) != nullptr) {
				view.endMIDILearn(); // holding LEARN entered midi-learn mode - leave it before the menu
				if (soundEditor.setup(clip, macroSourceMenuItems[macroIndex])) {
					openUI(&soundEditor);
				}
				return ActionResult::DEALT_WITH;
			}
		}
		// SHIFT + vertical encoder press on a macro lane toggles that macro Active/Inactive
		if (on && Buttons::isShiftButtonPressed() && inAutomationEditor() && !onArrangerView
		    && toggleMacroLaneActive()) {
			return ActionResult::DEALT_WITH;
		}
		handleVerticalEncoderButtonAction(on);
	}

	// Select encoder
	// if you're not pressing shift and press down on the select encoder, enter sound menu
	else if (!Buttons::isShiftButtonPressed() && b == SELECT_ENC) {
		handleSelectEncoderButtonAction(on);
	}

	else {
passToOthers:
		// if you're entering settings menu
		if (on && (b == SELECT_ENC) && Buttons::isShiftButtonPressed()) {
			if (padSelectionOn) {
				initPadSelection();
			}
		}

		// if you just toggle playback off, re-render 7SEG display
		if (!on && (b == PLAY) && display->have7SEG() && inAutomationEditor() && !padSelectionOn
		    && !playbackHandler.isEitherClockActive()) {
			renderDisplay();
		}

		uiNeedsRendering(&automationView);

		ActionResult result;
		if (onArrangerView) {
			result = TimelineView::buttonAction(b, on, inCardRoutine);
		}
		else if (isAudioClip) {
			result = ClipMinder::buttonAction(b, on);
		}
		else {
			result = InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		}
		if (result == ActionResult::NOT_DEALT_WITH) {
			result = ClipView::buttonAction(b, on, inCardRoutine);
		}

		// when you press affect entire, the parameter selection needs to reset
		// do this here because affect entire state may have just changed
		if (on && b == AFFECT_ENTIRE) {
			initParameterSelection();
			blinkShortcuts();
		}

		return result;
	}

	if (on && (b != KEYBOARD && b != CLIP_VIEW && b != SESSION_VIEW)) {
		uiNeedsRendering(&automationView);
	}

	return ActionResult::DEALT_WITH;
}

// called by button action if b == SESSION_VIEW
void AutomationView::handleSessionButtonAction(Clip* clip, bool on) {
	// if shift is pressed, go back to automation overview
	if (on && Buttons::isShiftButtonPressed()) {
		initParameterSelection();
		blinkShortcuts();
		uiNeedsRendering(&automationView);
	}
	// go back to song / arranger view
	else if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		if (padSelectionOn) {
			initPadSelection();
		}
		// automation arranger view transitioning back to arranger view
		if (onArrangerView) {
			changeRootUI(&arrangerView);
		}
		// automation clip view transitioning back to arranger or session view
		else {
			ClipMinder::transitionToArrangerOrSession();
		}
		resetShortcutBlinking();
	}
}

// called by button action if b == KEYBOARD
void AutomationView::handleKeyboardButtonAction(bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		if (padSelectionOn) {
			initPadSelection();
		}
		if (onArrangerView) {
			performanceView.timeKeyboardShortcutPress = AudioEngine::audioSampleTimer;
			changeRootUI(&performanceView);
		}
		else {
			changeRootUI(&keyboardScreen);
		}
		// reset blinking if you're leaving automation view for keyboard view
		// blinking will be reset when you come back
		resetShortcutBlinking();
	}
}

// called by button action if b == CLIP_VIEW
void AutomationView::handleClipButtonAction(bool on, bool isAudioClip) {
	// if audition pad or shift is pressed, go back to automation overview
	if (on && (currentUIMode == UI_MODE_AUDITIONING || Buttons::isShiftButtonPressed())) {
		initParameterSelection();
		blinkShortcuts();
		uiNeedsRendering(&automationView);
	}
	// go back to clip view
	else if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		if (padSelectionOn) {
			initPadSelection();
		}
		// automation arranger view transitioning back to arranger view
		if (onArrangerView) {
			changeRootUI(&arrangerView);
		}
		// automation audio clip view transitioning back to audio clip view
		else if (isAudioClip) {
			changeRootUI(&audioClipView);
		}
		// automation instrument clip view transitioning back to instrument clip view
		else {
			changeRootUI(&instrumentClipView);
		}
		resetShortcutBlinking();
	}
}

// call by button action if b == CROSS_SCREEN_EDIT
void AutomationView::handleCrossScreenButtonAction(bool on) {
	if (!on && currentUIMode == UI_MODE_NONE) {
		// if another button wasn't pressed while cross screen was held
		if (Buttons::considerCrossScreenReleaseForCrossScreenMode) {
			if (onArrangerView) {
				currentSong->arrangerAutoScrollModeActive = !currentSong->arrangerAutoScrollModeActive;
				indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, currentSong->arrangerAutoScrollModeActive);

				if (currentSong->arrangerAutoScrollModeActive) {
					arrangerView.reassessWhetherDoingAutoScroll();
				}
				else {
					arrangerView.doingAutoScrollNow = false;
				}
			}
			else {
				InstrumentClip* clip = getCurrentInstrumentClip();
				if (clip) {
					if (clip->wrapEditing) {
						clip->wrapEditing = false;
					}
					else {
						clip->wrapEditLevel = currentSong->xZoom[NAVIGATION_CLIP] * kDisplayWidth;
						// Ensure that there are actually multiple screens to edit across
						if (clip->wrapEditLevel < clip->loopLength) {
							clip->wrapEditing = true;
						}
						// If in we're in the note editor, we can check if the note row has multiple screens
						else if (inNoteEditor()) {
							char modelStackMemory[MODEL_STACK_MAX_SIZE];
							ModelStackWithTimelineCounter* modelStack =
							    currentSong->setupModelStackWithCurrentClip(modelStackMemory);
							ModelStackWithNoteRow* modelStackWithNoteRow =
							    clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
							                             modelStack); // don't create
							if (clip->wrapEditLevel < modelStackWithNoteRow->getLoopLength()) {
								clip->wrapEditing = true;
							}
						}
					}

					setLedStates();
				}
			}
		}
	}
}

// called by button action if b == KIT
void AutomationView::handleKitButtonAction(OutputType outputType, bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to create a new instrument or change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		instrumentClipView.handleInstrumentChange(OutputType::KIT);
	}
}

// called by button action if b == SYNTH
void AutomationView::handleSynthButtonAction(OutputType outputType, bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to create a new instrument or change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		instrumentClipView.handleInstrumentChange(OutputType::SYNTH);
	}
}

// called by button action if b == MIDI
void AutomationView::handleMidiButtonAction(OutputType outputType, bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		instrumentClipView.changeOutputType(OutputType::MIDI_OUT);
	}
}

// called by button action if b == CV
void AutomationView::handleCVButtonAction(OutputType outputType, bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		instrumentClipView.changeOutputType(OutputType::CV);
	}
}
// called by button action if b == X_ENC
bool AutomationView::handleHorizontalEncoderButtonAction(bool on, bool isAudioClip) {
	// copy / paste automation (same shortcut used for notes)
	if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
		if (inAutomationEditor()) {
			Clip* clip = getCurrentClip();
			OutputType outputType = clip->output->type;

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
			ModelStackWithAutoParam* modelStackWithParam = nullptr;

			if (onArrangerView) {
				modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
				modelStackWithParam = currentSong->getModelStackWithParam(modelStackWithThreeMainThings,
				                                                          currentSong->lastSelectedParamID);
			}
			else {
				modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
				modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
			}
			int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

			int32_t xScroll = currentSong->xScroll[navSysId];
			int32_t xZoom = currentSong->xZoom[navSysId];

			if (Buttons::isShiftButtonPressed()) {
				// paste within Automation Editor
				automationEditorLayoutModControllable.pasteAutomation(modelStackWithParam, clip, effectiveLength,
				                                                      xScroll, xZoom);
			}
			else {
				// copy within Automation Editor
				automationEditorLayoutModControllable.copyAutomation(modelStackWithParam, clip, xScroll, xZoom);
			}
		}
		return false;
	}
	else if (onArrangerView) {
		return true;
	}
	else if (isAudioClip) {
		// removing time stretching by re-calculating clip length based on length of audio sample
		if (on && Buttons::isButtonPressed(deluge::hid::button::Y_ENC) && currentUIMode == UI_MODE_NONE) {
			audioClipView.setClipLengthEqualToSampleLength();
			return false;
		}
		// if shift is pressed then we're resizing the clip without time stretching
		else if (Buttons::isShiftButtonPressed()) {
			return false;
		}
		return true;
	}
	// If user wants to "multiply" Clip contents
	else if (on && Buttons::isShiftButtonPressed() && !isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)
	         && !onAutomationOverview()) {
		if (isNoUIModeActive()) {
			// Zoom to max if we weren't already there...
			if (!zoomToMax()) {
				// Or if we didn't need to do that, double Clip length
				instrumentClipView.doubleClipLengthAction();
			}
			else {
				displayZoomLevel();
			}
		}
		// Whether or not we did the "multiply" action above, we need to be in this UI mode, e.g. for
		// rotating individual NoteRow
		enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
	}

	// Otherwise...
	else {
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			if (!on) {
				instrumentClipView.timeHorizontalKnobLastReleased = AudioEngine::audioSampleTimer;
			}
		}
		return true;
	}
	return false;
}

// called by button action if b == back and UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON
bool AutomationView::handleBackAndHorizontalEncoderButtonComboAction(Clip* clip, bool on) {
	// only allow clearing of a clip if you're on the automation overview
	if (on && onAutomationOverview()) {
		if (clip->type == ClipType::AUDIO || onArrangerView) {
			// clear all arranger automation
			if (onArrangerView) {
				Action* action = actionLogger.getNewAction(ActionType::ARRANGEMENT_CLEAR, ActionAddition::NOT_ALLOWED);

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStack =
				    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
				currentSong->paramManager.deleteAllAutomation(action, modelStack);
			}
			// clear all audio clip automation
			else {
				Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

				// clear automation, don't clear sample and mpe
				bool clearAutomation = true;
				bool clearSequenceAndMPE = false;
				clip->clear(action, modelStack, clearAutomation, clearSequenceAndMPE);
			}
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_AUTOMATION_CLEARED));

			return false;
		}
		return true;
	}
	else if (on && inAutomationEditor()) {
		// delete automation of current parameter selected

		char modelStackMemory[MODEL_STACK_MAX_SIZE];

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (onArrangerView) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
			modelStackWithParam =
			    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
		}
		else {
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			modelStackWithParam = getModelStackWithParamForClip(modelStack, clip);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {
			// each delete is its own undo step; the target cascade below joins it via the action pointer
			Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE);
			modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

			// clearing a macro lane clears its target lanes too, in the same undo step
			int32_t clearedMacroIndex = 0;
			if (!onArrangerView && macroLaneInstrument(clip, &clearedMacroIndex) != nullptr) {
				Macros::reFanMacro(clip, clearedMacroIndex, action);
			}

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));

			displayAutomation(padSelectionOn, !display->have7SEG());
		}
	}
	else if (on && inNoteEditor()) {
		Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

		// don't create note row if it doesn't exist
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    ((InstrumentClip*)clip)->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay, modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
			// don't clear automation, do clear notes and mpe
			noteRow->clear(action, modelStackWithNoteRow, false, true);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTES_CLEARED));
		}
	}
	return false;
}

// handle by button action if b == Y_ENC
void AutomationView::handleVerticalEncoderButtonAction(bool on) {
	if (on) {
		if (inNoteEditor()) {
			if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
				// Just pop up number - don't do anything
				instrumentClipView.editNoteRepeat(0);
			}
			else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    ((InstrumentClip*)modelStack->getTimelineCounter())
				        ->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay, modelStack);

				// Just pop up number - don't do anything
				instrumentClipView.editNumEuclideanEvents(modelStackWithNoteRow, 0,
				                                          instrumentClipView.lastAuditionedYDisplay);
			}
		}
	}
}

// called by button action if b == SELECT_ENC and shift button is not pressed
void AutomationView::handleSelectEncoderButtonAction(bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		initParameterSelection();
		uiNeedsRendering(&automationView);

		if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
			return;
		}

		if ((getCurrentOutputType() == OutputType::KIT) && (getCurrentInstrumentClip()->affectEntire)) {
			soundEditor.setupKitGlobalFXMenu = true;
		}

		display->setNextTransitionDirection(1);
		Clip* clip = onArrangerView ? nullptr : getCurrentClip();
		if (soundEditor.setup(clip)) {
			openUI(&soundEditor);
		}
	}
}

// pad action
// handles shortcut pad action for automation (e.g. when you press shift + pad on the grid)
// everything else is pretty much the same as instrument clip view
ActionResult AutomationView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	Clip* clip = getCurrentClip();

	if (clip->type == ClipType::AUDIO) {
		if (x >= kDisplayWidth) {
			return ActionResult::DEALT_WITH;
		}
	}

	// don't interact with sidebar if VU Meter is displayed
	if (onArrangerView && x >= kDisplayWidth && view.displayVUMeter) {
		return ActionResult::DEALT_WITH;
	}

	Output* output = clip->output;
	OutputType outputType = output->type;

	// if we're in a midi clip with a midi destination selected - or on a macro lane on either clip type -
	// and we press the name shortcut while holding shift, then enter the rename UI
	if (outputType == OutputType::MIDI_OUT || (!onArrangerView && macroLaneInstrument(clip, nullptr) != nullptr)) {
		if (Buttons::isShiftButtonPressed() && x == 11 && y == 5) {
			if (!onAutomationOverview()) {
				openUI(&renameMidiCCUI);
				return ActionResult::DEALT_WITH;
			}
		}
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	ModelStackWithNoteRow* modelStackWithNoteRow = nullptr;
	NoteRow* noteRow = nullptr;
	int32_t effectiveLength = 0;
	SquareInfo squareInfo;

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
		if (inNoteEditor()) {
			modelStackWithNoteRow = ((InstrumentClip*)clip)
			                            ->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
			                                                 modelStackWithTimelineCounter); // don't create
			// does note row exist?
			if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
				// if you're in note editor and note row doesn't exist, create it
				// don't create note rows that don't exist in kits because those are empty kit rows
				if (outputType != OutputType::KIT) {
					modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(
					    modelStackWithTimelineCounter, instrumentClipView.lastAuditionedYDisplay);
				}
			}

			if (modelStackWithNoteRow->getNoteRowAllowNull()) {
				effectiveLength = modelStackWithNoteRow->getLoopLength();
				noteRow = modelStackWithNoteRow->getNoteRow();
				noteRow->getSquareInfo(x, effectiveLength, squareInfo);
			}
		}
	}

	if (!inNoteEditor()) {
		effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);
	}

	// Edit pad action...
	if (x < kDisplayWidth) {
		return handleEditPadAction(modelStackWithParam, modelStackWithNoteRow, noteRow, clip, output, outputType,
		                           effectiveLength, x, y, velocity, squareInfo);
	}
	// mute / status pad action
	else if (x == kDisplayWidth) {
		return handleMutePadAction(modelStackWithTimelineCounter, (InstrumentClip*)clip, output, outputType, y,
		                           velocity);
	}
	// Audition pad action
	else {
		if (x == kDisplayWidth + 1) {
			return handleAuditionPadAction((InstrumentClip*)clip, output, outputType, y, velocity);
		}
	}

	return ActionResult::DEALT_WITH;
}

// called by pad action when pressing a pad in the main grid (x < kDisplayWidth)
ActionResult AutomationView::handleEditPadAction(ModelStackWithAutoParam* modelStackWithParam,
                                                 ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                                 Clip* clip, Output* output, OutputType outputType,
                                                 int32_t effectiveLength, int32_t x, int32_t y, int32_t velocity,
                                                 SquareInfo& squareInfo) {

	if (onArrangerView && isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		return ActionResult::DEALT_WITH;
	}

	// While the destination picker owns the grid, a press on a lit pad picks that param as the
	// held target's pending destination; every press is consumed so nothing edits the macro
	// lane's automation hidden under the overlay.
	if (macroPickerActive(clip)) {
		if (velocity) {
			handleMacroPickerPad(clip, x, y);
		}
		return ActionResult::DEALT_WITH;
	}

	// potentially enter or refresh note velocity editor if you're in an instrument clip, holding audition pad and
	// pressing PatchSource::Velocity shortcut
	if (!onArrangerView && clip->type == ClipType::INSTRUMENT && isUIModeActive(UI_MODE_AUDITIONING)
	    && isNoteVelocityEditorShortcut(x, y)) {
		// don't enter if we're in a kit with affect entire enabled
		if (!(outputType == OutputType::KIT && getAffectEntire())) {
			if (outputType == OutputType::KIT) {
				potentiallyVerticalScrollToSelectedDrum((InstrumentClip*)clip, output);
			}
			initParameterSelection(false);
			automationParamType = AutomationParamType::NOTE_VELOCITY;
			clip->lastSelectedParamShortcutX = x;
			clip->lastSelectedParamShortcutY = y;
			blinkShortcuts();
			renderDisplay();
			uiNeedsRendering(&automationView);
			// if you're in note editor, turn led on
			if (((InstrumentClip*)clip)->wrapEditing) {
				indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, true);
			}
		}
		return ActionResult::DEALT_WITH;
	}

	int32_t xScroll = currentSong->xScroll[navSysId];
	int32_t xZoom = currentSong->xZoom[navSysId];

	// if the user wants to change the parameter they are editing using Shift + Pad shortcut
	// or change the parameter they are editing by press on a shortcut pad on automation overview
	// or they want to enable/disable interpolation
	// or they want to enable/disable pad selection mode
	if (shortcutPadAction(modelStackWithParam, clip, output, outputType, effectiveLength, x, y, velocity, xScroll,
	                      xZoom, squareInfo)) {
		return ActionResult::DEALT_WITH;
	}

	// regular automation / note editing action
	if (isUIModeWithinRange(editPadActionUIModes) && isSquareDefined(x, xScroll, xZoom)) {
		if (inAutomationEditor()) {
			// A macro-driven param's value is owned by the macro (it overwrites each tick), so refuse the
			// pad edit and nudge to the macro instead of writing a value that would just snap back.
			if (velocity && refuseEditIfMacroDriven(clip)) {
				return ActionResult::DEALT_WITH;
			}
			automationEditorLayoutModControllable.automationEditPadAction(modelStackWithParam, clip, x, y, velocity,
			                                                              effectiveLength, xScroll, xZoom);
		}
		else if (inNoteEditor()) {
			if (noteRow) {
				automationEditorLayoutNote.noteEditPadAction(modelStackWithNoteRow, noteRow, (InstrumentClip*)clip, x,
				                                             y, velocity, effectiveLength, squareInfo);
			}
		}
	}
	return ActionResult::DEALT_WITH;
}

bool AutomationView::isNoteVelocityEditorShortcut(int32_t x, int32_t y) {
	return (x == kVelocityShortcutX && y == kVelocityShortcutY);
}

/// handles shortcut pad actions, including:
/// 1) toggle interpolation on / off
/// 2) select parameter on automation overview
/// 3) select parameter using shift + shortcut pad
/// 4) select parameter using audition + shortcut pad
bool AutomationView::shortcutPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, Output* output,
                                       OutputType outputType, int32_t effectiveLength, int32_t x, int32_t y,
                                       int32_t velocity, int32_t xScroll, int32_t xZoom, SquareInfo& squareInfo) {
	if (velocity) {
		bool shortcutPress = false;
		if (Buttons::isShiftButtonPressed()
		    || (isUIModeActive(UI_MODE_AUDITIONING) && !FlashStorage::automationDisableAuditionPadShortcuts)) {

			if (!inNoteEditor()) {
				// toggle interpolation on / off
				// not relevant for note editor because interpolation doesn't apply to note params
				if ((x == kInterpolationShortcutX && y == kInterpolationShortcutY)) {
					return automationEditorLayoutModControllable.toggleAutomationInterpolation();
				}
				// toggle pad selection on / off
				// not relevant for note editor because pad selection mode was deemed unnecessary
				if (inAutomationEditor() && (x == kPadSelectionShortcutX && y == kPadSelectionShortcutY)) {
					return automationEditorLayoutModControllable.toggleAutomationPadSelectionMode(
					    modelStackWithParam, effectiveLength, xScroll, xZoom);
				}
			}

			shortcutPress = true;
		}
		// this means you are selecting a parameter / entering or refreshing note editor
		if (shortcutPress || onAutomationOverview()) {
			// don't change parameters this way if we're in the menu
			if (getCurrentUI() == &automationView) {
				// make sure the context is valid for selecting a parameter
				// can't select a parameter in a kit if you haven't selected a drum
				if (onArrangerView
				    || !(outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)
				    || (outputType == OutputType::KIT && getAffectEntire())) {

					handleParameterSelection(clip, output, outputType, x, y);

					// if you're in not in note editor, turn led off if it's on
					if (((InstrumentClip*)clip)->wrapEditing) {
						indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, inNoteEditor());
					}
				}
			}

			return true;
		}
	}
	return false;
}

// called by shortcutPadAction when it is determined that you are selecting a parameter on automation
// overview or by using a grid shortcut combo
void AutomationView::handleParameterSelection(Clip* clip, Output* output, OutputType outputType, int32_t xDisplay,
                                              int32_t yDisplay) {
	// potentially select a regular automatable parameter
	if (!onArrangerView
	    && (outputType == OutputType::SYNTH
	        || (outputType == OutputType::KIT && !getAffectEntire() && ((Kit*)output)->selectedDrum
	            && ((Kit*)output)->selectedDrum->type == DrumType::SOUND))
	    && ((patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID)
	        || (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)
	        || params::isPatchCableShortcut(xDisplay, yDisplay))) {
		// don't allow automation of portamento in kit's
		if ((outputType == OutputType::KIT)
		    && (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == params::UNPATCHED_PORTAMENTO)) {
			return; // no parameter selected, don't re-render grid;
		}

		// if you are in a synth or a kit instrumentClip and the shortcut is valid, set current selected
		// ParamID
		if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			clip->lastSelectedParamKind = params::Kind::PATCHED;
			clip->lastSelectedParamID = patchedParamShortcuts[xDisplay][yDisplay];
		}
		else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			clip->lastSelectedParamKind = params::Kind::UNPATCHED_SOUND;
			clip->lastSelectedParamID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
		}
		else if (params::isPatchCableShortcut(xDisplay, yDisplay)) {
			ParamDescriptor paramDescriptor;
			params::getPatchCableFromShortcut(xDisplay, yDisplay, &paramDescriptor);
			clip->lastSelectedParamKind = params::Kind::PATCH_CABLE;
			clip->lastSelectedParamID = paramDescriptor.data;
			clip->lastSelectedPatchSource = paramDescriptor.getBottomLevelSource();
		}

		if (clip->lastSelectedParamKind != params::Kind::PATCH_CABLE) {
			getLastSelectedNonGlobalParamArrayPosition(clip);
		}
	}

	// if you are in arranger, an audio clip, or a kit clip with affect entire enabled
	else if ((onArrangerView || (outputType == OutputType::AUDIO)
	          || (outputType == OutputType::KIT && getAffectEntire()))
	         && (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)) {

		params::Kind paramKind = params::Kind::UNPATCHED_GLOBAL;
		int32_t paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];

		// don't allow automation of pitch adjust, or sidechain in arranger
		if (onArrangerView
		    && ((paramID == params::UNPATCHED_PITCH_ADJUST) || (paramID == params::UNPATCHED_SIDECHAIN_SHAPE)
		        || (paramID == params::UNPATCHED_SIDECHAIN_VOLUME)
		        || (paramID >= params::UNPATCHED_FIRST_ARP_PARAM && paramID <= params::UNPATCHED_LAST_ARP_PARAM)
		        || (paramID == params::UNPATCHED_ARP_RATE))) {
			return; // no parameter selected, don't re-render grid;
		}
		else if (outputType == OutputType::AUDIO
		         && ((paramID >= params::UNPATCHED_FIRST_ARP_PARAM && paramID <= params::UNPATCHED_LAST_ARP_PARAM)
		             || paramID == params::UNPATCHED_ARP_RATE)) {
			return; // no parameter selected, don't re-render grid;
		}

		if (onArrangerView) {
			currentSong->lastSelectedParamKind = paramKind;
			currentSong->lastSelectedParamID = paramID;
		}
		else {
			clip->lastSelectedParamKind = paramKind;
			clip->lastSelectedParamID = paramID;
		}

		getLastSelectedGlobalParamArrayPosition(clip);
	}

	else if (outputType == OutputType::MIDI_OUT
	         && midiFollow.midiCCShortcutsForAutomation[xDisplay][yDisplay] != kNoParamID) {

		// if you are in a midi clip and the shortcut is valid, set the current selected ParamID
		clip->lastSelectedParamID = midiFollow.midiCCShortcutsForAutomation[xDisplay][yDisplay];
	}
	// expression params, so sounds or midi/cv, or a single drum
	else if ((util::one_of(outputType, {OutputType::MIDI_OUT, OutputType::CV, OutputType::SYNTH})
	          // selected a single sound drum
	          || ((outputType == OutputType::KIT && !getAffectEntire() && ((Kit*)output)->selectedDrum
	               && ((Kit*)output)->selectedDrum->type == DrumType::SOUND)))
	         && params::expressionParamFromShortcut(xDisplay, yDisplay) != kNoParamID) {
		clip->lastSelectedParamID = params::expressionParamFromShortcut(xDisplay, yDisplay);
		clip->lastSelectedParamKind = params::Kind::EXPRESSION;
	}

	else {
		return; // no parameter selected, don't re-render grid;
	}

	// save the selected parameter ID's shortcut pad x,y coords so that you can setup the shortcut blink
	if (onArrangerView) {
		currentSong->lastSelectedParamShortcutX = xDisplay;
		currentSong->lastSelectedParamShortcutY = yDisplay;
	}
	else {
		clip->lastSelectedParamShortcutX = xDisplay;
		clip->lastSelectedParamShortcutY = yDisplay;
	}

	resetParameterShortcutBlinking();
	if (inNoteEditor()) {
		automationParamType = AutomationParamType::PER_SOUND;
		instrumentClipView.resetSelectedNoteRowBlinking();
	}
	blinkShortcuts();
	if (display->have7SEG()) {
		renderDisplay(); // always display parameter name first, if there's automation it will show after
	}
	displayAutomation(true);
	view.setModLedStates();
	uiNeedsRendering(&automationView);
	// turn off cross screen LED in automation editor
	if (clip && clip->type == ClipType::INSTRUMENT && ((InstrumentClip*)clip)->wrapEditing) {
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	}
}

// called by pad action when pressing a pad in the mute column (x = kDisplayWidth)
ActionResult AutomationView::handleMutePadAction(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                                 InstrumentClip* instrumentClip, Output* output, OutputType outputType,
                                                 int32_t y, int32_t velocity) {
	if (onArrangerView) {
		return arrangerView.handleStatusPadAction(y, velocity, &automationView);
	}
	else {
		if (currentUIMode == UI_MODE_MIDI_LEARN) [[unlikely]] {
			return instrumentClipView.commandLearnMutePad(y, velocity);
		}
		else if (isUIModeWithinRange(mutePadActionUIModes) && velocity) {
			if (inAutomationEditor()) {
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    instrumentClip->getNoteRowOnScreen(y, modelStackWithTimelineCounter);

				// if we're in a kit, and you press a mute pad
				// check if it's a mute pad corresponding to the current selected drum
				// if not, change the drum selection, refresh parameter selection and go back to automation overview
				if (outputType == OutputType::KIT) {
					if (modelStackWithNoteRow->getNoteRowAllowNull()) {
						Drum* drum = modelStackWithNoteRow->getNoteRow()->drum;
						if (((Kit*)output)->selectedDrum != drum) {
							if (!getAffectEntire()) {
								initParameterSelection();
							}
						}
					}
				}
			}

			instrumentClipView.mutePadPress(y);
		}
	}
	return ActionResult::DEALT_WITH;
}

// called by pad action when pressing a pad in the audition column (x = kDisplayWidth + 1)
ActionResult AutomationView::handleAuditionPadAction(InstrumentClip* instrumentClip, Output* output,
                                                     OutputType outputType, int32_t y, int32_t velocity) {
	if (onArrangerView) {
		if (onAutomationOverview()) {
			return arrangerView.handleAuditionPadAction(y, velocity, &automationView);
		}
	}
	else {
		// "Learning" to this audition pad:
		if (isUIModeActiveExclusively(UI_MODE_MIDI_LEARN)) [[unlikely]] {
			if (getCurrentUI() == &automationView) {
				return instrumentClipView.commandLearnAuditionPad(instrumentClip, output, outputType, y, velocity);
			}
		}

		else if (currentUIMode == UI_MODE_HOLDING_SAVE_BUTTON && velocity) [[unlikely]] {
			return instrumentClipView.commandSaveKitRow(instrumentClip, output, outputType, y);
		}

		// Actual basic audition pad press:
		else if (!velocity || isUIModeWithinRange(auditionPadActionUIModes)) {
			bool auditioningSilently = Buttons::isShiftButtonPressed();
			if (inNoteEditor()) {
				if (isUIModeActive(UI_MODE_NOTES_PRESSED)) {
					// special handling for note editor and holding a note and we changed row selection
					// don't process audition pad action as it leads to stuck notes
					if (instrumentClipView.lastAuditionedYDisplay != y) {
						return ActionResult::DEALT_WITH;
					}
				}
				// We're quantizing: either adding a new note to the set being quantized, or removing.
				// In the first case we simply defer to auditionPadAction.
				else if (isUIModeActive(UI_MODE_QUANTIZE)) {
					if (velocity == 0) {
						return instrumentClipView.commandStopQuantize(y);
					}
					auditioningSilently = true;
				}
			}
			return auditionPadAction(instrumentClip, output, outputType, y, velocity, auditioningSilently);
		}
	}
	return ActionResult::DEALT_WITH;
}

// audition pad action
// not used with Audio Clip Automation View or Arranger Automation View
ActionResult AutomationView::auditionPadAction(InstrumentClip* clip, Output* output, OutputType outputType,
                                               int32_t yDisplay, int32_t velocity, bool shiftButtonDown) {
	if (sdRoutineLock && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allowable sometimes if in card routine.
	}

	if (instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan && !velocity) {
		// in case we were editing quantize/humanize
		actionLogger.closeAction(ActionType::NOTE_NUDGE);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	bool clipIsActiveOnInstrument = InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

	bool isKit = (outputType == OutputType::KIT);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
	    clip->getNoteRowOnScreen(yDisplay, modelStackWithTimelineCounter);

	Drum* drum = nullptr;

	bool selectedDrumChanged = false;
	bool selectedRowChanged = false;
	bool drawNoteCode = false;

	// If Kit...
	if (isKit) {

		// if we're in a kit, and you press an audition pad
		// check if it's a audition pad corresponding to the current selected drum
		// also check that you're not in affect entire mode
		// if not, change the drum selection, refresh parameter selection and go back to automation
		// overview
		if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
			drum = modelStackWithNoteRowOnCurrentClip->getNoteRow()->drum;
			Drum* selectedDrum = ((Kit*)output)->selectedDrum;
			if (selectedDrum != drum) {
				selectedDrumChanged = true;
			}
		}

		// If NoteRow doesn't exist here, don't try to create one
		else {
			return ActionResult::DEALT_WITH;
		}
	}

	// Or if synth
	else if (outputType == OutputType::SYNTH) {
		instrumentClipView.potentiallyUpdateMultiRangeMenu(velocity, yDisplay, (Instrument*)output);
	}

	instrumentClipView.potentiallyRecordAuditionPadAction(clipIsActiveOnInstrument, velocity, yDisplay,
	                                                      (Instrument*)output, isKit, modelStackWithTimelineCounter,
	                                                      modelStackWithNoteRowOnCurrentClip, drum);

	NoteRow* noteRowOnActiveClip = instrumentClipView.getNoteRowOnActiveClip(
	    yDisplay, (Instrument*)output, clipIsActiveOnInstrument, modelStackWithNoteRowOnCurrentClip, drum);

	bool doRender = true;

	// If note on...
	if (velocity != 0) {
		int32_t lastAuditionedYDisplay = instrumentClipView.lastAuditionedYDisplay;

		// don't draw if you're in note editor because note code is already on the display
		drawNoteCode = !inNoteEditor();

		doRender = instrumentClipView.startAuditioningRow(velocity, yDisplay, shiftButtonDown, isKit,
		                                                  noteRowOnActiveClip, drum, drawNoteCode);

		if (!isKit && (instrumentClipView.lastAuditionedYDisplay != lastAuditionedYDisplay)) {
			selectedRowChanged = true;
		}
	}

	// Or if auditioning this NoteRow just finished...
	else {
		instrumentClipView.finishAuditioningRow(yDisplay, modelStackWithNoteRowOnCurrentClip, noteRowOnActiveClip);
		if (display->have7SEG()) {
			renderDisplay();
		}
	}

	if (selectedRowChanged || (selectedDrumChanged && (!getAffectEntire() || inNoteEditor()))) {
		if (inNoteEditor()) {
			renderDisplay();
			instrumentClipView.resetSelectedNoteRowBlinking();
			instrumentClipView.blinkSelectedNoteRow(0xFFFFFFFF);
			doRender = false;
		}
		else if (selectedDrumChanged) {
			initParameterSelection();
			uiNeedsRendering(getRootUI());
			doRender = false;
		}
	}

	if (doRender) {
		renderingNeededRegardlessOfUI(0, 1 << yDisplay);
	}

	// draw note code on top of the automation view display which may have just been refreshed
	if (drawNoteCode) {
		instrumentClipView.drawNoteCode(yDisplay);
	}

	// This has to happen after instrumentClipView.setSelectedDrum is called, cos that resets LEDs
	if (!clipIsActiveOnInstrument && velocity) {
		indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
	}

	return ActionResult::DEALT_WITH;
}

// horizontal encoder actions:
// scroll left / right
// zoom in / out
// adjust clip length
// shift automations left / right
// adjust velocity in note editor
ActionResult AutomationView::horizontalEncoderAction(int32_t offset) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
	}

	if (inAutomationEditor()) {
		// exit multi pad press selection but keep single pad press selection (if it's selected)
		multiPadPressSelected = false;
		rightPadSelectedX = kNoSelection;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	}

	if (!onAutomationOverview()
	    && ((isNoUIModeActive() && Buttons::isButtonPressed(hid::button::Y_ENC))
	        || (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
	            && Buttons::isButtonPressed(hid::button::CLIP_VIEW))
	        || (isUIModeActiveExclusively(UI_MODE_AUDITIONING | UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)))) {

		if (inAutomationEditor()) {
			int32_t xScroll = currentSong->xScroll[navSysId];
			int32_t xZoom = currentSong->xZoom[navSysId];
			int32_t squareSize = getPosFromSquare(1, xScroll, xZoom) - getPosFromSquare(0, xScroll, xZoom);
			int32_t shiftAmount = offset * squareSize;

			if (onArrangerView) {
				modelStackWithParam = currentSong->getModelStackWithParam(modelStackWithThreeMainThings,
				                                                          currentSong->lastSelectedParamID);
			}
			else {
				Clip* clip = getCurrentClip();
				modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
			}

			int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

			shiftAutomationHorizontally(modelStackWithParam, shiftAmount, effectiveLength);

			if (offset < 0) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_SHIFT_LEFT));
			}
			else if (offset > 0) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_SHIFT_RIGHT));
			}
		}
		else if (inNoteEditor()) {
			instrumentClipView.rotateNoteRowHorizontally(offset);
		}

		return ActionResult::DEALT_WITH;
	}

	// else if showing the Parameter selection grid menu, disable this action
	else if (onAutomationOverview()) {
		return ActionResult::DEALT_WITH;
	}

	// Auditioning but not holding down <> encoder - edit length of just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		instrumentClipView.editNoteRowLength(offset);
		return ActionResult::DEALT_WITH;
	}

	// fine tune note velocity
	// If holding down notes and nothing else is held down, adjust velocity
	else if (inNoteEditor() && isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
		if (automationParamType == AutomationParamType::NOTE_VELOCITY) {
			if (!instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
				instrumentClipView.adjustVelocity(offset);
				renderDisplay(getCurrentInstrument()->defaultVelocity);
				uiNeedsRendering(&automationView, 0xFFFFFFFF, 0);
			}
		}
		return ActionResult::DEALT_WITH;
	}

	// Shift and x pressed - edit length of audio clip without timestretching
	else if (getCurrentClip()->type == ClipType::AUDIO && isNoUIModeActive()
	         && Buttons::isButtonPressed(deluge::hid::button::X_ENC) && Buttons::isShiftButtonPressed()) {
		ActionResult result = audioClipView.editClipLengthWithoutTimestretching(offset);
		return result;
	}

	// Or, let parent deal with it
	else {
		ActionResult result = ClipView::horizontalEncoderAction(offset);
		return result;
	}
}

// new function created for automation instrument clip view to shift automations of the selected
// parameter previously users only had the option to shift ALL automations together as part of community
// feature i disabled automation shifting in the regular instrument clip view
void AutomationView::shiftAutomationHorizontally(ModelStackWithAutoParam* modelStackWithParam, int32_t offset,
                                                 int32_t effectiveLength) {
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		modelStackWithParam->autoParam->shiftHorizontally(offset, effectiveLength);

		// shifting a macro lane must fan the moved curve out to the target lanes
		Clip* clip = getCurrentClip();
		int32_t shiftedMacroIndex = 0;
		if (!onArrangerView && macroLaneInstrument(clip, &shiftedMacroIndex) != nullptr) {
			Macros::reFanMacro(clip, shiftedMacroIndex, nullptr);
		}
	}

	uiNeedsRendering(&automationView);
}

// vertical encoder action
// no change compared to instrument clip view version
// not used with Audio Clip Automation View
ActionResult AutomationView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (onArrangerView) {
		if (Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
			currentSong->commandTranspose(offset);
		}
		return ActionResult::DEALT_WITH;
	}

	if (getCurrentClip()->type == ClipType::AUDIO) {
		return ActionResult::DEALT_WITH;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();
	OutputType outputType = clip->output->type;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If encoder button pressed
	if (Buttons::isButtonPressed(hid::button::Y_ENC)) {
		if (inNoteEditor() && currentUIMode != UI_MODE_NONE) {
			// only allow editing note repeats when selecting a note
			if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
				instrumentClipView.editNoteRepeat(offset);
			}
			// only allow euclidean while holding audition pad
			else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
				instrumentClipView.commandEuclidean(offset);
			}
		}
		// If user not wanting to move a noteCode, they want to transpose the key
		else if (!currentUIMode && outputType != OutputType::KIT) {
			ActionResult result = instrumentClipView.commandTransposeKey(offset, inCardRoutine);
			// if we're in note editor transposing will the change note selected
			// so we want to re-render the display to show the updated note
			if (inNoteEditor()) {
				renderDisplay();
			}
		}
	}

	// Or, if shift key is pressed
	else if (Buttons::isShiftButtonPressed()) {
		instrumentClipView.commandShiftColour(offset);
	}

	// If neither button is pressed, we'll do vertical scrolling
	else {
		if (isUIModeWithinRange(verticalScrollUIModes)) {
			if ((!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress
			     || (!isUIModeActive(UI_MODE_NOTES_PRESSED) && !isUIModeActive(UI_MODE_AUDITIONING)))
			    && (!(isUIModeActive(UI_MODE_NOTES_PRESSED) && inNoteEditor()))) {
				instrumentClipView.scrollVertical(offset, inCardRoutine, false, modelStack);

				// if we're in note editor scrolling vertically will change note selected
				// so we want to re-render the display to show the updated note
				if (inNoteEditor()) {
					renderDisplay();
				}
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

/// if we're entering note editor, we want the selected drum to be visible and in sync with lastAuditionedYDisplay
/// so we'll check if the yDisplay of the selectedDrum is in sync with the lastAuditionedYDisplay
/// if they're not in sync, we'll sync them up by performing a vertical scroll
void AutomationView::potentiallyVerticalScrollToSelectedDrum(InstrumentClip* clip, Output* output) {
	int32_t noteRowIndex;
	Drum* selectedDrum = ((Kit*)output)->selectedDrum;
	if (selectedDrum) {
		NoteRow* noteRow = clip->getNoteRowForDrum(selectedDrum, &noteRowIndex);
		if (noteRow) {
			int32_t lastAuditionedYDisplayScrolled = instrumentClipView.lastAuditionedYDisplay + clip->yScroll;
			if (noteRowIndex != lastAuditionedYDisplayScrolled) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				int32_t yScrollAdjustment = noteRowIndex - lastAuditionedYDisplayScrolled;

				instrumentClipView.scrollVertical(yScrollAdjustment, sdRoutineLock, false, modelStack);
			}
		}
	}
}

// mod encoder action

// used to change the value of a step when you press and hold a pad on the timeline
// used to record live automations in
// While a macro lane is selected, holding a param-select button momentarily quick-edits that
// target (select=CC, gold knobs=From/To). Otherwise the buttons behave normally.
void AutomationView::modButtonAction(uint8_t whichButton, bool on) {
	// Handle release FIRST and unconditionally: the lane/editor state may have changed mid-hold, and
	// leaving heldTarget set would silently re-route the encoders next time a macro lane is shown.
	if (!on) {
		if (heldTarget >= 0) {
			commitHeldTargetDestination();
			heldTarget = -1;
			heldTargetMacro = -1;
			display->cancelPopup();        // the readout is persistent for the duration of the hold
			refreshMacroInactivePopup();   // the hold readout displaced the Inactive status - restore it
			view.setModLedStates();        // re-sync the assignment LEDs (pending value may have been dropped)
			view.setKnobIndicatorLevels(); // the bars showed From/To during the hold - restore them
			if (getCurrentUI() == &automationView) {
				blinkShortcuts(); // re-arm the macro-pad blinking the picker overlay stopped
			}
			uiNeedsRendering(&automationView, 0xFFFFFFFF, 0); // drop the picker overlay, back to the editor
			return;                                           // we consumed the press, so consume the matching release
		}
		ClipView::modButtonAction(whichButton, on);
		return;
	}

	int32_t macroIndex = 0;
	Output* instrument =
	    (inAutomationEditor() && !onArrangerView) ? macroLaneInstrument(getCurrentClip(), &macroIndex) : nullptr;
	if (instrument != nullptr && whichButton < Macros::kNumTargetSlots) {
		heldTarget = whichButton;
		heldTargetMacro = macroIndex;
		macroPickerLastX = -1; // start a fresh picker hold: no "same pad again" carried over
		macroPickerLastY = -1;
		macroPickerSecondLayer = false;
		uint8_t destination = instrument->macros[macroIndex].targets[whichButton].destination;
		heldTargetPendingDestination = (destination == Macros::kNoDestination) ? -1 : destination;
		// initial press: an in-use target reads out who owns its destination
		Macros::showTargetRangeReadout(instrument, macroIndex, whichButton, destination, true);
		Macros::showTargetKnobIndicators(instrument, macroIndex, whichButton);
		// the main grid flips to the destination-picker overlay for the duration of the hold; its
		// pads mean params now, so stop the shortcut blinking (re-armed on release)
		resetShortcutBlinking();
		uiNeedsRendering(&automationView, 0xFFFFFFFF, 0);
		return;
	}
	ClipView::modButtonAction(whichButton, on);
}

// Commits a CC dialed during the hold: one undoable step that clears the old CC's bake and bakes the
// new one. Deferred to release so scrolling never wipes the CCs passed through.
void AutomationView::commitHeldTargetDestination() {
	Clip* clip = getCurrentClip();
	int32_t macroIndex = 0;
	Output* instrument = (inAutomationEditor() && !onArrangerView) ? macroLaneInstrument(clip, &macroIndex) : nullptr;
	if (instrument == nullptr || macroIndex != heldTargetMacro) {
		return; // lane changed mid-hold: drop the pending value rather than commit to the wrong macro
	}
	uint8_t newDestination =
	    (heldTargetPendingDestination < 0) ? Macros::kNoDestination : (uint8_t)heldTargetPendingDestination;
	if (newDestination != instrument->macros[macroIndex].targets[heldTarget].destination) {
		Macros::changeTargetDestination(clip, macroIndex, heldTarget, newDestination);
	}
}

// True while the destination-picker overlay owns the main grid: a target quick-edit button is
// held on the macro lane currently in view.
bool AutomationView::macroPickerActive(Clip* clip) {
	if (heldTarget < 0 || onArrangerView || !inAutomationEditor()) {
		return false;
	}
	int32_t macroIndex = 0;
	return macroLaneInstrument(clip, &macroIndex) != nullptr && macroIndex == heldTargetMacro;
}

// A press on the picker overlay: picks that pad's param as the held target's pending destination,
// with the same live feedback as dialing the select encoder onto it (readout, would-drive LED,
// moved white highlight). The pick is still only committed on button release.
void AutomationView::handleMacroPickerPad(Clip* clip, int32_t x, int32_t y) {
	int32_t macroIndex = 0;
	Output* instrument = macroLaneInstrument(clip, &macroIndex);
	if (instrument == nullptr || macroIndex != heldTargetMacro) {
		return;
	}
	Macros::Domain domain = Macros::domainForOutput(clip->output);
	// Pressing the SAME pad again reaches the param that shares that shortcut (e.g. LFO1->LFO2 rate),
	// like the sound editor's second-layer cycling. A different pad resets to the primary.
	if (x == macroPickerLastX && y == macroPickerLastY && Macros::macroDestinationForPad(domain, x, y, true) >= 0) {
		macroPickerSecondLayer = !macroPickerSecondLayer;
	}
	else {
		macroPickerSecondLayer = false;
	}
	macroPickerLastX = x;
	macroPickerLastY = y;
	int32_t destination = Macros::macroDestinationForPad(domain, x, y, macroPickerSecondLayer);
	if (destination < 0) {
		return; // a dark pad - not a pickable destination
	}
	heldTargetPendingDestination = (int16_t)destination;
	Macros::showTargetRangeReadout(instrument, macroIndex, heldTarget, (uint8_t)destination, false);
	bool wouldDrive = Macros::findShadowingOwner(instrument->macros, (uint8_t)destination, macroIndex, heldTarget) < 0;
	indicator_leds::setLedState(indicator_leds::modLed[heldTarget], wouldDrive);
	uiNeedsRendering(&automationView, 0xFFFFFFFF, 0); // move the white pick highlight
}

// Clears a target's assignment. Routes the CC removal through changeTargetDestination (same as dialing
// the target to OFF) so the baked automation on the target param is deleted (lane back to its unautomated
// default) and, if another target was shadowed on that same destination, ownership hands over and that
// target's macro curve is baked in instead. Then resets the slot's remaining config (from/to/send) to
// defaults. Caller must invoke this while the slot still holds its destination.
bool AutomationView::clearMacroTargetAssignment(int32_t macroIndex, int32_t target) {
	int32_t laneMacroIndex = 0;
	Clip* clip = getCurrentClip();
	Output* instrument = macroLaneInstrument(clip, &laneMacroIndex);
	bool ok = instrument != nullptr && macroIndex >= 0 && target >= 0 && laneMacroIndex == macroIndex;
	if (ok) {
		// must run while the slot still holds its CC (changeTargetDestination reads it as the old
		// destination); clearing to kNoDestination also resets the slot's range/send to pristine defaults.
		Macros::changeTargetDestination(clip, macroIndex, target, Macros::kNoDestination);
		Macros::markHostEdited(instrument);
		view.setModLedStates(); // that target's assignment LED goes dark
		display->displayPopup("Target cleared");
	}
	return ok;
}

bool AutomationView::refuseEditIfMacroDriven(Clip* clip) {
	if (onArrangerView || clip == nullptr) {
		return false;
	}
	if (Macros::isParamMacroDriven(clip, clip->lastSelectedParamKind, clip->lastSelectedParamID)) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_MACRO_DRIVEN));
		return true;
	}
	return false;
}

bool AutomationView::toggleMacroLaneActive() {
	int32_t macroIndex = 0;
	Output* instrument = macroLaneInstrument(getCurrentClip(), &macroIndex);
	if (instrument == nullptr) {
		return false; // not a macro lane - let the encoder press do its normal job
	}
	Macros::Macro* macros = instrument->macros;
	// flips the flag and re-bakes any contested CC lane whose ownership moves with it
	Macros::setMacroActive(getCurrentClip(), macroIndex, !macros[macroIndex].active);
	// the persistent Inactive status popup IS the state display: shown on deactivate, cleared on
	// activate (its absence implies active - no confirmation popup needed)
	refreshMacroInactivePopup();
	renderDisplay();
	uiNeedsRendering(this); // the lane renders dimmed while inactive - update the grid brightness
	blinkShortcuts();       // start/stop the macro-pad blinking to match the new active state
	view.setModLedStates(); // conflicted targets' LEDs start/stop blinking with the active state
	return true;
}

void AutomationView::openMacroLaneEditor(Clip* clip, int32_t macroIndex) {
	if (!Macros::isEnabled() || Macros::macroHost(clip) == nullptr) {
		return;
	}
	// Point the clip's selected param at macro `macroIndex`'s lane, so automation view opens straight
	// into the editor for it (a non-empty lastSelectedParamID makes inAutomationEditor() true).
	if (clip->output->type == OutputType::SYNTH) {
		clip->lastSelectedParamKind = params::Kind::UNPATCHED_SOUND;
		clip->lastSelectedParamID = params::UNPATCHED_MACRO_1 + macroIndex;
		clip->lastSelectedParamArrayPosition = macroIndex;
	}
	else if (Macros::domainForOutput(clip->output) == Macros::Domain::GLOBAL) { // audio clip or kit affect-entire
		clip->lastSelectedParamKind = params::Kind::UNPATCHED_GLOBAL;
		clip->lastSelectedParamID = params::UNPATCHED_GLOBAL_MACRO_1 + macroIndex;
		clip->lastSelectedParamArrayPosition = macroIndex; // lanes are the first four globalParamsForAutomation entries
	}
	else { // MIDI_OUT: the lane is tracked by paramID alone (kind unused)
		clip->lastSelectedParamKind = params::Kind::NONE;
		clip->lastSelectedParamID = Macros::paramIDForMacro(macroIndex);
	}
	clip->lastSelectedParamShortcutX = kNoSelection; // a macro lane has no grid shortcut pad
	clip->lastSelectedParamShortcutY = kNoSelection;
	clip->lastSelectedOutputType = clip->output->type; // else initializeView() resets the selection
	changeRootUI(&automationView);
}

// Keeps a persistent "Inactive" status popup up while an inactive macro lane is in view (the lane
// name behind it already says which macro, so the popup doesn't repeat it), and clears it
// otherwise. openUI()/changeRootUI() clear it on any UI transition; focusRegained() re-shows it on
// return when still applicable.
void AutomationView::refreshMacroInactivePopup() {
	int32_t macroIndex = 0;
	Output* instrument = (getCurrentUI() == &automationView && inAutomationEditor() && !onArrangerView)
	                         ? macroLaneInstrument(getCurrentClip(), &macroIndex)
	                         : nullptr;
	// while a quick-edit button is held, its readout owns the popup
	if (instrument != nullptr && !instrument->macros[macroIndex].active && heldTarget < 0) {
		display->popupText("Inactive", PopupType::MACRO_INACTIVE);
	}
	else if (display->hasPopupOfType(PopupType::MACRO_INACTIVE)) {
		display->cancelPopup();
	}
}

void AutomationView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {

	// if we're in automation overview or note editor
	// then we want to change the value of the parameter assigned to the mod encoder
	if (!inAutomationEditor()) {
		ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
		return;
	}

	// While holding a param button on a macro lane, the gold knobs edit the target's From/To.
	if (heldTarget >= 0) {
		Clip* heldClip = getCurrentClip();
		int32_t macroIndex = 0;
		Output* instrument = macroLaneInstrument(heldClip, &macroIndex);
		if (instrument != nullptr && macroIndex == heldTargetMacro) {
			// Shaping the range implicitly confirms a destination still pending from the select
			// encoder - commit it now so From/To edit (and read out) the just-dialed CC.
			commitHeldTargetDestination();
			Macros::editTargetEndpoint(heldClip, instrument, macroIndex, heldTarget, whichModEncoder, offset);
			Macros::MacroTargetSlot& f = instrument->macros[macroIndex].targets[heldTarget];
			// always show the full "From X To Y" readout - even for an in-use target, so its range
			// stays editable like any other; the conflict shows as the blinking LED after release
			Macros::showTargetRangeReadout(instrument, macroIndex, heldTarget, f.destination, false);
			Macros::showTargetKnobIndicators(instrument, macroIndex, heldTarget);
			return;
		}
	}

	// A macro-driven param can't be edited from its own lane - the macro overwrites its value each tick,
	// so the knob would just snap back. Nudge to the macro instead of writing.
	if (refuseEditIfMacroDriven(getCurrentClip())) {
		return;
	}

	// ok we're on the automation editor, so both mod encoders will edit the currently selected parameter
	// we have two possible actions: step editing or editing current value

	// now we need to setup the model stack with param for the selected parameter in the selected context

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = getCurrentClip();
		modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
	}

	// if we don't have a model stack or auto param, then no parameter to edit, so return early
	if (!modelStackWithParam || !modelStackWithParam->autoParam) {
		return;
	}

	int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

	// if user holding a node down, or we're in pad selection mode
	// we'll adjust the value of the selected parameter being automated at the step selected
	bool is_step_editing = isUIModeActive(UI_MODE_NOTES_PRESSED) || padSelectionOn;

	if (is_step_editing) {
		if ((instrumentClipView.numEditPadPresses > 0
		     && ((int32_t)(instrumentClipView.timeLastEditPadPress + 80 * 44 - AudioEngine::audioSampleTimer) < 0))
		    || padSelectionOn) {

			if (automationEditorLayoutModControllable.automationModEncoderActionForSelectedPad(
			        modelStackWithParam, whichModEncoder, offset, effectiveLength)) {
				return;
			}
		}
	}
	// if playback is enabled and you are recording, you will be able to record in live automations for
	// the selected parameter this code is also executed if you're just changing the current value of
	// the parameter at the current mod position
	else {
		automationEditorLayoutModControllable.automationModEncoderActionForUnselectedPad(
		    modelStackWithParam, whichModEncoder, offset, effectiveLength);
	}

	uiNeedsRendering(&automationView);
}

// used to change gold knob parameter, copy paste automation or to delete automation of the current selected parameter
void AutomationView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	// if we're in automation overview or note editor
	// then we want to allow toggling with mod encoder buttons to change
	// mod encoder selections or copy / paste
	if (!inAutomationEditor()) {
		instrumentClipView.modEncoderButtonAction(whichModEncoder, on);
		// if we're on automation overview, re-render because we want to show automated params
		if (onAutomationOverview()) {
			uiNeedsRendering(&automationView);
		}
		return;
	}

	// if we're not trying to copy / paste (holding learn) and not trying to delete (holding shift), return
	// if we're releasing mod encoder button action, return (we don't do anything on release)
	bool is_learn_pressed = Buttons::isButtonPressed(hid::button::LEARN);
	bool is_shift_pressed = Buttons::isShiftButtonPressed();
	bool is_copy_action = is_learn_pressed && !is_shift_pressed;
	bool is_paste_action = is_learn_pressed && is_shift_pressed;
	bool is_delete_action = !is_learn_pressed & is_shift_pressed;
	if (!on || (!is_copy_action && !is_paste_action && !is_delete_action)) {
		return;
	}

	// if we got here then we're in automation editor and we want to copy / paste or delete automation

	// now we need to setup the model stack with param for the selected parameter in the selected context

	Clip* clip = getCurrentClip();
	OutputType outputType = clip->output->type;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
	}

	// if we don't have a model stack with param or auto param, then no automation to copy / paste or delete, so return
	if (!modelStackWithParam || !modelStackWithParam->autoParam) {
		return;
	}

	int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

	int32_t xScroll = currentSong->xScroll[navSysId];
	int32_t xZoom = currentSong->xZoom[navSysId];

	// if they want to copy automation...
	if (is_copy_action) {
		automationEditorLayoutModControllable.copyAutomation(modelStackWithParam, clip, xScroll, xZoom);
	}
	// if they want to paste automation
	else if (is_paste_action) {
		automationEditorLayoutModControllable.pasteAutomation(modelStackWithParam, clip, effectiveLength, xScroll,
		                                                      xZoom);
	}
	// if they want to delete automation
	else if (is_delete_action) {
		// each delete is its own undo step; the target cascade below joins it via the action pointer
		Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE);
		modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

		// clearing a macro lane clears its target lanes too, in the same undo step
		int32_t clearedMacroIndex = 0;
		if (!onArrangerView && macroLaneInstrument(clip, &clearedMacroIndex) != nullptr) {
			Macros::reFanMacro(clip, clearedMacroIndex, action);
		}

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));

		displayAutomation(padSelectionOn, !display->have7SEG());
	}

	// refresh automation editor grid to show copy / pasted automation or deleted automation
	uiNeedsRendering(&automationView);
}

// select encoder action

// used to change the parameter selection and reset shortcut pad settings so that new pad can be blinked
// once parameter is selected
// used to fine tune the values of non-midi parameters
void AutomationView::selectEncoderAction(int8_t offset) {
	// 5x acceleration of select encoder when holding the shift button
	if (Buttons::isButtonPressed(deluge::hid::button::SHIFT)) {
		offset = offset * 5;
	}

	// change midi CC or param ID
	Clip* clip = getCurrentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	// While holding a param button on a macro lane, the select encoder dials the target's destination
	// CC (rather than switching lanes). The value is only COMMITTED on button release (see
	// commitHeldTargetDestination), so scrolling never touches the automation of the CCs passed through.
	if (heldTarget >= 0 && inAutomationEditor()) {
		int32_t macroIndex = 0;
		Output* instrument = macroLaneInstrument(clip, &macroIndex);
		if (instrument != nullptr && macroIndex == heldTargetMacro) {
			// One continuous dial: OFF, then the domain's destinations (CC 0-127 on MIDI clips, the
			// targetable param list on synths), then this macro's cascade destinations (the
			// higher-indexed macros it may target, shown as "Macro N"). Destination bytes aren't
			// contiguous per-macro, so step in "dial position" space.
			Macros::Domain domain = Macros::domainForOutput(output);
			int32_t maxPosition = Macros::numDestinations(domain, macroIndex) - 1;
			int32_t position =
			    (heldTargetPendingDestination < 0)
			        ? -1
			        : Macros::positionForDestination(domain, macroIndex, (uint8_t)heldTargetPendingDestination);
			position = std::clamp<int32_t>(position + offset, -1, maxPosition);
			heldTargetPendingDestination =
			    (position < 0) ? -1 : Macros::destinationForPosition(domain, macroIndex, position);
			uint8_t pendingDestination =
			    (heldTargetPendingDestination < 0) ? Macros::kNoDestination : (uint8_t)heldTargetPendingDestination;
			// Dialing stays normal even onto an in-use destination (pick it, then shape From/To with
			// the gold knobs); only the LED hints it won't drive, and it blinks after release.
			Macros::showTargetRangeReadout(instrument, macroIndex, heldTarget, pendingDestination, false);
			bool wouldDrive =
			    pendingDestination != Macros::kNoDestination
			    && Macros::findShadowingOwner(instrument->macros, pendingDestination, macroIndex, heldTarget) < 0;
			// the held button's LED tracks the pending assignment live
			indicator_leds::setLedState(indicator_leds::modLed[heldTarget], wouldDrive);
			uiNeedsRendering(&automationView, 0xFFFFFFFF, 0); // the picker overlay's white highlight follows
			return;
		}
	}

	// if you've selected a mod encoder (e.g. by pressing modEncoderButton) and you're in Automation
	// Overview the currentUIMode will change to Selecting Midi CC. In this case, turning select encoder
	// should allow you to change the midi CC assignment to that modEncoder
	if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
		InstrumentClipMinder::selectEncoderAction(offset);
		return;
	}
	// don't allow switching to automation editor if you're holding the audition pad in arranger
	// automation view
	else if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		return;
	}
	// edit row or note probability or iterance
	else if (inNoteEditor()) {
		// only allow adjusting probability / iterance while holding note
		if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
			instrumentClipView.handleProbabilityOrIteranceEditing(offset, false);
			timeSelectKnobLastReleased = AudioEngine::audioSampleTimer;
			probabilityChanged = true;
		}
		// only allow adjusting row probability / iterance while holding audition
		else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
			instrumentClipView.handleProbabilityOrIteranceEditing(offset, true);
			timeSelectKnobLastReleased = AudioEngine::audioSampleTimer;
			probabilityChanged = true;
		}
		return;
	}
	// if you're in a midi clip
	else if (outputType == OutputType::MIDI_OUT) {
		selectMIDICC(offset, clip);
		getLastSelectedParamShortcut(clip);
		// on a macro lane the mod-button LEDs show which targets are assigned - refresh per lane
		view.setModLedStates();
		// landing on (or leaving) an inactive macro lane shows/clears its persistent status popup
		refreshMacroInactivePopup();
	}
	// if you're in arranger view or in a non-midi, non-cv clip (e.g. audio, synth, kit)
	else if (onArrangerView || outputType != OutputType::CV) {
		// if you're in a audio clip, a kit with affect entire enabled, or in arranger view
		if (onArrangerView || (outputType == OutputType::AUDIO)
		    || (outputType == OutputType::KIT && getAffectEntire())) {
			selectGlobalParam(offset, clip);
		}
		// if you're a synth or a kit (with affect entire off and a sound drum selected)
		else if (outputType == OutputType::SYNTH
		         || (outputType == OutputType::KIT && ((Kit*)output)->selectedDrum
		             && ((Kit*)output)->selectedDrum->type == DrumType::SOUND)) {
			selectNonGlobalParam(offset, clip);
			// on a macro lane the mod-button LEDs show which targets are assigned - refresh per lane
			view.setModLedStates();
			// landing on (or leaving) an inactive macro lane shows/clears its persistent status popup
			refreshMacroInactivePopup();
		}
		// don't have patch cable blinking logic figured out yet
		if (clip->lastSelectedParamKind == params::Kind::PATCH_CABLE) {
			clip->lastSelectedParamShortcutX = kNoSelection;
			clip->lastSelectedParamShortcutY = kNoSelection;
		}
		else {
			getLastSelectedParamShortcut(clip);
		}
	}
	// if you're in a CV clip or function is called for some other reason, do nothing
	else {
		return;
	}

	// update name on display, the LED mod indicators, and refresh the grid
	lastPadSelectedKnobPos = kNoSelection;
	if (multiPadPressSelected && padSelectionOn) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (onArrangerView) {
			modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
			modelStackWithParam =
			    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
		}
		else {
			modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
		}
		int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);
		int32_t xScroll = currentSong->xScroll[navSysId];
		int32_t xZoom = currentSong->xZoom[navSysId];
		automationEditorLayoutModControllable.renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip,
		                                                                              effectiveLength, xScroll, xZoom);
	}
	else {
		displayAutomation(true, !display->have7SEG());
	}
	resetParameterShortcutBlinking();
	blinkShortcuts();
	view.setModLedStates();
	uiNeedsRendering(&automationView);
}

// used with SelectEncoderAction to get the next arranger / audio clip / kit affect entire parameter
void AutomationView::selectGlobalParam(int32_t offset, Clip* clip) {
	if (onArrangerView) {
		auto idx = getNextSelectedParamArrayPosition(offset, currentSong->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		{
			// Skip the params the arranger has no use for, plus its macro lanes (it can't drive macros).
			while ((id == params::UNPATCHED_PITCH_ADJUST || id == params::UNPATCHED_SIDECHAIN_SHAPE
			        || id == params::UNPATCHED_SIDECHAIN_VOLUME || id == params::UNPATCHED_COMPRESSOR_THRESHOLD
			        || (id >= params::UNPATCHED_FIRST_ARP_PARAM && id <= params::UNPATCHED_LAST_ARP_PARAM)
			        || id == params::UNPATCHED_ARP_RATE || skipGlobalMacroLaneInScroll(id, false))) {

				if (offset < 0) {
					offset -= 1;
				}
				else if (offset > 0) {
					offset += 1;
				}
				idx = getNextSelectedParamArrayPosition(offset, currentSong->lastSelectedParamArrayPosition,
				                                        kNumGlobalParamsForAutomation);
				id = globalParamsForAutomation[idx].second;
			}
		}
		currentSong->lastSelectedParamID = id;
		currentSong->lastSelectedParamKind = kind;
		currentSong->lastSelectedParamArrayPosition = idx;
	}
	else if (clip->output->type == OutputType::AUDIO) {
		auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		{
			// Skip the arp params (audio clips have no arp), plus the macro lanes when the feature is off.
			while ((id >= params::UNPATCHED_FIRST_ARP_PARAM && id <= params::UNPATCHED_LAST_ARP_PARAM)
			       || id == params::UNPATCHED_ARP_RATE || skipGlobalMacroLaneInScroll(id, true)) {

				if (offset < 0) {
					offset -= 1;
				}
				else if (offset > 0) {
					offset += 1;
				}
				idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
				                                        kNumGlobalParamsForAutomation);
				id = globalParamsForAutomation[idx].second;
			}
		}
		clip->lastSelectedParamID = id;
		clip->lastSelectedParamKind = kind;
		clip->lastSelectedParamArrayPosition = idx;
	}
	else {
		auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		// Kit affect-entire hosts macros, so its macro lanes stay scrollable when the feature is on -
		// but only while affect-entire actually is on (this branch can also serve menu contexts where
		// getAffectEntire() is forced true with the clip's own flag off).
		while (skipGlobalMacroLaneInScroll(id, view.macroKnobModeAvailable(clip))) {
			if (offset < 0) {
				offset -= 1;
			}
			else if (offset > 0) {
				offset += 1;
			}
			idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
			                                        kNumGlobalParamsForAutomation);
			id = globalParamsForAutomation[idx].second;
		}
		clip->lastSelectedParamID = id;
		clip->lastSelectedParamKind = kind;
		clip->lastSelectedParamArrayPosition = idx;
	}
	automationParamType = AutomationParamType::PER_SOUND;
}

// used with SelectEncoderAction to get the next synth or kit non-affect entire param
void AutomationView::selectNonGlobalParam(int32_t offset, Clip* clip) {
	bool foundPatchCable = false;
	// if we previously selected a patch cable, we'll see if there are any more to scroll through
	if (clip->lastSelectedParamKind == params::Kind::PATCH_CABLE) {
		foundPatchCable = selectPatchCable(offset, clip);
		// did we find another patch cable?
		if (!foundPatchCable) {
			// if we haven't found a patch cable, it means we reached beginning or end of patch cable
			// list if we're scrolling right, we'll resume with selecting a regular param from beg of
			// list if we're scrolling left, we'll resume with selecting a regular param from end of
			// list to do so we re-set the last selected param array position

			// scrolling right
			if (offset > 0) {
				clip->lastSelectedParamArrayPosition = kNumNonGlobalParamsForAutomation - 1;
			}
			// scrolling left
			else if (offset < 0) {
				clip->lastSelectedParamArrayPosition = 0;
			}
		}
	}
	// if we didn't find anymore patch cables, then we'll select a regular param from the list
	if (!foundPatchCable) {
		auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
		                                             kNumNonGlobalParamsForAutomation);
		{
			// Skip entries that aren't selectable on this clip: portamento on kit rows, and the
			// macro lanes (the first four entries) when the macro feature is off or the output
			// isn't a synth. Step one entry at a time in the scroll direction; the list always
			// contains selectable params, so this terminates.
			int32_t step = (offset >= 0) ? 1 : -1;
			while (true) {
				auto [kind, id] = nonGlobalParamsForAutomation[idx];
				bool kitPortamento = (clip->output->type == OutputType::KIT) && (kind == params::Kind::UNPATCHED_SOUND)
				                     && (id == params::UNPATCHED_PORTAMENTO);
				bool skippedMacroLane = (kind == params::Kind::UNPATCHED_SOUND) && (id >= params::UNPATCHED_MACRO_1)
				                        && (id <= params::UNPATCHED_MACRO_4)
				                        && (!Macros::isEnabled() || clip->output->type != OutputType::SYNTH);
				if (!kitPortamento && !skippedMacroLane) {
					break;
				}
				idx = (idx + step + kNumNonGlobalParamsForAutomation) % kNumNonGlobalParamsForAutomation;
			}
		}

		// did we reach beginning or end of list?
		// if yes, then let's scroll through patch cables
		// but only if we haven't already scrolled through patch cables already above
		if ((clip->lastSelectedParamKind != params::Kind::PATCH_CABLE)
		    && (((offset > 0) && (idx < clip->lastSelectedParamArrayPosition))
		        || ((offset < 0) && (idx > clip->lastSelectedParamArrayPosition)))) {
			foundPatchCable = selectPatchCable(offset, clip);
		}

		// if we didn't find a patch cable, then we'll resume with scrolling the non-patch cable list
		if (!foundPatchCable) {
			auto [kind, id] = nonGlobalParamsForAutomation[idx];
			clip->lastSelectedParamID = id;
			clip->lastSelectedParamKind = kind;
			clip->lastSelectedParamArrayPosition = idx;
		}
	}
	automationParamType = AutomationParamType::PER_SOUND;
}

// iterate through the patch cable list to select the previous or next patch cable
// actual selecting of the patch cable is done in the selectPatchCableAtIndex function
bool AutomationView::selectPatchCable(int32_t offset, Clip* clip) {
	ParamManagerForTimeline* paramManager = clip->getCurrentParamManager();
	if (paramManager) {
		PatchCableSet* set = paramManager->getPatchCableSetAllowJibberish();
		// make sure it's not jiberish
		if (set) {
			// do we have any patch cables?
			if (set->numPatchCables > 0) {
				bool foundCurrentPatchCable = false;
				// scrolling right
				if (offset > 0) {
					// loop from beginning to end of patch cable list
					for (int i = 0; i < set->numPatchCables; i++) {
						// loop through patch cables until we've found a new one and select it
						// adjacent to current found patch cable (if we previously selected one)
						if (selectPatchCableAtIndex(clip, set, i, foundCurrentPatchCable)) {
							return true;
						}
					}
				}
				// scrolling left
				else if (offset < 0) {
					// loop from end to beginning of patch cable list
					for (int i = set->numPatchCables - 1; i >= 0; i--) {
						// loop through patch cables until we've found a new one and select it
						// adjacent to current found patch cable (if we previously selected one)
						if (selectPatchCableAtIndex(clip, set, i, foundCurrentPatchCable)) {
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

// this function does the actual selecting of a patch cable
// see if the patch cable selected is different from current one selected (or not selected)
// if we havent already selected a patch cable, we'll select this one
// if we selected one previously, we'll see if this one is adjacent to the previous one selected
// if it's adjacent to the previous one selected, we'll select this one
bool AutomationView::selectPatchCableAtIndex(Clip* clip, PatchCableSet* set, int32_t patchCableIndex,
                                             bool& foundCurrentPatchCable) {
	PatchCable* cable = &set->patchCables[patchCableIndex];
	ParamDescriptor desc = cable->destinationParamDescriptor;
	// need to add patch cable source to the descriptor so that we can get the paramId from it
	desc.addSource(cable->from);

	// if we've previously selected a patch cable, we want to start scrolling from that patch cable
	// note: the reason why we can't save the patchCableIndex to make finding the previous patch
	// cable selected easier is because the patch cable array gets re-indexed as patch cables get
	// added or removed or values change. Thus you need to search for the previous patch cable to get
	// the updated index and then you can find the adjacent patch cable in the list.
	if (desc.data == clip->lastSelectedParamID) {
		foundCurrentPatchCable = true;
	}
	// if we found the patch cable we previously selected and we found another one
	// or we hadn't selected a patch cable previously and found a patch cable
	// select the one we found
	else if ((foundCurrentPatchCable || (clip->lastSelectedParamKind != params::Kind::PATCH_CABLE))
	         && (desc.data != clip->lastSelectedParamID)) {
		clip->lastSelectedPatchSource = cable->from;
		clip->lastSelectedParamID = desc.data;
		clip->lastSelectedParamKind = params::Kind::PATCH_CABLE;
		return true;
	}
	return false;
}

// used with SelectEncoderAction to get the next midi CC
// The MIDI automation param order is a single ordered space: macro lanes 128-131 sit BEFORE CC 0, then
// CC 0..119, then the specials (pitch bend/aftertouch/Y). We map to a contiguous "position" where macros
// are negative (-kNumMacroParams..-1) and CCs/specials are 0..CC_NUMBER_Y_AXIS so scrolling is simple.
static int32_t midiCCToOrderedPos(int32_t id) {
	if (Macros::isMacroParamID(id)) {
		return Macros::macroIndexFromParamID(id) - Macros::kNumMacroParams;
	}
	return id;
}
static int32_t midiOrderedPosToCC(int32_t pos) {
	if (pos < 0) {
		return Macros::paramIDForMacro(pos + Macros::kNumMacroParams);
	}
	return pos;
}

void AutomationView::selectMIDICC(int32_t offset, Clip* clip) {
	const bool macros = Macros::isEnabled();
	const int32_t firstPos = macros ? -Macros::kNumMacroParams : 0; // macro 1, or CC 0
	const int32_t lastPos = CC_NUMBER_Y_AXIS;                       // 122

	int32_t pos;
	if (onAutomationOverview()) {
		pos = (offset >= 0) ? firstPos : lastPos; // first scroll lands on the first (or last) lane
	}
	else {
		pos = midiCCToOrderedPos(clip->lastSelectedParamID) + offset;
		if (pos < firstPos) {
			pos = lastPos;
		}
		else if (pos > lastPos) {
			pos = firstPos;
		}
		// CC 1 (external mod wheel) is internally CC_NUMBER_Y_AXIS, so skip it in the scroll direction
		if (pos == CC_EXTERNAL_MOD_WHEEL) {
			pos += (offset >= 0) ? 1 : -1;
		}
	}
	clip->lastSelectedParamID = midiOrderedPosToCC(pos);
	automationParamType = AutomationParamType::PER_SOUND;
}

// used with SelectEncoderAction to get the next parameter in the list of parameters
int32_t AutomationView::getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamArrayPosition,
                                                          int32_t numParams) {
	int32_t idx;
	// if you haven't selected a parameter yet, start at the beginning of the list
	if (onAutomationOverview()) {
		idx = 0;
	}
	// if you are scrolling left and are at the beginning of the list, go to the end of the list
	else if ((lastSelectedParamArrayPosition + offset) < 0) {
		idx = numParams + offset;
	}
	// if you are scrolling right and are at the end of the list, go to the beginning of the list
	else if ((lastSelectedParamArrayPosition + offset) > (numParams - 1)) {
		idx = 0;
	}
	// otherwise scrolling left/right within the list
	else {
		idx = lastSelectedParamArrayPosition + offset;
	}
	return idx;
}

// used with Select Encoder action to get the X, Y grid shortcut coordinates of the parameter selected
void AutomationView::getLastSelectedParamShortcut(Clip* clip) {
	bool paramShortcutFound = false;
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (onArrangerView) {
				if (unpatchedGlobalParamShortcuts[x][y] == currentSong->lastSelectedParamID) {
					currentSong->lastSelectedParamShortcutX = x;
					currentSong->lastSelectedParamShortcutY = y;
					paramShortcutFound = true;
					break;
				}
			}
			else if (clip->output->type == OutputType::MIDI_OUT) {
				if (midiFollow.midiCCShortcutsForAutomation[x][y] == clip->lastSelectedParamID) {
					clip->lastSelectedParamShortcutX = x;
					clip->lastSelectedParamShortcutY = y;
					paramShortcutFound = true;
					break;
				}
			}
			else {
				if ((clip->lastSelectedParamKind == params::Kind::PATCHED
				     && patchedParamShortcuts[x][y] == clip->lastSelectedParamID)
				    || (clip->lastSelectedParamKind == params::Kind::UNPATCHED_SOUND
				        && unpatchedNonGlobalParamShortcuts[x][y] == clip->lastSelectedParamID)
				    || (clip->lastSelectedParamKind == params::Kind::UNPATCHED_GLOBAL
				        && unpatchedGlobalParamShortcuts[x][y] == clip->lastSelectedParamID)
				    || (clip->lastSelectedParamKind == params::Kind::EXPRESSION
				        && params::expressionParamFromShortcut(x, y) == clip->lastSelectedParamID)) {
					clip->lastSelectedParamShortcutX = x;
					clip->lastSelectedParamShortcutY = y;
					paramShortcutFound = true;
					break;
				}
			}
		}
		if (paramShortcutFound) {
			break;
		}
	}
	if (!paramShortcutFound) {
		if (onArrangerView) {
			currentSong->lastSelectedParamShortcutX = kNoSelection;
			currentSong->lastSelectedParamShortcutY = kNoSelection;
		}
		else {
			clip->lastSelectedParamShortcutX = kNoSelection;
			clip->lastSelectedParamShortcutY = kNoSelection;
		}
	}
}

void AutomationView::getLastSelectedParamArrayPosition(Clip* clip) {
	Output* output = clip->output;
	OutputType outputType = output->type;

	// if you're in arranger view or in a non-midi, non-cv clip (e.g. audio, synth, kit)
	if (onArrangerView || outputType != OutputType::CV) {
		// if you're in a audio clip, a kit with affect entire enabled, or in arranger view
		if (onArrangerView || (outputType == OutputType::AUDIO)
		    || (outputType == OutputType::KIT && getAffectEntire())) {
			getLastSelectedGlobalParamArrayPosition(clip);
		}
		// if you're a synth or a kit (with affect entire off and a drum selected)
		else if (outputType == OutputType::SYNTH
		         || (outputType == OutputType::KIT && ((Kit*)output)->selectedDrum
		             && ((Kit*)output)->selectedDrum->type == DrumType::SOUND)) {
			getLastSelectedNonGlobalParamArrayPosition(clip);
		}
	}
}

void AutomationView::getLastSelectedNonGlobalParamArrayPosition(Clip* clip) {
	for (auto idx = 0; idx < kNumNonGlobalParamsForAutomation; idx++) {

		auto [kind, id] = nonGlobalParamsForAutomation[idx];

		if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
			clip->lastSelectedParamArrayPosition = idx;
			break;
		}
	}
}

void AutomationView::getLastSelectedGlobalParamArrayPosition(Clip* clip) {
	for (auto idx = 0; idx < kNumGlobalParamsForAutomation; idx++) {

		auto [kind, id] = globalParamsForAutomation[idx];

		if (onArrangerView) {
			if ((id == currentSong->lastSelectedParamID) && (kind == currentSong->lastSelectedParamKind)) {
				currentSong->lastSelectedParamArrayPosition = idx;
				break;
			}
		}
		else {
			if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
				clip->lastSelectedParamArrayPosition = idx;
				break;
			}
		}
	}
}

// called by melodic_instrument.cpp or kit.cpp
void AutomationView::noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) {
	instrumentClipView.noteRowChanged(clip, noteRow);
}

// called by playback_handler.cpp
void AutomationView::notifyPlaybackBegun() {
	if (!onArrangerView && getCurrentClip()->type != ClipType::AUDIO) {
		instrumentClipView.reassessAllAuditionStatus();
	}
}

// resets the Parameter Selection which sends you back to the Automation Overview screen
// these values are saved on a clip basis
void AutomationView::initParameterSelection(bool updateDisplay) {
	resetShortcutBlinking();
	initPadSelection();

	if (onArrangerView) {
		currentSong->lastSelectedParamID = params::kNoParamID;
		currentSong->lastSelectedParamKind = params::Kind::NONE;
		currentSong->lastSelectedParamShortcutX = kNoSelection;
		currentSong->lastSelectedParamShortcutY = kNoSelection;
		currentSong->lastSelectedParamArrayPosition = 0;
	}
	else {
		Clip* clip = getCurrentClip();
		clip->lastSelectedParamID = params::kNoParamID;
		clip->lastSelectedParamKind = params::Kind::NONE;
		clip->lastSelectedParamShortcutX = kNoSelection;
		clip->lastSelectedParamShortcutY = kNoSelection;
		clip->lastSelectedPatchSource = PatchSource::NONE;
		clip->lastSelectedParamArrayPosition = 0;

		// if you're on automation overview, turn led off if it's on
		if (clip->type == ClipType::INSTRUMENT && ((InstrumentClip*)clip)->wrapEditing) {
			indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
		}
	}

	automationParamType = AutomationParamType::PER_SOUND;

	// if we're going back to the Automation Overview, set the display to show "Automation Overview"
	// and update the knob indicator levels to match the master FX button selected
	display->cancelPopup();
	view.setKnobIndicatorLevels();
	view.setModLedStates();
	if (updateDisplay) {
		renderDisplay();
	}
}

// exit pad selection mode, reset pad press statuses
void AutomationView::initPadSelection() {
	padSelectionOn = false;
	multiPadPressSelected = false;
	multiPadPressActive = false;
	middlePadPressSelected = false;
	leftPadSelectedX = kNoSelection;
	rightPadSelectedX = kNoSelection;
	lastPadSelectedKnobPos = kNoSelection;

	resetPadSelectionShortcutBlinking();
}

void AutomationView::initInterpolation() {
	interpolationBefore = false;
	interpolationAfter = false;
}

// get's the modelstack for the parameters that are being edited
// the model stack differs for SYNTH's, KIT's, MIDI, and Audio clip's
ModelStackWithAutoParam* AutomationView::getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStack,
                                                                       Clip* clip, int32_t paramID,
                                                                       params::Kind paramKind) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (paramID == kNoParamID) {
		paramID = clip->lastSelectedParamID;
		paramKind = clip->lastSelectedParamKind;
	}

	// check if we're in the sound menu and not the settings menu
	// because in the settings menu, the menu mod controllable's aren't setup, so we don't want to use those
	bool inSoundMenu = getCurrentUI() == &soundEditor && !soundEditor.inSettingsMenu();

	modelStackWithParam =
	    clip->output->getModelStackWithParam(modelStack, clip, paramID, paramKind, getAffectEntire(), inSoundMenu);

	return modelStackWithParam;
}

// this function obtains a parameters value and converts it to a knobPos
// the knobPos is used for rendering the current parameter values in the automation editor
// it's also used for obtaining the start and end position values for a multi pad press
// and also used for increasing/decreasing parameter values with the mod encoders
int32_t AutomationView::getAutomationParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t squareStart) {
	return automationEditorLayoutModControllable.getAutomationParameterKnobPos(modelStack, squareStart);
}

// sets both knob indicators to the same value when pressing single pad,
// deleting automation, or displaying current parameter value
// multi pad presses don't use this function
void AutomationView::setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
                                                      int32_t knobPosRight) {
	automationEditorLayoutModControllable.setAutomationKnobIndicatorLevels(modelStack, knobPosLeft, knobPosRight);
}

// calculates the length of the arrangement timeline, clip or the length of the kit row
// if you're in a synth clip, kit clip with affect entire enabled or midi clip it returns clip length
// if you're in a kit clip with affect entire disabled and a row selected, it returns kit row length
int32_t AutomationView::getEffectiveLength(ModelStackWithTimelineCounter* modelStack) {
	Clip* clip = getCurrentClip();
	OutputType outputType = clip->output->type;

	int32_t effectiveLength = 0;

	if (onArrangerView) {
		effectiveLength = arrangerView.getMaxLength();
	}
	else if (outputType == OutputType::KIT && !getAffectEntire()) {
		ModelStackWithNoteRow* modelStackWithNoteRow = ((InstrumentClip*)clip)->getNoteRowForSelectedDrum(modelStack);

		effectiveLength = modelStackWithNoteRow->getLoopLength();
	}
	else {
		// this will differ for a kit when in note row mode
		effectiveLength = clip->loopLength;
	}

	return effectiveLength;
}

uint32_t AutomationView::getMaxLength() {
	if (onArrangerView) {
		return arrangerView.getMaxLength();
	}
	else {
		return getCurrentClip()->getMaxLength();
	}
}

uint32_t AutomationView::getMaxZoom() {
	if (onArrangerView) {
		return arrangerView.getMaxZoom();
	}
	else {
		return getCurrentClip()->getMaxZoom();
	}
}

int32_t AutomationView::getNavSysId() const {
	if (onArrangerView) {
		return NAVIGATION_ARRANGEMENT;
	}
	else {
		return NAVIGATION_CLIP;
	}
}

// used to render automation overview
// used to handle pad actions on automation overview
// used to disable certain actions on the automation overview screen
// e.g. doubling clip length, editing clip length
bool AutomationView::onAutomationOverview() {
	return (!inAutomationEditor() && !inNoteEditor());
}

bool AutomationView::inAutomationEditor() {
	if (onArrangerView) {
		if (currentSong->lastSelectedParamID == params::kNoParamID) {
			return false;
		}
	}
	else if (getCurrentClip()->lastSelectedParamID == params::kNoParamID) {
		return false;
	}

	return true;
}

void AutomationView::setAutomationParamType() {
	automationParamType = AutomationParamType::PER_SOUND;
	if (!inAutomationEditor()) {
		Clip* clip = getCurrentClip();
		if (isNoteVelocityEditorShortcut(clip->lastSelectedParamShortcutX, clip->lastSelectedParamShortcutY)) {
			automationParamType = AutomationParamType::NOTE_VELOCITY;
		}
	}
}

// used to check if we're automating a note row specific param type
// e.g. velocity, probability, poly expression, etc.
bool AutomationView::inNoteEditor() {
	return (automationParamType != AutomationParamType::PER_SOUND);
}

// used to determine the affect entire context
bool AutomationView::getAffectEntire() {
	// arranger view always uses affect entire
	if (onArrangerView) {
		return true;
	}
	// are you in the sound menu for a kit?
	else if (getCurrentOutputType() == OutputType::KIT && getCurrentUI() == &soundEditor
	         && !soundEditor.inSettingsMenu()) {
		// if you're in the kit global FX menu, the menu context is the same as if affect entire is enabled
		if (soundEditor.setupKitGlobalFXMenu) {
			return true;
		}
		// otherwise you're in the kit row context which is the same as if affect entire is disabled
		else {
			return false;
		}
	}
	// otherwise if you're not in the kit sound menu, use the clip affect entire state
	return getCurrentInstrumentClip()->affectEntire;
}

void AutomationView::blinkShortcuts() {
	if (getCurrentUI() == &automationView) {
		int32_t lastSelectedParamShortcutX = kNoSelection;
		int32_t lastSelectedParamShortcutY = kNoSelection;
		if (onArrangerView) {
			lastSelectedParamShortcutX = currentSong->lastSelectedParamShortcutX;
			lastSelectedParamShortcutY = currentSong->lastSelectedParamShortcutY;
		}
		else {
			Clip* clip = getCurrentClip();
			lastSelectedParamShortcutX = clip->lastSelectedParamShortcutX;
			lastSelectedParamShortcutY = clip->lastSelectedParamShortcutY;
		}
		// if a Param has been selected for editing, blink its shortcut pad
		if (lastSelectedParamShortcutX != kNoSelection) {
			if (!parameterShortcutBlinking) {
				soundEditor.setupShortcutBlink(lastSelectedParamShortcutX, lastSelectedParamShortcutY, 10);
				soundEditor.blinkShortcut();

				parameterShortcutBlinking = true;
			}
		}
		// unset previously set blink timers if not editing a parameter
		else {
			resetParameterShortcutBlinking();
		}
	}
	if (interpolation && !inNoteEditor()) {
		if (!interpolationShortcutBlinking) {
			blinkInterpolationShortcut();
		}
	}
	else {
		resetInterpolationShortcutBlinking();
	}
	if (padSelectionOn) {
		blinkPadSelectionShortcut();
	}
	else {
		resetPadSelectionShortcutBlinking();
	}
	if (inNoteEditor()) {
		if (!instrumentClipView.noteRowBlinking) {
			instrumentClipView.blinkSelectedNoteRow();
		}
	}
	else {
		instrumentClipView.resetSelectedNoteRowBlinking();
	}
}

void AutomationView::resetShortcutBlinking() {
	soundEditor.resetSourceBlinks();
	resetParameterShortcutBlinking();
	resetInterpolationShortcutBlinking();
	resetPadSelectionShortcutBlinking();
	instrumentClipView.resetSelectedNoteRowBlinking();
}

// created this function to undo any existing parameter shortcut blinking so that it doesn't get
// rendered in automation view also created it so that you can reset blinking when a parameter is
// deselected or when you enter/exit automation view
void AutomationView::resetParameterShortcutBlinking() {
	uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	parameterShortcutBlinking = false;
}

// created this function to undo any existing interpolation shortcut blinking so that it doesn't get
// rendered in automation view also created it so that you can reset blinking when interpolation is
// turned off or when you enter/exit automation view
void AutomationView::resetInterpolationShortcutBlinking() {
	uiTimerManager.unsetTimer(TimerName::INTERPOLATION_SHORTCUT_BLINK);
	interpolationShortcutBlinking = false;
}

void AutomationView::blinkInterpolationShortcut() {
	PadLEDs::flashMainPad(kInterpolationShortcutX, kInterpolationShortcutY);
	uiTimerManager.setTimer(TimerName::INTERPOLATION_SHORTCUT_BLINK, 3000);
	interpolationShortcutBlinking = true;
}

// used to blink waveform shortcut when in pad selection mode
void AutomationView::resetPadSelectionShortcutBlinking() {
	uiTimerManager.unsetTimer(TimerName::PAD_SELECTION_SHORTCUT_BLINK);
	padSelectionShortcutBlinking = false;
}

void AutomationView::blinkPadSelectionShortcut() {
	PadLEDs::flashMainPad(kPadSelectionShortcutX, kPadSelectionShortcutY);
	uiTimerManager.setTimer(TimerName::PAD_SELECTION_SHORTCUT_BLINK, 3000);
	padSelectionShortcutBlinking = true;
}
