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

#pragma once

#include "project/instrument_slot.h"
#include "project/setting_group.h"
#include "serialization/value.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class Song;
class Sound;

namespace deluge::harness {

/// @brief Which equations a coherence check holds a sound to.
struct CoherenceChecks {
	bool runtime = true;       ///< what the runtime still holds == the record: types, patches, ranges, effects
	bool bound = true;         ///< the sound plays its slot's state: the record's, not a copy of it
	bool snapshot = true;      ///< the config == the one deriveConfig() makes of the record
	bool controls = true;      ///< its controls record == its mod knobs; no learned knob outside the table
	bool decode = true;        ///< the record encoded at the slot's magnitude, then decoded == the record
	bool chains = true;        ///< each host chain (a kit's, an audio track's, the master's) == its owner's encoding
	bool song_settings = true; ///< every setting group's record == its engine copy (songSettingsIncoherence)
	/// no sound holds a param of an engine it is not playing (parkedIncoherence): a check for a song just loaded, as a
	/// mode switch keeps the parked engine's params
	bool parked = false;
	/// The leg is expected to break: each break is printed as EXPECTED-INCOHERENT, not FAIL.
	bool expect_incoherent = false;
};

/// @brief The instrument state of the slot @p address names, encoded at the slot's own tick magnitude.
/// @param song    The song whose project holds the slot.
/// @param address The slot's address.
/// @return The encoding, or nullopt where @p song has no such slot.
std::optional<deluge::serialization::Value> encodedSlot(::Song& song, deluge::project::SlotAddress address);

/// @brief Find the first equation @p sound breaks. A sound with no slot in @p song breaks none.
/// @param sound  The sound to check.
/// @param song   The song whose project holds the sound's slot and controls records.
/// @param checks Which equations to hold the sound to.
/// @return The break as "<check> at <where>", or empty if the sound is coherent.
std::string soundIncoherence(::Sound& sound, ::Song& song, CoherenceChecks checks = {});

/// @brief Find the first param of an engine a sound of @p song is not playing that the sound holds: a value other than
///        the param's default or automation (in any of its clips' or kit rows' param sets), a cable rooted at it, a mod
///        knob or a learned MIDI knob on it. The current file form never holds one.
/// @param song The song.
/// @return The break as "<owner>: parked <what> <param>", or empty if no sound holds one.
std::string parkedIncoherence(::Song& song);

/// @brief Find the first host chain of @p song that is not its owner's runtime state encoded.
/// @param song The song whose kit, audio-track and master chains are checked.
/// @return The break as "output <id> effects: <where>" or "master effects: <where>", or empty if all agree.
std::string chainIncoherence(::Song& song);

/// @brief Visit every setting group of every owner @p song addresses: the song, each output's track and (MIDI and CV)
///        instrument device, each kit lane, each note row and each audio clip.
/// @param song  The song.
/// @param visit Called with the address, the owner's durable id (0 for the song) and the paths of the group's table
///              this owner has an engine copy of.
void forEachSettingAddress(::Song& song, const std::function<void(const deluge::project::SettingAddress&, uint32_t,
                                                                  std::span<const std::string_view>)>& visit);

/// @brief Find the first song setting whose record differs from its engine copy, then the first MIDI binding that
///        breaks mappingIncoherence.
/// @param song The song; its reverb is checked against the engine's, so it must be the current song.
/// @return The break as "<group> <owner id> <path>: record=<a> engine=<b>" or as mappingIncoherence reports it, or
///         empty if every setting and binding agrees.
std::string songSettingsIncoherence(::Song& song);

/// @brief Find the first MIDI binding of an owner @p song holds (the song's knobs; each output's, playing or
///        hibernating, with a kit's rows; each clip) whose mapping-table entries differ from the engine field that
///        plays them: a track's or a kit row's input, a kit row's or a clip's mute; or whose owner holds learned
///        knobs outside the table, which holds them alone; then knobIndexIncoherence(). Commands compare by what they
///        play (cable, channel or zone, note or CC), so an entry naming a device that is not connected agrees with a
///        field with no cable.
/// @param song The song.
/// @return The break as "MAPPINGS <kind> <owner id>: table=<a> engine=<b>" (outside=<b> for knobs), or empty if every
///         binding agrees.
std::string mappingIncoherence(::Song& song);

/// @brief Find the first learned knob of @p song's table that the mapping index does not play as the table holds it:
///        rebuilt, the index must play each knob of the song, of an output in the song's main list and of such a kit's
///        rows, as its own target and relative flag; and an index not marked stale must play what the rebuild does (a
///        table write that left it stale unmarked breaks this). Rebuilds the index. A song that is not current breaks
///        nothing.
/// @param song The song.
/// @return The break as "MAPPINGS KNOB_INDEX <owner kind> <id> knob <param>=<source>: <what>", or empty.
std::string knobIndexIncoherence(::Song& song);

/// @brief Find the first of @p paths at @p address whose record differs from its engine copy.
/// @param song    The song.
/// @param address The group and owner.
/// @param owner   The owner's durable id, for the report.
/// @param paths   The paths to compare.
/// @return The break as songSettingsIncoherence reports it, or empty.
std::string settingIncoherence(::Song& song, const deluge::project::SettingAddress& address, uint32_t owner,
                               std::span<const std::string_view> paths);

/// @brief Check every bound sound of @p song (its outputs', its hibernating instruments' and its kits' sound drums),
/// and its controls and host chains as @p checks selects.
///
/// Each break is printed as `SLOT-COHERENT FAIL leg=<leg> <owner>: <what>` (EXPECTED-INCOHERENT in place of FAIL when
/// @p checks expects the leg to break).
/// @param song   The song to check.
/// @param leg    Name of the scenario leg, printed with each break.
/// @param checks Which equations to hold each sound to.
/// @return How many breaks were found.
int32_t expectSongCoherent(::Song& song, const char* leg, CoherenceChecks checks = {});

} // namespace deluge::harness
