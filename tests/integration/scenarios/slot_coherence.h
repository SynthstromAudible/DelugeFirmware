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

#include "deluge-project/setting_group.h"
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class Song;
class Sound;

namespace deluge::harness {

/// @brief Which equations a coherence check holds a sound to.
struct CoherenceChecks {
	bool runtime = true;       ///< the runtime's state == the record (dump_state of seededState() vs the bound state)
	bool slot = true;          ///< the slot's instrument entries == encode(record), by key
	bool snapshot = true;      ///< the snapshot == projectSnapshot() of the record
	bool controls = true;      ///< the owner's controls record and its table knobs == the runtime's knobs
	bool decode = true;        ///< the slot decoded onto the record == the record (the flip would change nothing)
	bool chains = true;        ///< each host chain (a kit's, an audio track's, the master's) == its owner's encoding
	bool song_settings = true; ///< every setting group's record == its engine copy (songSettingsIncoherence)
};

/// @brief Find the first equation @p sound breaks. An unbound sound breaks none.
/// @param sound  The sound to check.
/// @param song   The song whose project holds the sound's slot and controls records.
/// @param checks Which equations to hold the sound to.
/// @return The break as "<check> at <where>", or empty if the sound is coherent.
std::string soundIncoherence(::Sound& sound, ::Song& song, CoherenceChecks checks = {});

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

/// @brief Find the first song setting whose record differs from its engine copy.
/// @param song The song; its reverb is checked against the engine's, so it must be the current song.
/// @return The break as "<group> <owner id> <path>: record=<a> engine=<b>", or empty if every setting agrees.
std::string songSettingsIncoherence(::Song& song);

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
/// Each break is printed as `SLOT-COHERENT FAIL leg=<leg> <owner>: <what>`.
/// @param song   The song to check.
/// @param leg    Name of the scenario leg, printed with each break.
/// @param checks Which equations to hold each sound to.
/// @return How many breaks were found.
int32_t expectSongCoherent(::Song& song, const char* leg, CoherenceChecks checks = {});

} // namespace deluge::harness
