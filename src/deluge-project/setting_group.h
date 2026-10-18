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

#include "deluge-project/arp_record.h"
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
///
/// Each says which engine::SettingContext a host passes when it asks whether a row's menu shows: the Owner (the
/// menu's scope) and the Instrument (what plays the notes, the track's or a kit lane's drum).
enum class SettingGroupId : uint8_t {
	SONG,              ///< the song's own: reverb and its model, swing interval, disabled scales. SONG, NONE
	TRACK,             ///< a track's identity: its name. TRACK, the track's
	INSTRUMENT_DEVICE, ///< a track's MIDI or CV instrument: channel, device definition, Y-axis, collapse, CV2. TRACK,
	                   ///< MIDI or CV
	LANE,              ///< a kit lane's identity: its name. KIT_LANE, the lane's drum
	NOTE_ROW,          ///< a note row's own: its direction. KIT_LANE and the lane's drum for a kit row, else CLIP and
	                   ///< the track's
	AUDIO_CLIP,        ///< an audio clip's descriptor: markers, tuning, playback. CLIP, AUDIO
	CLIP,              ///< a clip's identity and playback: its name and direction. CLIP, the track's
	CLIP_VALUES,       ///< the values a clip holds over its track's: MIDI program, bend ranges. CLIP, the track's
	AUDIO_INPUT,       ///< an audio track's input: its mode, channel and the track it records from. TRACK, AUDIO
	CLIP_ARP,          ///< a clip's arp, in its value layer: a synth's, a MIDI or CV clip's, or the whole kit's. CLIP,
	                   ///< the track's (KIT for the whole kit's)
	LANE_ARP,          ///< a kit row's arp, its lane's note device. KIT_LANE, the lane's drum
};

/// @brief How many setting groups there are. Code that numbers owners after the groups (the host's name table)
/// derives its indices from it.
inline constexpr uint8_t kSettingGroupCount = 11;

/// @brief Whether kSettingGroupCount counts @p group. The switch names every group, so a new one fails to compile
/// (-Werror=switch) until it has a case here, beside the count it must move.
[[nodiscard]] constexpr bool counted(SettingGroupId group) {
	switch (group) {
	case SettingGroupId::SONG:
	case SettingGroupId::TRACK:
	case SettingGroupId::INSTRUMENT_DEVICE:
	case SettingGroupId::LANE:
	case SettingGroupId::NOTE_ROW:
	case SettingGroupId::AUDIO_CLIP:
	case SettingGroupId::CLIP:
	case SettingGroupId::CLIP_VALUES:
	case SettingGroupId::AUDIO_INPUT:
	case SettingGroupId::CLIP_ARP:
	case SettingGroupId::LANE_ARP:
		return static_cast<uint8_t>(group) < kSettingGroupCount;
	}
	return false;
}
static_assert(counted(SettingGroupId::LANE_ARP)
                  && static_cast<SettingGroupId>(kSettingGroupCount - 1) == SettingGroupId::LANE_ARP,
              "kSettingGroupCount counts every SettingGroupId, and LANE_ARP is the last");

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

/// @brief @p record's value at arp path @p path, in its file form: what the CLIP_ARP and LANE_ARP groups read.
/// @param record The arp's record.
/// @param path   A path of the arp groups' table.
/// @return The value; a host value the record does not hold reads as its row's default. Nullopt for any other path.
[[nodiscard]] std::optional<serialization::Value> arp_setting_value(const ArpRecord& record, std::string_view path);

/// @brief A record as it was before a write that may reshape it beyond the written path: an arp's, whose preset writes
///        three fields and whose first write creates it. Empty for every other group, whose writes touch their path
///        alone.
struct SettingRecordSnapshot {
	std::optional<std::optional<ArpRecord>> arp; ///< the arp record slot, when the address names one
};

/// @brief @p address's record as it is now, for restore_setting_record.
/// @param song    The song whose records hold it.
/// @param address The group and owner.
/// @return The snapshot; empty for a group whose writes touch their path alone, or an owner that does not resolve.
[[nodiscard]] SettingRecordSnapshot snapshot_setting_record(Song& song, const SettingAddress& address);

/// @brief Put @p address's record back as @p snapshot took it.
/// @param song     The song whose records hold it.
/// @param address  The group and owner the snapshot was taken at.
/// @param snapshot What snapshot_setting_record returned.
/// @return True when the snapshot held a record and it was put back; false for an empty snapshot.
bool restore_setting_record(Song& song, const SettingAddress& address, const SettingRecordSnapshot& snapshot);

/// @brief Whether @p group's value at @p path is a command its record derives rather than holds: an arp's preset, which
///        writes the modes it stands for and reads back as the preset those modes make (so "custom" reads as "up").
/// @param group The group.
/// @param path  A path of the group's table.
/// @return True for a derived command row.
[[nodiscard]] bool is_derived_setting(SettingGroupId group, std::string_view path);

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
