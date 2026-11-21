#include "scenarios/slot_coherence.h"

#include "io/midi/mapping_index.h"
#include "io/midi/mapping_index_stale.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "libdeluge/system.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/project_bridge/setting_projection.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace deluge::harness {

namespace {

using deluge::project::SettingAddress;
using deluge::project::SettingGroupId;

const char* groupName(SettingGroupId group) {
	switch (group) {
	case SettingGroupId::SONG:
		return "SONG";
	case SettingGroupId::TRACK:
		return "TRACK";
	case SettingGroupId::INSTRUMENT_DEVICE:
		return "INSTRUMENT_DEVICE";
	case SettingGroupId::LANE:
		return "LANE";
	case SettingGroupId::NOTE_ROW:
		return "NOTE_ROW";
	case SettingGroupId::AUDIO_CLIP:
		return "AUDIO_CLIP";
	case SettingGroupId::CLIP:
		return "CLIP";
	case SettingGroupId::CLIP_VALUES:
		return "CLIP_VALUES";
	case SettingGroupId::AUDIO_INPUT:
		return "AUDIO_INPUT";
	case SettingGroupId::CLIP_ARP:
		return "CLIP_ARP";
	case SettingGroupId::LANE_ARP:
		return "LANE_ARP";
	case SettingGroupId::SOUND_ROUTING:
		return "SOUND_ROUTING";
	}
	return "?";
}

/// The paths of @p group's table, less those @p output's kind has no engine copy of: a MIDI instrument has no CV2
/// source, a CV instrument only its channel and CV2 source, and other outputs no instrument device at all. An arp's
/// host values are paths only where they are settings (@p arpHost): elsewhere they are params.
std::vector<std::string_view> pathsFor(SettingGroupId group, const ::Output* output, bool arpHost = false) {
	std::vector<std::string_view> paths;
	for (const deluge::plugin::SettingInfo& row : deluge::project::table_of(group)) {
		if ((group == SettingGroupId::CLIP_ARP || group == SettingGroupId::LANE_ARP) && !arpHost
		    && deluge::plugin::find_setting(deluge::plugin::kArpHostRows, row.path) != nullptr) {
			continue;
		}
		if (group == SettingGroupId::INSTRUMENT_DEVICE) {
			const bool kept = output->type == OutputType::MIDI_OUT ? row.path != "cv2Source"
			                  : output->type == OutputType::CV     ? row.path == "channel" || row.path == "cv2Source"
			                                                       : false;
			if (!kept) {
				continue;
			}
		}
		paths.push_back(row.path);
	}
	return paths;
}

} // namespace

void forEachSettingAddress(
    ::Song& song,
    const std::function<void(const SettingAddress&, uint32_t, std::span<const std::string_view>)>& visit) {
	auto visitGroup = [&](const SettingAddress& address, uint32_t owner, const ::Output* output, bool arpHost = false) {
		const std::vector<std::string_view> paths = pathsFor(address.group, output, arpHost);
		if (!paths.empty()) {
			visit(address, owner, paths);
		}
	};
	visitGroup({SettingGroupId::SONG, deluge::project::SongAddress{}}, 0, nullptr);
	auto visitOutput = [&](::Output& output) {
		const auto track = deluge::bridge::trackAddressOf(output, song);
		if (!track) {
			return;
		}
		visitGroup({SettingGroupId::TRACK, *track}, track->output_id, &output);
		visitGroup({SettingGroupId::INSTRUMENT_DEVICE, *track}, track->output_id, &output);
		if (output.type == OutputType::AUDIO) {
			visitGroup({SettingGroupId::AUDIO_INPUT, *track}, track->output_id, nullptr);
		}
		if (output.type == OutputType::SYNTH) {
			visitGroup({SettingGroupId::SOUND_ROUTING, *track}, track->output_id, nullptr);
		}
		if (output.type == OutputType::KIT) {
			for (::Drum* drum = static_cast<::Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
				if (const auto lane = deluge::bridge::laneAddressOf(*drum, song)) {
					visitGroup({SettingGroupId::LANE, *lane}, lane->drum_id, nullptr);
					if (drum->type == DrumType::SOUND) {
						visitGroup({SettingGroupId::SOUND_ROUTING, *lane}, lane->drum_id, nullptr);
					}
					visitGroup({SettingGroupId::LANE_ARP, *lane}, lane->drum_id, nullptr,
					           deluge::bridge::arpHostValuesAreSettings({.drum = drum}));
				}
			}
		}
	};
	for (::Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		visitOutput(*output);
	}
	for (::Output* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = sleeping->next) {
		visitOutput(*sleeping);
	}
	for (::Clip* clip : AllClips::everywhere(&song)) {
		if (const auto address = deluge::bridge::clipAddressOf(*clip, song)) {
			visitGroup({SettingGroupId::CLIP, *address}, address->clip_id, nullptr);
			if (clip->type == ClipType::INSTRUMENT) {
				auto* instrumentClip = static_cast<::InstrumentClip*>(clip);
				visitGroup({SettingGroupId::CLIP_VALUES, *address}, address->clip_id, nullptr);
				visitGroup({SettingGroupId::CLIP_ARP, *address}, address->clip_id, nullptr,
				           deluge::bridge::arpHostValuesAreSettings({.clip = instrumentClip}));
			}
		}
		if (clip->type == ClipType::AUDIO) {
			if (const auto address = deluge::bridge::audioClipAddressOf(*static_cast<::AudioClip*>(clip), song)) {
				visitGroup({SettingGroupId::AUDIO_CLIP, *address}, address->clip_id, nullptr);
			}
			continue;
		}
		auto& instrumentClip = *static_cast<::InstrumentClip*>(clip);
		for (int32_t i = 0; ::NoteRow* row = instrumentClip.noteRows.tryGet(i); i++) {
			if (const auto address = deluge::bridge::noteRowAddressOf(*row, instrumentClip, song)) {
				visitGroup({SettingGroupId::NOTE_ROW, *address}, address->clip_id, nullptr);
			}
		}
	}
}

/// @brief Whether @p address's bend range at @p path is one to compare: the record holds the clip's or row's own, or
///        the engine has an expression set holding one. With neither, the clip plays its device's default, which the
///        record cannot name.
bool comparesBendRange(::Song& song, const SettingAddress& address, std::string_view path) {
	const bool bend = (address.group == SettingGroupId::CLIP_VALUES && path.starts_with("bendRange"))
	                  || (address.group == SettingGroupId::NOTE_ROW && path == "bendRange");
	if (!bend || deluge::project::holds_setting(song.project, address, path)) {
		return true;
	}
	if (address.group == SettingGroupId::CLIP_VALUES) {
		for (::Clip* clip : AllClips::everywhere(&song)) {
			if (clip->id() == std::get<deluge::project::ClipAddress>(address.owner).clip_id
			    && clip->type == ClipType::INSTRUMENT) {
				return clip->paramManager.getExpressionParamSet() != nullptr;
			}
		}
		return false;
	}
	const auto& row = std::get<deluge::project::NoteRowAddress>(address.owner);
	for (::Clip* clip : AllClips::everywhere(&song)) {
		if (clip->id() != row.clip_id || clip->type != ClipType::INSTRUMENT) {
			continue;
		}
		auto& instrumentClip = *static_cast<::InstrumentClip*>(clip);
		for (int32_t i = 0; ::NoteRow* noteRow = instrumentClip.noteRows.tryGet(i); i++) {
			const bool matches =
			    row.is_kit ? noteRow->drum != nullptr && noteRow->drum->id() == static_cast<uint32_t>(row.row_key)
			               : noteRow->y() == row.row_key;
			if (matches) {
				return noteRow->paramManager.getExpressionParamSet() != nullptr;
			}
		}
	}
	return false;
}

std::string settingIncoherence(::Song& song, const SettingAddress& address, uint32_t owner,
                               std::span<const std::string_view> paths) {
	for (std::string_view path : paths) {
		if (!comparesBendRange(song, address, path)) {
			continue;
		}
		const auto record = deluge::project::read_setting(song.project, address, path);
		const auto engine = deluge::bridge::engineSettingValue(song, address, path);
		const std::string a = record ? record->str() : "<none>";
		const std::string b = engine ? engine->str() : "<none>";
		if (!record || !engine || a != b) {
			return std::string{groupName(address.group)} + " " + std::to_string(owner) + " " + std::string{path}
			       + ": record=" + a + " engine=" + b;
		}
	}
	return {};
}

namespace {

using deluge::project::ControlsAddress;
using deluge::project::Mapping;
using deluge::project::MappingTarget;

/// Whether @p field plays @p source: the same channel or zone and note or CC, from no cable when @p source is from any
/// device, and from the cable the device list holds for the device it names otherwise. A device the Deluge did not know
/// gets an entry when its binding is projected, so a named device is never played from no cable, which would match
/// every device.
bool playsTheSame(const deluge::project::MidiCommand& source, const ::LearnedMIDI& field) {
	if (field.channelOrZone != source.channel_or_zone || field.noteOrCC != source.note_or_cc) {
		return false;
	}
	if (!source.device) {
		return field.cable == nullptr;
	}
	return field.cable != nullptr && field.cable == deluge::bridge::find_cable(*source.device);
}

/// @p learned as a report shows it.
std::string describe(const ::LearnedMIDI& learned) {
	if (!learned.containsSomething()) {
		return "<none>";
	}
	return std::string{learned.cable != nullptr ? learned.cable->getDisplayName() : "any"} + "/"
	       + std::to_string(learned.channelOrZone) + "/" + std::to_string(learned.noteOrCC);
}

/// A table entry's source as a report shows it: a device the Deluge has no cable for by its name.
std::string describe(const deluge::project::MidiCommand& source) {
	const ::LearnedMIDI learned = deluge::bridge::learned_of(source);
	if (source.device && learned.cable == nullptr && learned.containsSomething()) {
		return "unknown(" + source.device->name + ")/" + std::to_string(learned.channelOrZone) + "/"
		       + std::to_string(learned.noteOrCC);
	}
	return describe(learned);
}

/// One table knob as a report shows it.
std::string describe(deluge::plugin::ParamId param, const deluge::project::MidiCommand& source, bool relative) {
	return std::to_string(param.value) + "=" + describe(source) + (relative ? "~" : "");
}

/// The first break of @p address's learned knobs: its runtime owner holds none outside the song's table, whose entries
/// are its knobs alone.
std::string knobsIncoherence(::Song& song, ControlsAddress address) {
	const std::optional<deluge::model::EffectsOwner> owner = deluge::bridge::effectsOwnerAt(song, address);
	if (!owner || owner->knobs->loaded.empty()) {
		return {};
	}
	const auto knobs = [](std::span<const Mapping> entries) {
		std::string listed;
		for (const Mapping& entry : entries) {
			listed += (listed.empty() ? "" : " ")
			          + describe(std::get<deluge::project::OwnerParamTarget>(entry.target).param.id, entry.source,
			                     entry.relative);
		}
		return listed;
	};
	const char* kind = address.kind == ControlsAddress::Kind::SONG   ? "SONG_KNOBS"
	                   : address.kind == ControlsAddress::Kind::DRUM ? "DRUM_KNOBS"
	                                                                 : "OUTPUT_KNOBS";
	return std::string{"MAPPINGS "} + kind + " " + std::to_string(address.id) + ": table=["
	       + knobs(song.project.mappings.params_of(address)) + "] outside=[" + knobs(owner->knobs->loaded) + "]";
}

/// The first break between @p target's table entry and @p field, the engine field that plays it.
std::string commandIncoherence(::Song& song, const MappingTarget& target, const char* kind, uint32_t owner,
                               const ::LearnedMIDI& field) {
	const Mapping* entry = song.project.mappings.find(target);
	const ::LearnedMIDI table = entry != nullptr ? deluge::bridge::learned_of(entry->source) : ::LearnedMIDI{};
	const bool same = table.containsSomething() == field.containsSomething()
	                  && (!table.containsSomething() || playsTheSame(entry->source, field));
	if (same) {
		return {};
	}
	return std::string{"MAPPINGS "} + kind + " " + std::to_string(owner)
	       + ": table=" + (entry != nullptr ? describe(entry->source) : describe(table)) + " engine=" + describe(field);
}

/// The first break of the song's section launch commands: one past the last section, or two for one section.
std::string sectionIncoherence(::Song& song) {
	std::array<bool, kMaxNumSections> seen{};
	for (const Mapping& entry : song.project.mappings.entries()) {
		const auto* section = std::get_if<deluge::project::SectionLaunchTarget>(&entry.target);
		if (section == nullptr) {
			continue;
		}
		if (section->section >= kMaxNumSections) {
			return "MAPPINGS SECTION_LAUNCH " + std::to_string(section->section) + ": there is no such section";
		}
		if (seen[section->section]) {
			return "MAPPINGS SECTION_LAUNCH " + std::to_string(section->section) + ": the table holds it twice";
		}
		seen[section->section] = true;
	}
	return {};
}

/// The first break of @p output's bindings: its knobs, its input, and a kit's rows' knobs, mutes and inputs.
std::string outputMappingIncoherence(::Song& song, ::Output& output) {
	if (!output.id()) {
		return {};
	}
	const uint32_t id = *output.id();
	if (std::string what = knobsIncoherence(song, {ControlsAddress::Kind::OUTPUT, id}); !what.empty()) {
		return what;
	}
	if (output.type == OutputType::AUDIO) {
		// An audio track has no MIDI input to hold an entry for.
		if (const Mapping* stray = song.project.mappings.find(deluge::project::TrackInputTarget{id})) {
			return "MAPPINGS TRACK_INPUT " + std::to_string(id) + ": the table holds " + describe(stray->source)
			       + " for an audio track";
		}
		return {};
	}
	if (std::string what = commandIncoherence(song, deluge::project::TrackInputTarget{id}, "TRACK_INPUT", id,
	                                          static_cast<::Instrument&>(output).midiInput);
	    !what.empty()) {
		return what;
	}
	if (output.type != OutputType::KIT) {
		return {};
	}
	for (::Drum* drum = static_cast<::Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
		if (!drum->id()) {
			continue;
		}
		const uint32_t drumId = *drum->id();
		// Only a sound row has knobs to turn.
		if (drum->type != DrumType::SOUND
		    && !song.project.mappings.params_of({ControlsAddress::Kind::DRUM, drumId}).empty()) {
			return "MAPPINGS DRUM_KNOBS " + std::to_string(drumId) + ": the table holds knobs for a row with no sound";
		}
		for (std::string what : {knobsIncoherence(song, {ControlsAddress::Kind::DRUM, drumId}),
		                         commandIncoherence(song, deluge::project::DrumMuteTarget{drumId}, "DRUM_MUTE", drumId,
		                                            drum->muteMIDICommand),
		                         commandIncoherence(song, deluge::project::LaneInputTarget{drumId}, "LANE_INPUT",
		                                            drumId, drum->midiInput)}) {
			if (!what.empty()) {
				return what;
			}
		}
	}
	return {};
}

} // namespace

std::string mappingIncoherence(::Song& song) {
	if (std::string what = sectionIncoherence(song); !what.empty()) {
		return what;
	}
	if (std::string what = knobsIncoherence(song, {ControlsAddress::Kind::SONG, 0}); !what.empty()) {
		return what;
	}
	for (::Output* first :
	     {song.live.media.first_output, static_cast<::Output*>(song.live.media.first_hibernating_instrument)}) {
		for (::Output* output = first; output != nullptr; output = output->next) {
			if (std::string what = outputMappingIncoherence(song, *output); !what.empty()) {
				return what;
			}
		}
	}
	for (::Clip* clip : AllClips::everywhere(&song)) {
		if (std::string what = commandIncoherence(song, deluge::project::ClipMuteTarget{clip->id()}, "CLIP_MUTE",
		                                          clip->id(), clip->muteMIDICommand);
		    !what.empty()) {
			return what;
		}
	}
	return knobIndexIncoherence(song);
}

std::string knobIndexIncoherence(::Song& song) {
	if (&song != currentSong) {
		return {};
	}
	// The owners whose knobs the index plays: the song, each output in the main list and those kits' rows.
	std::vector<ControlsAddress> awake{{ControlsAddress::Kind::SONG, 0}};
	for (::Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (!output->id()) {
			continue;
		}
		awake.push_back({ControlsAddress::Kind::OUTPUT, *output->id()});
		if (output->type == OutputType::KIT) {
			for (::Drum* drum = static_cast<::Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->id()) {
					awake.push_back({ControlsAddress::Kind::DRUM, *drum->id()});
				}
			}
		}
	}
	// What the index plays for each table knob's owner and source, from any device, as text; and whether that holds
	// the knob itself.
	const auto played = [&](const deluge::midi::MappingIndex& index, const Mapping& entry, bool* holdsIt) {
		const auto& knob = std::get<deluge::project::OwnerParamTarget>(entry.target);
		std::string hits;
		index.forEachKnob(knob.owner, MIDIDeviceManager::dinMIDIPorts, entry.source.channel_or_zone,
		                  entry.source.note_or_cc, [&](const deluge::midi::IndexedEntry& hit) {
			                  const auto* target = std::get_if<deluge::project::OwnerParamTarget>(&hit.target);
			                  hits += " "
			                          + (target != nullptr ? std::to_string(target->param.id.value) : std::string{"?"})
			                          + (hit.relative ? "~" : "");
			                  if (holdsIt != nullptr && hit.target == entry.target && hit.relative == entry.relative) {
				                  *holdsIt = true;
			                  }
		                  });
		return hits;
	};
	const auto isKnob = [](const Mapping& entry) {
		return std::holds_alternative<deluge::project::OwnerParamTarget>(entry.target)
		       && entry.source.contains_something();
	};
	const auto where = [](const Mapping& entry) {
		const auto& knob = std::get<deluge::project::OwnerParamTarget>(entry.target);
		const char* kind = knob.owner.kind == ControlsAddress::Kind::SONG   ? "SONG"
		                   : knob.owner.kind == ControlsAddress::Kind::DRUM ? "DRUM"
		                                                                    : "OUTPUT";
		return std::string{kind} + " " + std::to_string(knob.owner.id) + " knob "
		       + describe(knob.param.id, entry.source, entry.relative);
	};
	const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
	MIDIDeviceManager::differentiatingInputsByDevice = false; // a knob from any device is heard
	// What a rebuild would make, built here: the check leaves the live index, its stale mark, the held writes and the
	// device flag as they are, so it cannot repair a refresh the app missed.
	const bool stale = deluge::mappingIndexStale();
	const bool held = deluge::bridge::mappingOpsHeld();
	deluge::midi::MappingSnapshot snapshot;
	{
		AudioExcludedSection section;
		snapshot.copyOwners(song);
	}
	snapshot.copyEntries(song.project.mappings);
	deluge::midi::MappingIndex rebuilt;
	rebuilt.rebuild(std::move(snapshot));
	// An index not marked stale is the one the next message matches: it must play what a rebuild does. Writes the
	// render still holds are not in the table the copy reads, so a rebuild would differ from it by them.
	const bool compareLive = !stale && !held;
	std::string found;
	for (const Mapping& entry : song.project.mappings.entries()) {
		if (!found.empty() || !isKnob(entry)) {
			continue;
		}
		bool holdsIt = false;
		const std::string after = played(rebuilt, entry, &holdsIt);
		const auto& owner = std::get<deluge::project::OwnerParamTarget>(entry.target).owner;
		if (!holdsIt && std::ranges::find(awake, owner) != awake.end()) {
			found = "MAPPINGS KNOB_INDEX " + where(entry) + ": the rebuilt index plays [" + after + " ]";
		}
		else if (const std::string live = played(deluge::midi::mappingIndex(), entry, nullptr);
		         compareLive && live != after) {
			found = "MAPPINGS KNOB_INDEX " + where(entry) + ": stale, unmarked: index=[" + live + " ] rebuilt=[" + after
			        + " ]";
		}
	}
	if (stale != deluge::mappingIndexStale() || held != deluge::bridge::mappingOpsHeld()) {
		found = "MAPPINGS KNOB_INDEX the check changed the index's stale mark or the held writes";
	}
	MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
	return found;
}

std::string songSettingsIncoherence(::Song& song) {
	std::string found;
	forEachSettingAddress(song,
	                      [&](const SettingAddress& address, uint32_t owner, std::span<const std::string_view> paths) {
		                      if (found.empty()) {
			                      found = settingIncoherence(song, address, owner, paths);
		                      }
	                      });
	return found.empty() ? mappingIncoherence(song) : found;
}

} // namespace deluge::harness
