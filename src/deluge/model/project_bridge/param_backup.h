/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/backed_params.h"    // BackedParamKey
#include "modulation/params/param_manager.h" // ParamManager (stored by value)
#include "util/containers.h"                 // deluge::fast_map
#include <cstdint>
#include <list>
#include <optional>
#include <variant>

class Clip;
class Drum;
class ModControllableAudio;
class Song;
class Output;
class ParamManagerForTimeline;
class Sound;

namespace deluge::bridge {

/// @brief Whose params a backup holds: an output (a synth, a kit, an audio track) or a kit's drum, typed.
class BackupOwner {
public:
	BackupOwner(::Output& output) : owner_(&output) {}
	BackupOwner(::Drum& drum) : owner_(&drum) {}
	/// @param output An output, not null.
	BackupOwner(::Output* output) : owner_(output) {}
	/// @param drum A drum, not null.
	BackupOwner(::Drum* drum) : owner_(drum) {}

	/// @brief The output this owner is, or null for a drum.
	[[nodiscard]] ::Output* output() const;
	/// @brief The drum this owner is, or null for an output.
	[[nodiscard]] ::Drum* drum() const;

	/// @brief The owner's address once it is adopted into its song (bound to its record); nullopt before.
	[[nodiscard]] std::optional<project::OwnerId> adopted() const;
	/// @brief The owner's address whenever it has an id, adopted or not.
	[[nodiscard]] std::optional<project::OwnerId> any_id() const;
	/// @brief Whether @p other is this owner.
	[[nodiscard]] bool is(const BackupOwner& other) const { return owner_ == other.owner_; }

private:
	std::variant<::Output*, ::Drum*> owner_;
};

/// @brief The backup owner a sound is: its synth track, or its kit row.
/// @param sound A SoundInstrument or a SoundDrum.
/// @return The typed owner.
[[nodiscard]] BackupOwner backupOwnerOf(::Sound& sound);

/// @brief The backup owner whose engine @p owner is, among @p song's outputs, hibernating instruments and kit rows.
/// @param owner A synth's or a kit row's sound, a kit, or an audio track.
/// @param song  The song it is in.
/// @return The typed owner, or nullopt for one that backs nothing up (the song's master).
[[nodiscard]] std::optional<BackupOwner> backupOwnerOf(::ModControllableAudio& owner, ::Song& song);

/// @brief Parked ParamManagers, keyed by owner and clip id.
///
/// When a Clip or an owner is not currently holding its ParamManager live, the manager is stashed here so its
/// automation/state survives. An engine runtime cache, held by the engine Song (`::Song::live`). An owner is keyed by
/// its id once it is adopted into the song. Before that -- while a song or a preset is read, when ids are not yet the
/// ones the song will keep -- its backups wait in `pending`, keyed by the owner itself, and move into `map` when the
/// song adopts it (`adopt`).
///
/// The bulk-delete operations that cooperatively yield to the audio engine (`deleteBackedUpParamManagersForClip` and
/// the two `deleteAll…`) are on the ::Song controller.
struct ParamBackup {
	/// @brief A backup whose owner is not adopted yet.
	struct Pending {
		BackupOwner owner;
		std::optional<uint32_t> clip_id;
		ParamManager params;
	};

	deluge::fast_map<project::BackedParamKey, ParamManager> map;
	std::list<Pending> pending; ///< a list, so a ParamManager it hands out stays put

	/// @brief The backed-up ParamManager for exactly (owner, clip), or nullptr.
	///
	/// If @p stealInto is given, the entry's collections are moved into it and the entry erased.
	/// @param owner     The owner.
	/// @param clip      The clip; null for the owner's own (no-clip) entry.
	/// @param stealInto Optional destination to move the entry's collections into.
	/// @return The entry (or @p stealInto when stolen into), or nullptr if none.
	ParamManager* getForExactClip(BackupOwner owner, const ::Clip* clip, ParamManager* stealInto = nullptr);

	/// @brief A backed-up ParamManager for @p owner: the entry for exactly @p clip if there is one, else the owner's
	///        first entry (its no-clip entry when present).
	///
	/// If @p stealInto is given, the entry is moved into it and erased.
	/// @param owner     The owner.
	/// @param clip      The clip whose own entry is preferred; may be null.
	/// @param stealInto Optional destination to move the entry into.
	/// @return The entry (or @p stealInto when stolen into), or nullptr if none.
	ParamManager* getPreferablyWithClip(BackupOwner owner, const ::Clip* clip, ParamManager* stealInto = nullptr);

	/// @brief Stash @p paramManager under (owner, clip), stealing its param collections.
	/// @param owner                          The owner.
	/// @param clip                           The clip; null for the owner's own entry.
	/// @param paramManager                   The manager whose collections are stolen.
	/// @param shouldStealExpressionParamsToo Whether to steal the expression params as well.
	void backUp(BackupOwner owner, const ::Clip* clip, ParamManagerForTimeline* paramManager,
	            bool shouldStealExpressionParamsToo = false);

	/// @brief Erase every entry belonging to @p owner, by its id and pending alike.
	/// @param owner The owner.
	void deleteFor(BackupOwner owner);

	/// @brief Move @p owner's pending entries into the map under its id, now that its song has adopted it.
	/// @param owner The owner, adopted.
	void adopt(BackupOwner owner);

	/// @brief Every entry of @p owner, by its key: the map's entries for its id when it is adopted, else its pending
	///        entries (keyed by its id if it has one, else by a null owner).
	/// @param owner The owner.
	/// @param visit Called with (clip id, params) for each entry.
	template <typename Visit>
	void forEachOf(BackupOwner owner, Visit visit) {
		if (const std::optional<project::OwnerId> id = owner.adopted()) {
			for (auto it = map.lower_bound({*id, std::nullopt}); it != map.end() && it->first.owner == *id; ++it) {
				visit(it->first.clip_id, it->second);
			}
			return;
		}
		for (Pending& entry : pending) {
			if (entry.owner.is(owner)) {
				visit(entry.clip_id, entry.params);
			}
		}
	}
};

} // namespace deluge::bridge
