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

#include "effects/global_effectable/naming.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/naming.h"
#include "instruments/deluge/plugin.h"
#include "io/debug/log.h"
#include "model/drum/drum.h"
#include "model/edits/set_controls.h"
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

deluge::plugin::ParamId controlTargetOf(const deluge::modulation::KnobTarget& knob, params::Kind unpatchedKind) {
	const deluge::synth::CableTarget word = deluge::modulation::knobWord(knob);
	if (!word.is_param()) {
		return cableDepthIdOf(word.word);
	}
	const int32_t p = word.param_index();
	if (p >= params::UNPATCHED_START) {
		return paramIdOf(unpatchedKind, p - params::UNPATCHED_START);
	}
	return paramIdOf(params::Kind::PATCHED, p);
}

std::vector<ControllerMapping> controlsOf(::Sound& sound) {
	std::vector<ControllerMapping> controls;
	for (uint8_t button = 0; button < kNumModButtons; ++button) {
		for (uint8_t knob = 0; knob < kNumPhysicalModKnobs; ++knob) {
			controls.push_back({ModKnobControl{button, knob},
			                    controlTargetOf(sound.modKnobs[button][knob].target, params::Kind::UNPATCHED_SOUND)});
		}
	}
	return controls;
}

static_assert(kNumModButtons == 8, "a knob pair per mod button");

deluge::instrument::KnobPairs knobPairsOf(const std::vector<ControllerMapping>& controls) {
	deluge::instrument::KnobPairs pairs{};
	for (const ControllerMapping& mapping : controls) {
		const ModKnobControl& knob = mapping.control;
		if (knob.button < pairs.size() && knob.knob < kNumPhysicalModKnobs) {
			(knob.knob == 0 ? pairs[knob.button].lower : pairs[knob.button].upper) = mapping.target;
		}
	}
	return pairs;
}

std::vector<ControllerMapping> controlsOf(const deluge::instrument::KnobPairs& pairs) {
	std::vector<ControllerMapping> controls;
	controls.reserve(pairs.size() * kNumPhysicalModKnobs);
	for (uint8_t button = 0; button < pairs.size(); ++button) {
		controls.push_back({ModKnobControl{button, 0}, pairs[button].lower});
		controls.push_back({ModKnobControl{button, 1}, pairs[button].upper});
	}
	return controls;
}

bool writeKnobPairs(::Sound& sound, ::Song& song,
                    deluge::function_ref<deluge::instrument::KnobPairs(const deluge::instrument::KnobPairs&)> change) {
	const std::optional<deluge::project::ControlsAddress> address = controlsAddressOf(sound);
	std::vector<ControllerMapping>* record = address ? song.project.resolveControls(*address) : nullptr;
	// A record not holding all the mod knobs would read the missing ones as param id 0: the engine's knobs stand in.
	const bool whole = record != nullptr && record->size() == kNumModButtons * kNumPhysicalModKnobs;
	const deluge::instrument::KnobPairs knobs = knobPairsOf(whole ? *record : controlsOf(sound));
	const deluge::instrument::KnobPairs changed = change(knobs);
	if (changed == knobs) {
		return false;
	}
	std::vector<ControllerMapping> controls = controlsOf(changed);
	if (record == nullptr) {
		applyControls(sound, controls);
		return true;
	}
	deluge::edits::Context ctx{song, song.project};
	deluge::edits::SetControls edit{*address, controls};
	if (deluge::edits::apply(ctx, edit) != Error::NONE) {
		// The record resolved but its owner did not: write both copies, so they stay equal.
		D_PRINTLN("controls refused: knobs written directly");
		applyControls(sound, controls);
		*record = std::move(controls);
	}
	return true;
}

void remapKnobsForMode(::Sound& sound, ::Song& song, deluge::instrument::SynthMode from,
                       deluge::instrument::SynthMode to) {
	writeKnobPairs(sound, song, [from, to](const deluge::instrument::KnobPairs& knobs) {
		return deluge::instrument::remap_for_mode(from, to, knobs);
	});
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
}

deluge::modulation::KnobTarget knobOfControl(deluge::plugin::ParamId target, params::Kind unpatchedKind) {
	if ((target.value & deluge::instrument::kCableDepthFlag) != 0) { // a cable's depth
		return deluge::modulation::knobTargetOf({cableWordOf(target)});
	}
	if (const std::optional<int32_t> patched = runtimeIndexOf(params::Kind::PATCHED, target)) {
		return deluge::modulation::knobParam(static_cast<uint32_t>(*patched));
	}
	if (const std::optional<int32_t> unpatched = runtimeIndexOf(unpatchedKind, target)) {
		return deluge::modulation::knobParam(static_cast<uint32_t>(params::UNPATCHED_START + *unpatched));
	}
	return std::nullopt;
}

deluge::modulation::KnobTarget knobOfControl(deluge::project::ParamTarget target, params::Kind unpatchedKind) {
	if (target.space != deluge::project::ParamSpace::ENGINE) {
		return std::nullopt;
	}
	return knobOfControl(target.id, unpatchedKind);
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

/// The owner of @p output's own controls: a synth's sound, a kit or an audio track; null for any other output.
::ModControllable* ownControlsOf(Output& output) {
	switch (output.type) {
	case OutputType::SYNTH:
	case OutputType::KIT:
	case OutputType::AUDIO:
		return output.toModControllable();
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
		::ModControllable* own = ownControlsOf(output);
		if (own != nullptr && own == &owner && output.docOutput_ != nullptr && output.id()) {
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

Output* outputWithId(::Song& song, uint32_t id) {
	Output* found = nullptr;
	forEachOutput(song, [&](Output& output) {
		if (output.id() == id) {
			found = &output;
		}
		return found != nullptr;
	});
	return found;
}

Drum* drumWithId(::Song& song, uint32_t id) {
	Drum* found = nullptr;
	forEachOutput(song, [&](Output& output) {
		if (output.type == OutputType::KIT) {
			found = static_cast<Kit&>(output).drumWithId(id);
		}
		return found != nullptr;
	});
	return found;
}

::Sound* soundAt(::Song& song, deluge::project::ControlsAddress address) {
	using Kind = deluge::project::ControlsAddress::Kind;
	switch (address.kind) {
	case Kind::SONG:
		return nullptr;
	case Kind::OUTPUT: {
		Output* output = outputWithId(song, address.id);
		return output != nullptr && output->type == OutputType::SYNTH ? static_cast<SoundInstrument*>(output) : nullptr;
	}
	case Kind::DRUM:
		break;
	}
	Drum* drum = drumWithId(song, address.id);
	return drum != nullptr && drum->type == DrumType::SOUND ? static_cast<SoundDrum*>(drum) : nullptr;
}

std::optional<deluge::model::EffectsOwner> effectsOwnerAt(::Song& song, deluge::project::ControlsAddress address) {
	using Kind = deluge::project::ControlsAddress::Kind;
	if (address.kind == Kind::SONG) {
		return song.live.global_effectable.effectsOwner();
	}
	if (address.kind == Kind::OUTPUT) {
		Output* output = outputWithId(song, address.id);
		::ModControllable* own = output != nullptr ? ownControlsOf(*output) : nullptr;
		return own != nullptr ? own->effectsOwner() : std::nullopt;
	}
	::Sound* sound = soundAt(song, address);
	return sound != nullptr ? sound->effectsOwner() : std::nullopt;
}

::ModControllable* controlsSlotAt(::Song& song, deluge::project::ControlsAddress address) {
	using Kind = deluge::project::ControlsAddress::Kind;
	if (address.kind == Kind::SONG) {
		return &song.live.global_effectable;
	}
	if (address.kind == Kind::OUTPUT) {
		Output* output = outputWithId(song, address.id);
		return output != nullptr ? ownControlsOf(*output) : nullptr;
	}
	return soundAt(song, address);
}

void applyControls(::Sound& sound, const std::vector<ControllerMapping>& controls) {
	for (const ControllerMapping& mapping : controls) {
		const ModKnobControl& knob = mapping.control;
		if (knob.button < kNumModButtons && knob.knob < kNumPhysicalModKnobs) {
			sound.modKnobs[knob.button][knob.knob].target =
			    knobOfControl(mapping.target, params::Kind::UNPATCHED_SOUND);
		}
	}
}

void recordOutputControls(::Output& output, ::Song& song) {
	if (output.docOutput_ == nullptr || !output.id()) {
		return;
	}
	std::vector<ControllerMapping>* record =
	    song.project.resolveControls({deluge::project::ControlsAddress::Kind::OUTPUT, *output.id()});
	if (ownControlsOf(output) == nullptr || record == nullptr) {
		return;
	}
	*record = output.type == OutputType::SYNTH ? controlsOf(*static_cast<SoundInstrument*>(&output))
	                                           : std::vector<ControllerMapping>{};
}

namespace {

/// A sound element's instrument, read onto a new synth's state as a load reads it.
deluge::plugin::StateHandle decodeInstrument(const deluge::serialization::Value& element, int32_t tick_magnitude) {
	deluge::instrument::SoundState state = deluge::instrument::new_synth_state();
	(void)deluge::instrument::decode(element, state, {.input_tick_magnitude = tick_magnitude});
	return deluge::plugin::StateHandle::of<deluge::instrument::DelugeInstrument>(std::move(state));
}

void encodeInstrument(const deluge::plugin::StateHandle& state, deluge::serialization::Value& element,
                      int32_t tick_magnitude) {
	for (const auto& [key, value] : state.encode({.input_tick_magnitude = tick_magnitude}).entries()) {
		element.set(key, value);
	}
}

} // namespace

deluge::project::CodecPlugins codecPlugins() {
	static const std::array<deluge::plugin::ParamNaming, 2> namings{deluge::instrument::kNaming,
	                                                                deluge::global_effectable::kNaming};
	return {.namings = namings,
	        .chains = chainCodec(),
	        .instruments = {.decode = &decodeInstrument, .encode = &encodeInstrument}};
}

} // namespace deluge::bridge
