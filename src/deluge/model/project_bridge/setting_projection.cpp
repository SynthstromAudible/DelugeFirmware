#include "model/project_bridge/setting_projection.h"

#include "engine-api/types/sync.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "serialization/tree.h"

namespace deluge::bridge {

namespace {

using project::SettingGroupId;
using serialization::Value;

/// @brief The song's output with durable id @p id, playing or hibernating.
::Output* findOutput(::Song& song, uint32_t id) {
	for (::Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->id() == id) {
			return output;
		}
	}
	for (::Output* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = sleeping->next) {
		if (sleeping->id() == id) {
			return sleeping;
		}
	}
	return nullptr;
}

/// @brief Drum @p drum_id of kit @p kit_id.
::Drum* findDrum(::Song& song, uint32_t kit_id, uint32_t drum_id) {
	::Output* output = findOutput(song, kit_id);
	if (output == nullptr || output->type != OutputType::KIT) {
		return nullptr;
	}
	for (::Drum* drum = static_cast<::Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->id() == drum_id) {
			return drum;
		}
	}
	return nullptr;
}

/// @brief The song's clip with durable id @p id and type @p type.
::Clip* findClip(::Song& song, uint32_t id, ClipType type) {
	for (::Clip* clip : AllClips::everywhere(&song)) {
		if (clip->id() == id && clip->type == type) {
			return clip;
		}
	}
	return nullptr;
}

/// @brief @p clip's row @p address names: a melodic row by its y, a kit row by its sound drum's id.
::NoteRow* findNoteRow(::InstrumentClip& clip, const project::NoteRowAddress& address) {
	for (int32_t i = 0; ::NoteRow* row = clip.noteRows.tryGet(i); i++) {
		const bool matches = address.is_kit
		                         ? (row->drum != nullptr && row->drum->id() == static_cast<uint32_t>(address.row_key))
		                         : row->y() == address.row_key;
		if (matches) {
			return row;
		}
	}
	return nullptr;
}

std::optional<int32_t> integer_of(const Value& value) {
	return serialization::int_of(&value);
}

} // namespace

project::SettingGroupId trackGroupOf(std::string_view path) {
	return path == "name" ? SettingGroupId::TRACK : SettingGroupId::INSTRUMENT_DEVICE;
}

// ---------------------------------------------------------------------------------------------
// Applying

Error applySongSetting(::Song& song, std::string_view path, const Value& value) {
	const std::optional<int32_t> v = integer_of(value);
	if (!v) {
		return Error::UNSPECIFIED;
	}
	if (path == "swingInterval") {
		song.changeSwingInterval(*v);
		return Error::NONE;
	}
	return Error::UNSPECIFIED;
}

Error applyTrackSetting(::Output& /*output*/, std::string_view /*path*/, const Value& /*value*/) {
	return Error::UNSPECIFIED;
}

Error applyLaneSetting(::Drum& /*drum*/, std::string_view /*path*/, const Value& /*value*/) {
	return Error::UNSPECIFIED;
}

Error applyNoteRowSetting(::NoteRow& /*row*/, ::InstrumentClip& /*clip*/, ::Song& /*song*/, std::string_view /*path*/,
                          const Value& /*value*/) {
	return Error::UNSPECIFIED;
}

Error applyAudioClipSetting(::AudioClip& /*clip*/, ::Song& /*song*/, std::string_view /*path*/,
                            const Value& /*value*/) {
	return Error::UNSPECIFIED;
}

// ---------------------------------------------------------------------------------------------
// Projecting and reading back

Error projectSetting(::Song& song, const project::SettingAddress& address, std::string_view path) {
	const std::optional<Value> value = project::read_setting(song.project, address, path);
	if (!value) {
		return Error::UNSPECIFIED;
	}
	switch (address.group) {
	case SettingGroupId::SONG:
		return applySongSetting(song, path, *value);
	case SettingGroupId::TRACK:
	case SettingGroupId::INSTRUMENT_DEVICE: {
		::Output* output = findOutput(song, std::get<project::TrackAddress>(address.owner).output_id);
		return output != nullptr ? applyTrackSetting(*output, path, *value) : Error::UNSPECIFIED;
	}
	case SettingGroupId::LANE: {
		const auto& lane = std::get<project::LaneAddress>(address.owner);
		::Drum* drum = findDrum(song, lane.kit_id, lane.drum_id);
		return drum != nullptr ? applyLaneSetting(*drum, path, *value) : Error::UNSPECIFIED;
	}
	case SettingGroupId::NOTE_ROW: {
		const auto& row_address = std::get<project::NoteRowAddress>(address.owner);
		auto* clip = static_cast<::InstrumentClip*>(findClip(song, row_address.clip_id, ClipType::INSTRUMENT));
		::NoteRow* row = clip != nullptr ? findNoteRow(*clip, row_address) : nullptr;
		return row != nullptr ? applyNoteRowSetting(*row, *clip, song, path, *value) : Error::UNSPECIFIED;
	}
	case SettingGroupId::AUDIO_CLIP: {
		auto* clip = static_cast<::AudioClip*>(
		    findClip(song, std::get<project::ClipAddress>(address.owner).clip_id, ClipType::AUDIO));
		return clip != nullptr ? applyAudioClipSetting(*clip, song, path, *value) : Error::UNSPECIFIED;
	}
	}
	return Error::UNSPECIFIED;
}

std::optional<Value> engineSettingValue(::Song& song, const project::SettingAddress& address, std::string_view path) {
	if (address.group == SettingGroupId::SONG) {
		if (path == "swingInterval") {
			return Value::makeNumber(deluge::engine::index_of(song.project.groove.swing_interval));
		}
	}
	return std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// Addresses

std::optional<project::TrackAddress> trackAddressOf(::Output& output, ::Song& song) {
	const std::optional<uint32_t> id = output.id();
	if (!id || findOutput(song, *id) != &output || song.project.output_structure.find(*id) == nullptr) {
		return std::nullopt;
	}
	return project::TrackAddress{.output_id = *id};
}

std::optional<project::LaneAddress> laneAddressOf(::Drum& drum, ::Song& song) {
	const std::optional<uint32_t> drum_id = drum.id();
	if (!drum_id || song.project.drum_structure.find(*drum_id) == nullptr) {
		return std::nullopt;
	}
	for (::Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->type == OutputType::KIT && output->id() && findDrum(song, *output->id(), *drum_id) == &drum) {
			return project::LaneAddress{.kit_id = *output->id(), .drum_id = *drum_id};
		}
	}
	return std::nullopt;
}

std::optional<project::NoteRowAddress> noteRowAddressOf(::NoteRow& row, ::InstrumentClip& clip, ::Song& song) {
	if (findClip(song, clip.id(), ClipType::INSTRUMENT) != &clip || clip.output == nullptr) {
		return std::nullopt;
	}
	project::NoteRowAddress address{.clip_id = clip.id()};
	if (clip.output->type == OutputType::KIT) {
		if (row.drum == nullptr || row.drum->type != DrumType::SOUND || !row.drum->id()) {
			return std::nullopt;
		}
		address.is_kit = true;
		address.row_key = static_cast<int32_t>(*row.drum->id());
	}
	else {
		address.row_key = row.y();
	}
	if (findNoteRow(clip, address) != &row) {
		return std::nullopt;
	}
	return address;
}

std::optional<project::ClipAddress> audioClipAddressOf(::AudioClip& clip, ::Song& song) {
	if (findClip(song, clip.id(), ClipType::AUDIO) != &clip
	    || song.project.audio_clip_structure.find(clip.id()) == nullptr) {
		return std::nullopt;
	}
	return project::ClipAddress{.clip_id = clip.id()};
}

} // namespace deluge::bridge
