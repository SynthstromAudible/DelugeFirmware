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

#include "deluge-project/arrangement.h"
#include "io/midi/mapping_index.h"
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

#include <algorithm>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>

namespace deluge::bridge {

namespace params = deluge::modulation::params;
using deluge::project::ControlsAddress;
using deluge::project::Mapping;
using deluge::project::MappingTable;
using deluge::project::MidiCommand;

namespace {

/// Record @p learned as @p target's binding, when it holds something and the table holds nothing for @p target.
void recordLearned(::Song& song, const LearnedMIDI& learned, deluge::project::MappingTarget target) {
	if (learned.containsSomething() && song.project.mappings.find(target) == nullptr) {
		recordLoadedMapping(song, Mapping{.source = command_of(learned), .target = target});
	}
}

/// Record @p knobs' learned knobs as @p owner's, when the table holds none for @p owner.
void recordKnobs(ControlsAddress owner, const ::ModControllableAudio& knobs, bool isSound, ::Song& song) {
	if (song.project.mappings.params_of(owner).empty()) {
		song.project.mappings.set_params(owner, knobsOf(owner, knobs, isSound));
	}
}

/// Record @p drum's bindings: a sound row's knobs, its mute and its input.
void recordDrumBindings(Drum& drum, ::Song& song) {
	if (!drum.id()) {
		return;
	}
	if (drum.type == DrumType::SOUND) {
		recordKnobs({ControlsAddress::Kind::DRUM, *drum.id()}, *static_cast<SoundDrum*>(&drum), true, song);
	}
	recordLearned(song, drum.muteMIDICommand, deluge::project::DrumMuteTarget{*drum.id()});
	recordLearned(song, drum.midiInput, deluge::project::LaneInputTarget{*drum.id()});
}

/// Record @p output's bindings: its own knobs, its input, and a kit's rows' knobs, mutes and inputs.
void recordOutputBindings(Output& output, ::Song& song) {
	if (!output.id()) {
		return;
	}
	const ControlsAddress address{ControlsAddress::Kind::OUTPUT, *output.id()};
	switch (output.type) {
	case OutputType::SYNTH:
		recordKnobs(address, *static_cast<SoundInstrument*>(&output), true, song);
		break;
	case OutputType::KIT:
		recordKnobs(address, *static_cast<Kit*>(&output), false, song);
		break;
	case OutputType::AUDIO:
		recordKnobs(address, *static_cast<AudioOutput*>(&output), false, song);
		return; // an audio track has no MIDI input
	default:
		break;
	}
	recordLearned(song, static_cast<Instrument&>(output).midiInput, deluge::project::TrackInputTarget{*output.id()});
	if (output.type != OutputType::KIT) {
		return;
	}
	for (Drum* drum = static_cast<Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
		recordDrumBindings(*drum, song);
	}
}

/// Whether @p a and @p b play the same command from the same cable.
bool sameLearned(const LearnedMIDI& a, const LearnedMIDI& b) {
	return a.cable == b.cable && a.channelOrZone == b.channelOrZone && a.noteOrCC == b.noteOrCC;
}

/// The kit row @p target names, when it is a row's mute or input.
std::optional<uint32_t> rowOf(const deluge::project::MappingTarget& target) {
	if (const auto* mute = std::get_if<deluge::project::DrumMuteTarget>(&target)) {
		return mute->drum_id;
	}
	if (const auto* input = std::get_if<deluge::project::LaneInputTarget>(&target)) {
		return input->drum_id;
	}
	return std::nullopt;
}

/// Make @p target's entry (a row's mute or input) the command @p field holds after the runtime moved it there.
///
/// Its source is the one @p before held for @p target itself, else for the same kind of command of one of @p fromRows,
/// when that plays what @p field holds: so a device the runtime could not name stays named, and no other kind of
/// binding lends its own. Failing both, what @p field names.
void recordMovedCommand(MappingTable& mappings, const MappingTable& before,
                        const deluge::project::MappingTarget& target, const LearnedMIDI& field,
                        std::span<const uint32_t> fromRows) {
	if (!field.containsSomething()) {
		mappings.unlearn(target);
		return;
	}
	const Mapping* played = nullptr;
	for (const Mapping& entry : before.entries()) {
		if (entry.target.index() != target.index() || !sameLearned(learned_of(entry.source), field)) {
			continue;
		}
		if (entry.target == target) {
			played = &entry;
			break;
		}
		const std::optional<uint32_t> row = rowOf(entry.target);
		if (played == nullptr && row && std::ranges::find(fromRows, *row) != fromRows.end()) {
			played = &entry;
		}
	}
	mappings.learn(Mapping{.source = played != nullptr ? played->source : command_of(field), .target = target});
}

/// The output of @p song, playing or hibernating, whose id is @p id, or nullptr.
Output* outputWithId(::Song& song, uint32_t id) {
	for (Output* first :
	     {song.live.media.first_output, static_cast<Output*>(song.live.media.first_hibernating_instrument)}) {
		for (Output* output = first; output != nullptr; output = output->next) {
			if (output->id() == id) {
				return output;
			}
		}
	}
	return nullptr;
}

/// The drum of any kit of @p song, playing or hibernating, whose id is @p id, or nullptr.
Drum* drumWithId(::Song& song, uint32_t id) {
	for (Output* first :
	     {song.live.media.first_output, static_cast<Output*>(song.live.media.first_hibernating_instrument)}) {
		for (Output* output = first; output != nullptr; output = output->next) {
			if (output->type != OutputType::KIT) {
				continue;
			}
			for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->id() == id) {
					return drum;
				}
			}
		}
	}
	return nullptr;
}

/// The clip of @p song, in the session or the arrangement, whose id is @p id, or nullptr.
Clip* clipWithId(::Song& song, uint32_t id) {
	for (Clip* clip : AllClips::everywhere(&song)) {
		if (clip->id() == id) {
			return clip;
		}
	}
	return nullptr;
}

/// Whether a projection may make a device entry (see cable_for), which allocates and edits the device list: never on
/// the audio tier.
enum class Devices : bool { FIND, MAKE };

/// @p command as an engine field: see field_of, or learned_of when @p devices is FIND.
LearnedMIDI projected(const MidiCommand& command, Devices devices) {
	return devices == Devices::MAKE ? field_of(command) : learned_of(command);
}

/// Make @p field the command the table holds for @p target, or empty when it holds none.
void projectCommand(const MappingTable& mappings, const deluge::project::MappingTarget& target, LearnedMIDI& field,
                    Devices devices) {
	if (const Mapping* entry = mappings.find(target)) {
		field = projected(entry->source, devices);
	}
	else {
		field.clear();
	}
}

/// Make @p owner's learned knob list its knobs in @p mappings, in table order. A knob the list leaves as it is stays
/// whole: its device (which a name lookup could not tell from another of the same name) and its takeover state with
/// it.
void projectKnobs(const MappingTable& mappings, ControlsAddress address, ::ModControllableAudio& owner, bool isSound,
                  Devices devices) {
	const params::Kind unpatchedKind = isSound ? params::Kind::UNPATCHED_SOUND : params::Kind::UNPATCHED_GLOBAL;
	deluge::fast_vector<MIDIKnob> knobs;
	for (const Mapping& entry : mappings.params_of(address)) {
		const ParamDescriptor target =
		    descriptorOf(std::get<deluge::project::ParamTarget>(entry.target).param, unpatchedKind);
		const auto kept = std::ranges::find_if(owner.midi_knobs, [&](const MIDIKnob& old) {
			return command_of(old.midiInput) == entry.source && old.relative == entry.relative
			       && old.paramDescriptor == target;
		});
		if (kept != owner.midi_knobs.end()) {
			knobs.push_back(*kept);
			continue;
		}
		MIDIKnob made;
		made.midiInput = projected(entry.source, devices);
		made.relative = entry.relative;
		made.paramDescriptor = target;
		knobs.push_back(made);
	}
	owner.midi_knobs = std::move(knobs);
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

void recordArrivingOutput(::Output& output, ::Song& song) {
	recordOutputBindings(output, song);
	deluge::markMappingIndexStale();
}

void recordArrivingDrum(::Drum& drum, ::Song& song) {
	recordDrumBindings(drum, song);
	deluge::markMappingIndexStale();
}

void cloneOutputMappings(const ::Output& from, ::Output& to, ::Song& song) {
	if (!from.id() || !to.id()) {
		return;
	}
	song.project.mappings.copy_params(*from.id(), *to.id());
	// An overdub's clone runs on the song's tick, which may be the audio tier: a device the Deluge does not know gets
	// its entry, and the index its rebuild, on the app loop.
	(void)projectMappingFindingDevices(song,
	                                   deluge::project::ParamTarget{{ControlsAddress::Kind::OUTPUT, *to.id()}, {}});
	deluge::markMappingIndexStale();
}

void handOverInstrument(::Song& song, uint32_t from, uint32_t to, bool knobs) {
	deluge::markMappingIndexStale();
	MappingTable& mappings = song.project.mappings;
	if (knobs) {
		mappings.retarget_output(from, to);
		return;
	}
	const Mapping* input = mappings.find(deluge::project::TrackInputTarget{from});
	const std::optional<MidiCommand> source = input != nullptr ? std::optional{input->source} : std::nullopt;
	mappings.unlearn(deluge::project::TrackInputTarget{to});
	mappings.unlearn(deluge::project::TrackInputTarget{from});
	if (source) {
		mappings.learn(Mapping{.source = *source, .target = deluge::project::TrackInputTarget{to}});
	}
}

void projectOutputMappings(::Song& song, uint32_t output) {
	// An owner with no knob list (a MIDI or CV track) or no input (an audio track) refuses its half: nothing to play.
	(void)projectMapping(song, deluge::project::ParamTarget{{ControlsAddress::Kind::OUTPUT, output}, {}});
	(void)projectMapping(song, deluge::project::TrackInputTarget{output});
	deluge::midi::rebuildMappingIndex(song);
}

void projectDeviceMappings(::Song& song) {
	std::vector<deluge::project::MappingTarget> targets;
	for (const Mapping& entry : song.project.mappings.entries()) {
		if (!entry.source.device || entry.source.device->port != deluge::project::MidiDeviceRef::Port::HOSTED) {
			continue;
		}
		deluge::project::MappingTarget target = entry.target;
		if (auto* knob = std::get_if<deluge::project::ParamTarget>(&target)) {
			knob->param = {}; // an owner's knob list is projected whole
		}
		if (std::ranges::find(targets, target) == targets.end()) {
			targets.push_back(target);
		}
	}
	for (const deluge::project::MappingTarget& target : targets) {
		(void)projectMapping(song, target);
	}
}

std::vector<uint32_t> rowIdsOf(const ::Output* output) {
	std::vector<uint32_t> rows;
	if (output == nullptr || output->type != OutputType::KIT) {
		return rows;
	}
	for (const Drum* drum = static_cast<const Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->id()) {
			rows.push_back(*drum->id());
		}
	}
	return rows;
}

void recordMovedDrumCommands(::Kit& kit, const MappingTable& before, std::span<const uint32_t> fromRows, ::Song& song) {
	for (Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
		if (!drum->id()) {
			continue;
		}
		recordMovedCommand(song.project.mappings, before, deluge::project::DrumMuteTarget{*drum->id()},
		                   drum->muteMIDICommand, fromRows);
		recordMovedCommand(song.project.mappings, before, deluge::project::LaneInputTarget{*drum->id()},
		                   drum->midiInput, fromRows);
	}
	deluge::midi::rebuildMappingIndex(song);
}

void knobNoLongerRelative(::ModControllableAudio& owner, const ::MIDIKnob& knob, ::Song& song) {
	const std::optional<ControlsAddress> address = controlsAddressOf(owner, song);
	if (!address) {
		return;
	}
	bool isSound = false;
	if (controlsOwnerAt(song, *address, &isSound) != &owner) {
		return;
	}
	// The entry is the one the knob was projected from: same param, and its source plays as the knob does (a device's
	// saved ids may not be the cable's own).
	const deluge::project::MappingTarget target = deluge::project::ParamTarget{
	    *address,
	    knobTargetOf(knob.paramDescriptor, isSound ? params::Kind::UNPATCHED_SOUND : params::Kind::UNPATCHED_GLOBAL)};
	for (Mapping& entry : song.project.mappings.entries()) {
		if (entry.target == target && sameLearned(learned_of(entry.source), knob.midiInput)) {
			entry.relative = false;
		}
	}
	// Run from the MIDI input path, maybe while it walks the index: the rebuild waits for the next message.
	deluge::markMappingIndexStale();
}

void recordLoadedMappings(::Song& song) {
	MappingTable read;
	for (const Mapping& entry : song.project.mappings.entries()) {
		if (std::holds_alternative<deluge::project::SectionLaunchTarget>(entry.target)) {
			read.learn(entry);
		}
	}
	song.project.mappings = std::move(read);
	recordKnobs({ControlsAddress::Kind::SONG, 0}, song.live.global_effectable, false, song);
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

namespace {

Error projectMappingWith(::Song& song, const deluge::project::MappingTarget& target, Devices devices) {
	const MappingTable& mappings = song.project.mappings;
	return std::visit(
	    [&]<typename T>(const T& t) -> Error {
		    if constexpr (std::is_same_v<T, deluge::project::ParamTarget>) {
			    bool isSound = false;
			    ::ModControllableAudio* owner = controlsOwnerAt(song, t.owner, &isSound);
			    if (owner == nullptr) {
				    return Error::UNSPECIFIED;
			    }
			    projectKnobs(mappings, t.owner, *owner, isSound, devices);
		    }
		    else if constexpr (std::is_same_v<T, deluge::project::ClipMuteTarget>) {
			    Clip* clip = clipWithId(song, t.clip_id);
			    if (clip == nullptr) {
				    return Error::UNSPECIFIED;
			    }
			    projectCommand(mappings, target, clip->muteMIDICommand, devices);
		    }
		    else if constexpr (std::is_same_v<T, deluge::project::DrumMuteTarget>
		                       || std::is_same_v<T, deluge::project::LaneInputTarget>) {
			    Drum* drum = drumWithId(song, t.drum_id);
			    if (drum == nullptr) {
				    return Error::UNSPECIFIED;
			    }
			    projectCommand(mappings, target,
			                   std::is_same_v<T, deluge::project::DrumMuteTarget> ? drum->muteMIDICommand
			                                                                      : drum->midiInput,
			                   devices);
		    }
		    else if constexpr (std::is_same_v<T, deluge::project::TrackInputTarget>) {
			    Output* output = outputWithId(song, t.output_id);
			    // An audio track has no MIDI input.
			    if (output == nullptr || output->type == OutputType::AUDIO) {
				    return Error::UNSPECIFIED;
			    }
			    projectCommand(mappings, target, static_cast<Instrument*>(output)->midiInput, devices);
		    }
		    else {
			    static_assert(std::is_same_v<T, deluge::project::SectionLaunchTarget>);
			    if (t.section >= kMaxNumSections) {
				    return Error::UNSPECIFIED;
			    }
		    }
		    return Error::NONE;
	    },
	    target);
}

} // namespace

Error projectMapping(::Song& song, const deluge::project::MappingTarget& target) {
	return projectMappingWith(song, target, Devices::MAKE);
}

Error projectMappingFindingDevices(::Song& song, const deluge::project::MappingTarget& target) {
	const Error error = projectMappingWith(song, target, Devices::FIND);
	for (const Mapping& entry : song.project.mappings.entries()) {
		if (entry.target.index() == target.index() && entry.source.device
		    && find_cable(*entry.source.device) == nullptr) {
			deluge::markMappingDevicesChanged(); // the refresh makes its entry and projects it again
			break;
		}
	}
	return error;
}

bool holdsOutput(::Song& song, const ::Output& output) {
	return output.id() && outputWithId(song, *output.id()) == &output;
}

bool holdsDrum(::Song& song, const ::Drum& drum) {
	return drum.id() && drumWithId(song, *drum.id()) == &drum;
}

std::optional<std::vector<Mapping>> savedKnobsOf(::Song& song, const ::ModControllableAudio& owner,
                                                 std::optional<ControlsAddress> address) {
	bool isSound = false;
	if (!address || controlsOwnerAt(song, *address, &isSound) != &owner) {
		return std::nullopt;
	}
	return song.project.mappings.params_of(*address);
}

} // namespace deluge::bridge
