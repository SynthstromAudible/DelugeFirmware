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

#include "model/project_bridge/mapping_bridge.h"

#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

namespace deluge::bridge {

namespace params = deluge::modulation::params;
using deluge::project::ControlsAddress;
using deluge::project::Mapping;
using deluge::project::MappingTable;
using deluge::project::MidiCommand;

namespace {

/// Record @p learned as @p target's binding, when it holds something.
void recordLearned(::Song& song, const LearnedMIDI& learned, deluge::project::MappingTarget target) {
	if (learned.containsSomething()) {
		recordLoadedMapping(song, Mapping{.source = command_of(learned), .target = target});
	}
}

/// Record @p output's bindings: its own knobs, its input, and a kit's rows' knobs, mutes and inputs.
void recordOutputBindings(Output& output, ::Song& song) {
	if (!output.id()) {
		return;
	}
	const ControlsAddress address{ControlsAddress::Kind::OUTPUT, *output.id()};
	switch (output.type) {
	case OutputType::SYNTH:
		recordOwnerKnobs(address, *static_cast<SoundInstrument*>(&output), true, song);
		break;
	case OutputType::KIT:
		recordOwnerKnobs(address, *static_cast<Kit*>(&output), false, song);
		break;
	case OutputType::AUDIO:
		recordOwnerKnobs(address, *static_cast<AudioOutput*>(&output), false, song);
		return; // an audio track has no MIDI input
	default:
		break;
	}
	recordLearned(song, static_cast<Instrument&>(output).midiInput, deluge::project::TrackInputTarget{*output.id()});
	if (output.type != OutputType::KIT) {
		return;
	}
	for (Drum* drum = static_cast<Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
		if (!drum->id()) {
			continue;
		}
		if (drum->type == DrumType::SOUND) {
			recordOwnerKnobs({ControlsAddress::Kind::DRUM, *drum->id()}, *static_cast<SoundDrum*>(drum), true, song);
		}
		recordLearned(song, drum->muteMIDICommand, deluge::project::DrumMuteTarget{*drum->id()});
		recordLearned(song, drum->midiInput, deluge::project::LaneInputTarget{*drum->id()});
	}
}

} // namespace

void recordLoadedMapping(::Song& song, const Mapping& mapping) {
	song.project.mappings.learn(mapping);
}

std::vector<Mapping> knobsOf(ControlsAddress owner, const ::ModControllableAudio& knobs, bool isSound) {
	const params::Kind unpatchedKind = isSound ? params::Kind::UNPATCHED_SOUND : params::Kind::UNPATCHED_GLOBAL;
	std::vector<Mapping> entries;
	for (const MIDIKnob& knob : knobs.midi_knobs) {
		entries.push_back(
		    Mapping{.source = command_of(knob.midiInput),
		            .target = deluge::project::ParamTarget{owner, knobTargetOf(knob.paramDescriptor, unpatchedKind)},
		            .relative = knob.relative});
	}
	return entries;
}

void recordOwnerKnobs(ControlsAddress owner, const ::ModControllableAudio& knobs, bool isSound, ::Song& song) {
	song.project.mappings.set_params(owner, knobsOf(owner, knobs, isSound));
}

void recordSoundKnobs(::Sound& sound, ::Song& song) {
	if (const std::optional<ControlsAddress> owner = controlsAddressOf(sound)) {
		recordOwnerKnobs(*owner, sound, true, song);
	}
}

void recordLoadedMappings(::Song& song) {
	MappingTable read;
	for (const Mapping& entry : song.project.mappings.entries()) {
		if (std::holds_alternative<deluge::project::SectionLaunchTarget>(entry.target)) {
			read.learn(entry);
		}
	}
	song.project.mappings = std::move(read);
	recordOwnerKnobs({ControlsAddress::Kind::SONG, 0}, song.live.global_effectable, false, song);
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		recordOutputBindings(*output, song);
	}
	for (Clip* clip : AllClips::everywhere(&song)) {
		recordLearned(song, clip->muteMIDICommand, deluge::project::ClipMuteTarget{clip->id()});
	}
}

MidiCommand sectionCommand(const MappingTable& mappings, uint8_t section) {
	const Mapping* entry = mappings.find(deluge::project::SectionLaunchTarget{section});
	return entry != nullptr ? entry->source : MidiCommand{};
}

void setSectionCommand(MappingTable& mappings, uint8_t section, const MidiCommand& command) {
	if (command.contains_something()) {
		mappings.learn(Mapping{.source = command, .target = deluge::project::SectionLaunchTarget{section}});
	}
	else {
		mappings.unlearn(deluge::project::SectionLaunchTarget{section});
	}
}

} // namespace deluge::bridge
