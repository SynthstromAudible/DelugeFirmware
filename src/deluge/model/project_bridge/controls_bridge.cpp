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

#include "engines/deluge/cables.h"
#include "engines/deluge/naming.h"
#include "engines/global_effectable/naming.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/chain_bridge.h"
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

std::vector<ControllerMapping> controlsOf(::Sound& sound) {
	std::vector<ControllerMapping> controls;
	for (uint8_t button = 0; button < kNumModButtons; ++button) {
		for (uint8_t knob = 0; knob < kNumPhysicalModKnobs; ++knob) {
			controls.push_back({ModKnobControl{button, knob}, knobTargetOf(sound.modKnobs[button][knob].paramDescriptor,
			                                                               params::Kind::UNPATCHED_SOUND)});
		}
	}
	return controls;
}

void refreshControls(::Song& song) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->docOutput_ == nullptr) {
			continue;
		}
		switch (output->type) {
		case OutputType::SYNTH: {
			auto& sound = *static_cast<SoundInstrument*>(output);
			output->docOutput_->controls = controlsOf(sound);
			break;
		}
		case OutputType::KIT: {
			Kit* kit = static_cast<Kit*>(output);
			output->docOutput_->controls.clear();
			for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->type != DrumType::SOUND || !drum->id()) {
					continue;
				}
				auto& sound = *static_cast<SoundDrum*>(drum);
				if (deluge::project::DocDrum* record = song.project.drum_structure.find(*drum->id())) {
					record->controls = controlsOf(sound);
				}
			}
			break;
		}
		case OutputType::AUDIO:
			output->docOutput_->controls.clear();
			break;
		default:
			break;
		}
	}
	song.project.controls.clear();
}

ParamDescriptor descriptorOf(deluge::engine::ParamId target, params::Kind unpatchedKind) {
	ParamDescriptor descriptor;
	if ((target.value & deluge::instrument::kCableDepthFlag) != 0) { // a cable's depth
		descriptor.data = cableWordOf(target);
	}
	else if (const std::optional<int32_t> patched = runtimeIndexOf(params::Kind::PATCHED, target)) {
		descriptor.setToHaveParamOnly(*patched);
	}
	else if (const std::optional<int32_t> unpatched = runtimeIndexOf(unpatchedKind, target)) {
		descriptor.setToHaveParamOnly(params::UNPATCHED_START + *unpatched);
	}
	else {
		descriptor.setToNull();
	}
	return descriptor;
}

std::optional<deluge::project::ControlsAddress> controlsAddressOf(::Sound& sound) {
	using Kind = deluge::project::ControlsAddress::Kind;
	if (sound.isDrum()) {
		auto& drum = static_cast<SoundDrum&>(sound);
		return drum.docDrum_ != nullptr && drum.id()
		           ? std::optional{deluge::project::ControlsAddress{Kind::DRUM, *drum.id()}}
		           : std::nullopt;
	}
	auto& synth = static_cast<SoundInstrument&>(sound);
	return synth.docOutput_ != nullptr && synth.id()
	           ? std::optional{deluge::project::ControlsAddress{Kind::OUTPUT, *synth.id()}}
	           : std::nullopt;
}

namespace {

/// The owner of @p output's own controls and whether it is a sound: a synth's sound, a kit's or an audio track's.
::ModControllableAudio* ownControlsOf(Output& output, bool* isSound) {
	*isSound = output.type == OutputType::SYNTH;
	switch (output.type) {
	case OutputType::SYNTH:
		return static_cast<SoundInstrument*>(&output);
	case OutputType::KIT:
		return static_cast<Kit*>(&output);
	case OutputType::AUDIO:
		return static_cast<AudioOutput*>(&output);
	default:
		return nullptr;
	}
}

/// Call @p visit with every output and hibernating instrument of @p song until it answers true.
template <typename Visit>
void forEachOutput(::Song& song, Visit visit) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (visit(*output)) {
			return;
		}
	}
	for (Output* output = song.live.media.first_hibernating_instrument; output != nullptr; output = output->next) {
		if (visit(*output)) {
			return;
		}
	}
}

} // namespace

std::optional<deluge::project::ControlsAddress> controlsAddressOf(::ModControllable& owner, ::Song& song) {
	using Kind = deluge::project::ControlsAddress::Kind;
	if (&owner == static_cast<::ModControllable*>(&song.live.global_effectable)) {
		return deluge::project::ControlsAddress{Kind::SONG, 0};
	}
	std::optional<deluge::project::ControlsAddress> found;
	forEachOutput(song, [&](Output& output) {
		bool isSound = false;
		::ModControllableAudio* own = ownControlsOf(output, &isSound);
		if (own != nullptr && static_cast<::ModControllable*>(own) == &owner && output.docOutput_ != nullptr
		    && output.id()) {
			found = deluge::project::ControlsAddress{Kind::OUTPUT, *output.id()};
			return true;
		}
		if (output.type == OutputType::KIT) {
			for (Drum* drum = static_cast<Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->type == DrumType::SOUND
				    && static_cast<::ModControllable*>(static_cast<SoundDrum*>(drum)) == &owner) {
					found = controlsAddressOf(*static_cast<SoundDrum*>(drum));
					return true;
				}
			}
		}
		return false;
	});
	return found;
}

::ModControllableAudio* controlsOwnerAt(::Song& song, deluge::project::ControlsAddress address, bool* isSound) {
	using Kind = deluge::project::ControlsAddress::Kind;
	*isSound = false;
	if (address.kind == Kind::SONG) {
		return &song.live.global_effectable;
	}
	::ModControllableAudio* owner = nullptr;
	forEachOutput(song, [&](Output& output) {
		if (address.kind == Kind::OUTPUT) {
			if (output.id() == address.id) {
				owner = ownControlsOf(output, isSound);
				return true;
			}
			return false;
		}
		if (output.type == OutputType::KIT) {
			for (Drum* drum = static_cast<Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->type == DrumType::SOUND && drum->id() == address.id) {
					owner = static_cast<SoundDrum*>(drum);
					*isSound = true;
					return true;
				}
			}
		}
		return false;
	});
	return owner;
}

void applyControls(::ModControllableAudio& owner, bool isSound, const std::vector<ControllerMapping>& controls) {
	if (!isSound) {
		return;
	}
	for (const ControllerMapping& mapping : controls) {
		const ModKnobControl& knob = mapping.control;
		if (knob.button < kNumModButtons && knob.knob < kNumPhysicalModKnobs) {
			static_cast<::Sound&>(owner).modKnobs[knob.button][knob.knob].paramDescriptor =
			    descriptorOf(mapping.target, params::Kind::UNPATCHED_SOUND);
		}
	}
}

void recordOutputControls(::Output& output, ::Song& song) {
	if (output.docOutput_ == nullptr || !output.id()) {
		return;
	}
	bool isSound = false;
	::ModControllableAudio* own = ownControlsOf(output, &isSound);
	std::vector<ControllerMapping>* record =
	    song.project.resolveControls({deluge::project::ControlsAddress::Kind::OUTPUT, *output.id()});
	if (own == nullptr || record == nullptr) {
		return;
	}
	*record = isSound ? controlsOf(static_cast<::Sound&>(*own)) : std::vector<ControllerMapping>{};
}

deluge::project::CodecEngines codecEngines() {
	static const std::array<deluge::engine::ParamNaming, 2> namings{deluge::instrument::kNaming,
	                                                                deluge::global_effectable::kNaming};
	return {.namings = namings, .chains = chainCodec()};
}

} // namespace deluge::bridge
