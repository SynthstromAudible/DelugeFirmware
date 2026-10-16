#include "scenarios/slot_coherence.h"

#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/project_bridge/setting_projection.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"

#include <string>
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
	}
	return "?";
}

/// The paths of @p group's table, less those @p output's kind has no engine copy of: a MIDI instrument has no CV2
/// source, a CV instrument only its channel and CV2 source, and other outputs no instrument device at all.
std::vector<std::string_view> pathsFor(SettingGroupId group, const ::Output* output) {
	std::vector<std::string_view> paths;
	for (const deluge::engine::SettingInfo& row : deluge::project::table_of(group)) {
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
	auto visitGroup = [&](const SettingAddress& address, uint32_t owner, const ::Output* output) {
		const std::vector<std::string_view> paths = pathsFor(address.group, output);
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
		if (output.type == OutputType::KIT) {
			for (::Drum* drum = static_cast<::Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
				if (const auto lane = deluge::bridge::laneAddressOf(*drum, song)) {
					visitGroup({SettingGroupId::LANE, *lane}, lane->drum_id, nullptr);
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

std::string songSettingsIncoherence(::Song& song) {
	std::string found;
	forEachSettingAddress(song,
	                      [&](const SettingAddress& address, uint32_t owner, std::span<const std::string_view> paths) {
		                      for (std::string_view path : paths) {
			                      if (!found.empty()) {
				                      return;
			                      }
			                      const auto record = deluge::project::read_setting(song.project, address, path);
			                      const auto engine = deluge::bridge::engineSettingValue(song, address, path);
			                      const std::string a = record ? record->str() : "<none>";
			                      const std::string b = engine ? engine->str() : "<none>";
			                      if (!record || !engine || a != b) {
				                      found = std::string{groupName(address.group)} + " " + std::to_string(owner) + " "
				                              + std::string{path} + ": record=" + a + " engine=" + b;
			                      }
		                      }
	                      });
	return found;
}

} // namespace deluge::harness
