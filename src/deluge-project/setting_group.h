#pragma once

/// @file
/// @brief Setting groups: a song setting's record, named by an address and a path, with the table a menu reads.
///
/// A group is one family of settings that live together on one owner's record: the song's own settings, a track's
/// identity, its instrument device's fields, a lane's identity, a note row's, an audio clip's descriptor. Each has a
/// table of `engine::SettingInfo` rows (the same form an engine's settings take), and the record read and write here.
/// Values are in their file form: the text `check_setting` checks. Writing checks the value against the row and
/// changes nothing when the owner, the path or the value is refused; the engine side follows through the bridge's
/// projection (`model/project_bridge/setting_projection.h`).

#include "deluge-project/clip_address.h"
#include "deluge-project/kit_row_address.h"
#include "deluge-project/note_address.h"
#include "engine-api/types/setting.h"
#include "serialization/value.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace deluge::project {

class Song;

/// @brief Which family of settings an address names.
enum class SettingGroupId : uint8_t {
	SONG,              ///< the song's own: reverb and its model, swing interval, disabled scales
	TRACK,             ///< a track's identity: its name
	INSTRUMENT_DEVICE, ///< a track's MIDI or CV instrument: channel, device definition, Y-axis, collapse, CV2
	LANE,              ///< a kit lane's identity: its name
	NOTE_ROW,          ///< a note row's own: its direction
	AUDIO_CLIP,        ///< an audio clip's descriptor: markers, tuning, playback
	CLIP,              ///< a clip's identity and playback: its name and direction
	CLIP_VALUES,       ///< the values a clip holds over its track's: MIDI program, bend ranges
	AUDIO_INPUT,       ///< an audio track's input: its mode, channel and the track it records from
	CLIP_ARP,          ///< a clip's arp, in its value layer: a synth's, a MIDI or CV clip's, or the whole kit's
	LANE_ARP,          ///< a kit row's arp, its lane's note device
};

/// @brief The song itself, as an owner.
struct SongAddress {
	auto operator<=>(const SongAddress&) const = default;
};

/// @brief A track, by its output's durable id.
struct TrackAddress {
	uint32_t output_id{0};
	auto operator<=>(const TrackAddress&) const = default;
};

/// @brief A kit lane, by its kit's and drum's durable ids.
using LaneAddress = KitRowAddress;

/// @brief Whose record a setting is on.
using SettingOwner = std::variant<SongAddress, TrackAddress, LaneAddress, ClipAddress, NoteRowAddress>;

/// @brief One group on one owner.
struct SettingAddress {
	SettingGroupId group{SettingGroupId::SONG};
	SettingOwner owner{SongAddress{}};
	bool operator==(const SettingAddress&) const = default;
};

/// @brief The rows of @p group.
/// @param group The group.
/// @return Its table; every path a read or write of the group takes.
[[nodiscard]] std::span<const engine::SettingInfo> table_of(SettingGroupId group);

/// @brief @p address's value at @p path, in its file form.
/// @param song    The song whose records hold it.
/// @param address The group and owner.
/// @param path    A path of the group's table.
/// @return The value, or nullopt when the owner does not resolve or the group has no such path.
[[nodiscard]] std::optional<serialization::Value> read_setting(Song& song, const SettingAddress& address,
                                                               std::string_view path);

/// @brief Whether @p address holds its own value at @p path. Every setting does but a clip value (and a kit row's bend
///        range) a clip leaves to its track, and an arp's host value where it is a param.
/// @param song    The song whose records hold it.
/// @param address The group and owner.
/// @param path    A path of the group's table.
/// @return False for an unheld clip value, or an owner that does not resolve; true otherwise.
[[nodiscard]] bool holds_setting(Song& song, const SettingAddress& address, std::string_view path);

/// @brief Forget @p address's own value at @p path, so it plays its track's again. Only a clip value (and a kit row's
///        bend range, and an arp's host value) can be forgotten; any other setting is left as it is.
/// @param song    The song whose records hold it.
/// @param address The group and owner.
/// @param path    A path of the group's table.
void forget_setting(Song& song, const SettingAddress& address, std::string_view path);

/// @brief Set @p address's value at @p path in its record.
/// @param song    The song whose records hold it.
/// @param address The group and owner.
/// @param path    A path of the group's table.
/// @param value   The new value, in its file form.
/// @return True when written; false, with nothing changed, when the owner does not resolve, the group has no such
///         path, or the row refuses the value.
[[nodiscard]] bool write_setting(Song& song, const SettingAddress& address, std::string_view path,
                                 const serialization::Value& value);

} // namespace deluge::project
