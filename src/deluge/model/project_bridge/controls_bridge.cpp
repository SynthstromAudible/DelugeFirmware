/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "model/project_bridge/controls_bridge.h"

#include "engines/deluge/naming.h"
#include "engines/global_effectable/naming.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/midi_command.h"
#include "model/project_bridge/param_ids.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include <array>

namespace deluge::bridge {

namespace params = deluge::modulation::params;
using deluge::project::ControllerMapping;
using deluge::project::MidiInputControl;
using deluge::project::ModKnobControl;

deluge::engine::ParamId knobTargetOf(ParamDescriptor descriptor, params::Kind unpatchedKind) {
	if (!descriptor.isJustAParam()) {
		return cableDepthIdOf(descriptor.data);
	}
	const int32_t p = descriptor.getJustTheParam();
	if (p >= params::UNPATCHED_START) {
		return paramIdOf(unpatchedKind, p - params::UNPATCHED_START);
	}
	return paramIdOf(params::Kind::PATCHED, p);
}

namespace {

void appendLearned(::ModControllableAudio& owner, params::Kind unpatchedKind, std::vector<ControllerMapping>& into) {
	for (MIDIKnob& knob : owner.midi_knobs) {
		into.push_back({MidiInputControl{command_of(knob.midiInput), knob.relative},
		                knobTargetOf(knob.paramDescriptor, unpatchedKind)});
	}
}

} // namespace

std::vector<ControllerMapping> controlsOf(::Sound& sound) {
	std::vector<ControllerMapping> controls;
	for (uint8_t button = 0; button < kNumModButtons; ++button) {
		for (uint8_t knob = 0; knob < kNumPhysicalModKnobs; ++knob) {
			controls.push_back({ModKnobControl{button, knob}, knobTargetOf(sound.modKnobs[button][knob].paramDescriptor,
			                                                               params::Kind::UNPATCHED_SOUND)});
		}
	}
	appendLearned(sound, params::Kind::UNPATCHED_SOUND, controls);
	return controls;
}

std::vector<ControllerMapping> learnedControlsOf(::ModControllableAudio& owner) {
	std::vector<ControllerMapping> controls;
	appendLearned(owner, params::Kind::UNPATCHED_GLOBAL, controls);
	return controls;
}

void refreshControls(::Song& song) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->docOutput_ == nullptr) {
			continue;
		}
		switch (output->type) {
		case OutputType::SYNTH:
			output->docOutput_->controls = controlsOf(*static_cast<SoundInstrument*>(output));
			break;
		case OutputType::KIT: {
			Kit* kit = static_cast<Kit*>(output);
			output->docOutput_->controls = learnedControlsOf(*kit);
			for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->type != DrumType::SOUND || !drum->id()) {
					continue;
				}
				if (deluge::project::DocDrum* record = song.project.drum_structure.find(*drum->id())) {
					record->controls = controlsOf(*static_cast<SoundDrum*>(drum));
				}
			}
			break;
		}
		case OutputType::AUDIO:
			output->docOutput_->controls = learnedControlsOf(*static_cast<AudioOutput*>(output));
			break;
		default:
			break;
		}
	}
	song.project.controls = learnedControlsOf(song.live.global_effectable);
}

deluge::project::CodecEngines codecEngines() {
	static const std::array<deluge::engine::ParamNaming, 2> namings{deluge::instrument::kNaming,
	                                                                deluge::global_effectable::kNaming};
	return {.namings = namings};
}

} // namespace deluge::bridge
